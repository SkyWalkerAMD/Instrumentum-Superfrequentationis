// SPDX-License-Identifier: GPL-2.0
/*
 * octool_hwio - low-level hardware access backend for octool.
 *
 * This is a clean-room reimplementation of the peter_kernel character driver
 * that octool talks to, written for portability across EL8-EL10 and
 * Ubuntu 20.04-26.04 and Debian 11-13. The successful MMIO request layout,
 * opcodes and mailbox offsets match the shipped binary. This does NOT prove
 * drop-in operation: its loader handshake is separate, and its full 64-bit
 * done==1 wait cannot consume this module's negative errno completion words.
 * See docs/legacy-mailbox-contract.md and docs/legacy-module-handoff.md.
 *
 * On top of that it exposes MSR, port-I/O, PCI-config and EC operations
 * through the same device for the reconstructed GUI/HAL. Signing, trust,
 * permissions and hardware behavior need target-machine acceptance; the
 * old binary's direct paths are not redirected by loading this module.
 * Those opcodes use this module's own numbering
 * (see below) - the shipped binary never sends them, so there is nothing to be
 * compatible with; octool's access layer is updated to match.
 *
 * Design notes vs. the original:
 *  - No per-CPU kthread. The original created a kthread per request with
 *    kthread_create_on_cpu(), which is not exported before Linux 5.17 and made
 *    the module fail to build on old kernels. CPU-pinned operations (MSR,
 *    CPUID, TSC) use the in-tree rdmsr_safe_on_cpu()/smp_call_function_single()
 *    helpers, which exist on every target (4.18+). MMIO/PCI/port ops are CPU
 *    agnostic and run inline. This removes the only other version-sensitive
 *    dependency; class_create()'s arity (probed by Kbuild) is the last one.
 *  - Every operation is gated on CAP_SYS_RAWIO, checked at open().
 *  - Reads/writes of the request are bounds-checked; ioremap mappings are
 *    released on every path.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/capability.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/atomic.h>
#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/io.h>
#include "octool_hwio_abi.h"
#include "../abi/octool_hwio_caps.h"
#include "octool_bus_access.h"

#define DRV_NAME	"octool_hwio"
#define DEFAULT_DEVNAME	"mydev"		/* octool opens /dev/mydev */

static char *devname = DEFAULT_DEVNAME;
module_param(devname, charp, 0444);
MODULE_PARM_DESC(devname, "character device name under /dev (default: mydev)");

/* Allow non-root openers that hold CAP_SYS_RAWIO only; off by default keeps
 * the node effectively root-only even if its mode is loosened. */
static bool allow_unpriv;
module_param(allow_unpriv, bool, 0644);
MODULE_PARM_DESC(allow_unpriv, "permit any opener holding CAP_SYS_RAWIO (default: N)");

/*
 * Mailbox: one page shared with userspace via mmap(offset 0). octool clears
 * slot[0] to 0, issues the write(), then spins until slot[0] == 1 and reads
 * the scalar result from slot[1] (byte offset 8). CPUID returns four dwords in
 * slot[1..4]. Do not change slot[0]/slot[1] - the shipped binary depends on
 * them.
 */
struct hwio_ctx {
	unsigned long	*mbox;		/* one zeroed page */
	u64		id;
};


static dev_t hwio_devt;
static struct cdev hwio_cdev;
static struct class *hwio_class;
static atomic64_t id_ctr = ATOMIC64_INIT(1);

/* ---- MMIO --------------------------------------------------------------- */
static int mmio_read(u64 phys, int width, u64 *out)
{
	void __iomem *base;

	/* Let ioremap cover the entire access, including a possible page boundary.
	 * Its returned pointer already includes the physical address's offset. */
	base = ioremap(phys, width);
	if (!base)
		return -ENOMEM;
	switch (width) {
	case 1: *out = readb(base); break;
	case 2: *out = readw(base); break;
	case 4: *out = readl(base); break;
	case 8: *out = readq(base); break;
	default: iounmap(base); return -EINVAL;
	}
	iounmap(base);
	return 0;
}

static int mmio_write(u64 phys, int width, u64 val)
{
	void __iomem *base;

	base = ioremap(phys, width);
	if (!base)
		return -ENOMEM;
	switch (width) {
	case 1: writeb((u8)val, base); break;
	case 2: writew((u16)val, base); break;
	case 4: writel((u32)val, base); break;
	case 8: writeq(val, base); break;
	default: iounmap(base); return -EINVAL;
	}
	iounmap(base);
	return 0;
}

/* ---- CPUID / TSC on a target CPU ---------------------------------------- */
struct cpuid_arg { u32 leaf, sub, a, b, c, d; };
static void do_cpuid(void *p)
{
	struct cpuid_arg *x = p;

	cpuid_count(x->leaf, x->sub, &x->a, &x->b, &x->c, &x->d);
}

struct tsc_arg { u64 v; };
static void do_rdtsc(void *p)
{
	((struct tsc_arg *)p)->v = rdtsc();
}

/* ---- request dispatch --------------------------------------------------- */
static void dispatch(struct hwio_ctx *ctx, struct octool_hwio_req *r)
{
	unsigned long *mbox = ctx->mbox;
	unsigned int cpu = (unsigned int)r->user_id;
	u64 res = 0;
	int rc = 0;

	/* Reject an invalid CPU for CPU-specific requests. Falling back to the
	 * caller's CPU could write an MSR on a different processor. MMIO keeps
	 * accepting the legacy per-open token in this field, as before. */
	if ((r->cmd == OCTOOL_OP_RD_MSR || r->cmd == OCTOOL_OP_WR_MSR ||
	     r->cmd == OCTOOL_OP_CPUID || r->cmd == OCTOOL_OP_RD_TSC) &&
	    (r->user_id >= nr_cpu_ids || !cpu_online(cpu))) {
		rc = -EINVAL;
		mbox[OCTOOL_MBOX_RESULT] = 0;
		goto done;
	}

	switch (r->cmd) {
	/* MMIO success layout is preserved; see the error-completion caveat below. */
	case OCTOOL_OP_RD_MEM8:  rc = mmio_read(r->data0, 1, &res); break;
	case OCTOOL_OP_RD_MEM16: rc = mmio_read(r->data0, 2, &res); break;
	case OCTOOL_OP_RD_MEM32: rc = mmio_read(r->data0, 4, &res); break;
	case OCTOOL_OP_RD_MEM64: rc = mmio_read(r->data0, 8, &res); break;
	case OCTOOL_OP_WR_MEM8:  rc = mmio_write(r->data0, 1, r->data1); break;
	case OCTOOL_OP_WR_MEM16: rc = mmio_write(r->data0, 2, r->data1); break;
	case OCTOOL_OP_WR_MEM32: rc = mmio_write(r->data0, 4, r->data1); break;
	case OCTOOL_OP_WR_MEM64: rc = mmio_write(r->data0, 8, r->data1); break;

	/* MSR / CPUID / TSC on a target CPU */
	case OCTOOL_OP_RD_MSR: {
		u32 lo, hi;

		rc = rdmsr_safe_on_cpu(cpu, (u32)r->data0, &lo, &hi);
		res = rc ? 0 : ((u64)hi << 32) | lo;
		break;
	}
	case OCTOOL_OP_WR_MSR:
		rc = wrmsr_safe_on_cpu(cpu, (u32)r->data0,
				       (u32)r->data1, (u32)(r->data1 >> 32));
		break;
	case OCTOOL_OP_RD_TSC: {
		struct tsc_arg a = { 0 };

		rc = smp_call_function_single(cpu, do_rdtsc, &a, 1);
		res = a.v;
		break;
	}
	case OCTOOL_OP_CPUID: {
		struct cpuid_arg a = { .leaf = (u32)r->data0, .sub = (u32)r->data1 };

		rc = smp_call_function_single(cpu, do_cpuid, &a, 1);
		mbox[OCTOOL_MBOX_RESULT]  = a.a;
		mbox[OCTOOL_MBOX_RESULT2] = a.b;
		mbox[OCTOOL_MBOX_RESULT3] = a.c;
		mbox[OCTOOL_MBOX_RESULT4] = a.d;
		res = a.a;
		goto done;	/* multi-word result already stored */
	}

	/* Port I/O */
	case OCTOOL_OP_IN_8:  res = inb((u16)r->data0); break;
	case OCTOOL_OP_IN_16: res = inw((u16)r->data0); break;
	case OCTOOL_OP_IN_32: res = inl((u16)r->data0); break;
	case OCTOOL_OP_OUT_8:  outb((u8)r->data1,  (u16)r->data0); break;
	case OCTOOL_OP_OUT_16: outw((u16)r->data1, (u16)r->data0); break;
	case OCTOOL_OP_OUT_32: outl((u32)r->data1, (u16)r->data0); break;

	/* PCI config through the core; domain 0, conventional 256-byte space. */
	case OCTOOL_OP_PCI_RD:
	case OCTOOL_OP_PCI_WR:
		rc = octool_pci_access(r->data0, r->data1, r->data2, r->data3,
				       r->data4, r->data5, r->cmd == OCTOOL_OP_PCI_WR, &res);
		break;

	/* First EC registered by the ACPI subsystem; never raw fixed ports. */
	case OCTOOL_OP_EC_RD:
	case OCTOOL_OP_EC_WR:
		rc = octool_ec_access(r->data0, r->data1,
				      r->cmd == OCTOOL_OP_EC_WR, &res);
		break;

	case OCTOOL_OP_CPU_CORES:
		res = num_online_cpus();
		break;

	default:
		rc = -EINVAL;
		break;
	}

	mbox[OCTOOL_MBOX_RESULT] = res;
done:
	/* Success is exactly 1. The HAL understands negative errno in the high
	 * half; the original GUI waits for the entire word to equal 1 and will
	 * hang on errors. Do not erase errno to manufacture a successful reading. */
	smp_wmb();
	mbox[OCTOOL_MBOX_DONE] = rc ? (u64)(u32)rc << 32 | 1 : 1;
}

/* ---- file ops ----------------------------------------------------------- */
static int hwio_open(struct inode *ino, struct file *f)
{
	struct hwio_ctx *ctx;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (!allow_unpriv && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->mbox = (unsigned long *)get_zeroed_page(GFP_KERNEL);
	if (!ctx->mbox) {
		kfree(ctx);
		return -ENOMEM;
	}
	ctx->id = atomic64_inc_return(&id_ctr);
	f->private_data = ctx;
	return 0;
}

static int hwio_release(struct inode *ino, struct file *f)
{
	struct hwio_ctx *ctx = f->private_data;

	if (ctx) {
		free_page((unsigned long)ctx->mbox);
		kfree(ctx);
	}
	return 0;
}

/* read() hands userspace an 8-byte per-open token; octool stores it and echoes
 * it back in request.user_id. Any value is accepted. */
static ssize_t hwio_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	struct hwio_ctx *ctx = f->private_data;

	if (len < sizeof(ctx->id))
		return -EINVAL;
	if (copy_to_user(buf, &ctx->id, sizeof(ctx->id)))
		return -EFAULT;
	return sizeof(ctx->id);
}

static ssize_t hwio_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	struct hwio_ctx *ctx = f->private_data;
	struct octool_hwio_req r;

	if (len < sizeof(r))
		return -EINVAL;
	if (copy_from_user(&r, buf, sizeof(r)))
		return -EFAULT;
	dispatch(ctx, &r);
	return len;
}

static int hwio_mmap(struct file *f, struct vm_area_struct *vma)
{
	struct hwio_ctx *ctx = f->private_data;
	unsigned long pfn = virt_to_phys(ctx->mbox) >> PAGE_SHIFT;

	if (vma->vm_end - vma->vm_start > PAGE_SIZE)
		return -EINVAL;
	if (vma->vm_pgoff)
		return -EINVAL;
	return remap_pfn_range(vma, vma->vm_start, pfn,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

/* Query supported command families without sending a hardware request. Old
 * clients continue using read/write/mmap unchanged and need not call this. */
static long hwio_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct octool_hwio_caps caps = {
		.magic = OCTOOL_CAPS_MAGIC,
		.version = OCTOOL_CAPS_VERSION,
		.size = sizeof(struct octool_hwio_caps),
		.features = OCTOOL_CAP_MSR | OCTOOL_CAP_MMIO | OCTOOL_CAP_IO |
			    OCTOOL_CAP_PCI | OCTOOL_CAP_CPU,
	};

	(void)f;
	BUILD_BUG_ON(sizeof(caps) != 32);
	if (cmd != OCTOOL_HWIO_GET_CAPS_V1)
		return -ENOTTY;
#if IS_ENABLED(CONFIG_ACPI)
	caps.features |= OCTOOL_CAP_EC;
#endif
	return copy_to_user((void __user *)arg, &caps, sizeof(caps)) ? -EFAULT : 0;
}

static const struct file_operations hwio_fops = {
	.owner		= THIS_MODULE,
	.open		= hwio_open,
	.release	= hwio_release,
	.read		= hwio_read,
	.write		= hwio_write,
	.mmap		= hwio_mmap,
	.unlocked_ioctl = hwio_ioctl,
	.llseek		= noop_llseek,
};

/* ---- module init/exit --------------------------------------------------- */
static int __init hwio_init(void)
{
	int rc;
	struct device *dev;

	rc = alloc_chrdev_region(&hwio_devt, 0, 1, DRV_NAME);
	if (rc)
		return rc;
	cdev_init(&hwio_cdev, &hwio_fops);
	hwio_cdev.owner = THIS_MODULE;
	rc = cdev_add(&hwio_cdev, hwio_devt, 1);
	if (rc)
		goto err_region;

#if defined(PK_CLASS_CREATE_1ARG)
	hwio_class = class_create(DRV_NAME);
#else
	hwio_class = class_create(THIS_MODULE, DRV_NAME);
#endif
	if (IS_ERR(hwio_class)) {
		rc = PTR_ERR(hwio_class);
		goto err_cdev;
	}
	dev = device_create(hwio_class, NULL, hwio_devt, NULL, "%s", devname);
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		goto err_class;
	}
	pr_info("%s: /dev/%s ready (major %d)\n", DRV_NAME, devname, MAJOR(hwio_devt));
	return 0;

err_class:
	class_destroy(hwio_class);
err_cdev:
	cdev_del(&hwio_cdev);
err_region:
	unregister_chrdev_region(hwio_devt, 1);
	return rc;
}

static void __exit hwio_exit(void)
{
	device_destroy(hwio_class, hwio_devt);
	class_destroy(hwio_class);
	cdev_del(&hwio_cdev);
	unregister_chrdev_region(hwio_devt, 1);
}

module_init(hwio_init);
module_exit(hwio_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("octool low-level hardware access backend (MMIO/MSR/PCI/IO/EC)");
MODULE_VERSION("2.0.1");
