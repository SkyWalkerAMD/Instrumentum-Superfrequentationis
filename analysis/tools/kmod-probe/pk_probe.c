// SPDX-License-Identifier: GPL-2.0
/*
 * pk_probe.c - compile-only probe that exercises the same kernel APIs that
 * peter_kernel.ko imports (read from its __versions/DWARF): chrdev + class +
 * device node, mmap served by a .fault handler over a get_zeroed_page() page,
 * copy_{from,to}_user, ioremap + readl/writel/writeq, a kthread pinned to a CPU,
 * kmalloc/kfree, cpu_data(). It is NOT peter_kernel.c; it only answers
 * "does this API set build and link against kernel X".
 *
 * Variants (make PK_VARIANT=...):
 *   asis   - class_create(name) one-arg + kthread_create_on_cpu(), i.e. what the
 *            6.8 / 6.17 builds of peter_kernel.ko import
 *   old    - class_create(THIS_MODULE, name) two-arg, what the 6.2 build imports
 *   compat - class_create arity chosen by probing the kernel headers (Kbuild),
 *            kthread_create_on_node() + kthread_bind() instead of
 *            kthread_create_on_cpu()
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/cpumask.h>
#include <linux/topology.h>
#include <asm/processor.h>

static dev_t pk_devno;
static struct cdev pk_cdev;
static struct class *pk_class;
static unsigned long pk_page;

static vm_fault_t pk_fault(struct vm_fault *vmf)
{
	struct page *page = virt_to_page((void *)pk_page);

	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct pk_vm_ops = { .fault = pk_fault };

static int pk_mmap(struct file *f, struct vm_area_struct *vma)
{
	vma->vm_ops = &pk_vm_ops;
	return 0;
}

static int pk_worker(void *arg)
{
	volatile u64 *slot = (u64 *)pk_page;
	u32 lo, hi;

	asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x10));	/* TSC MSR, own asm like RdmsrTx_kernel */
	slot[1] = ((u64)hi << 32) | lo;
	slot[0] = 1;
	return 0;
}

static struct task_struct *pk_spawn_on(unsigned int cpu)
{
#if defined(PK_VARIANT_ASIS) || defined(PK_VARIANT_OLD)
	return kthread_create_on_cpu(pk_worker, NULL, cpu, "pk/%u");
#else
	struct task_struct *t = kthread_create_on_node(pk_worker, NULL, cpu_to_node(cpu), "pk/%u", cpu);

	if (!IS_ERR(t))
		kthread_bind(t, cpu);
	return t;
#endif
}

static ssize_t pk_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	u64 req[12] = { 0 };
	struct task_struct *t;
	void __iomem *p;

	if (len > sizeof(req))
		len = sizeof(req);
	if (copy_from_user(req, buf, len))
		return -EFAULT;
	t = pk_spawn_on((unsigned int)req[1] % num_online_cpus());
	if (!IS_ERR(t))
		wake_up_process(t);
	if (req[2]) {
		p = ioremap(req[2] & PAGE_MASK, PAGE_SIZE);
		if (p) {
			req[3] = readl(p);
			writel((u32)req[4], p);
			writeq(req[5], p);
			iounmap(p);
		}
	}
	return len;
}

static ssize_t pk_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	u64 info[2] = { virt_to_phys((void *)pk_page), cpu_data(0).x86_model };
	void *tmp = kmalloc(64, GFP_KERNEL);

	kfree(tmp);
	if (len > sizeof(info))
		len = sizeof(info);
	return copy_to_user(buf, info, len) ? -EFAULT : len;
}

static const struct file_operations pk_fops = {
	.owner = THIS_MODULE,
	.read = pk_read,
	.write = pk_write,
	.mmap = pk_mmap,
};

static int __init pk_init(void)
{
	int rc = alloc_chrdev_region(&pk_devno, 0, 1, "pkprobe");

	if (rc)
		return rc;
	cdev_init(&pk_cdev, &pk_fops);
	rc = cdev_add(&pk_cdev, pk_devno, 1);
	if (rc)
		goto out_region;
#if defined(PK_VARIANT_OLD) || (defined(PK_VARIANT_COMPAT) && !defined(PK_CLASS_CREATE_1ARG))
	pk_class = class_create(THIS_MODULE, "pkprobe");
#else
	pk_class = class_create("pkprobe");
#endif
	if (IS_ERR(pk_class)) {
		rc = PTR_ERR(pk_class);
		goto out_cdev;
	}
	device_create(pk_class, NULL, pk_devno, NULL, "pkprobe");
	pk_page = get_zeroed_page(GFP_KERNEL);
	if (!pk_page) {
		rc = -ENOMEM;
		goto out_class;
	}
	return 0;
out_class:
	device_destroy(pk_class, pk_devno);
	class_destroy(pk_class);
out_cdev:
	cdev_del(&pk_cdev);
out_region:
	unregister_chrdev_region(pk_devno, 1);
	return rc;
}

static void __exit pk_exit(void)
{
	free_page(pk_page);
	device_destroy(pk_class, pk_devno);
	class_destroy(pk_class);
	cdev_del(&pk_cdev);
	unregister_chrdev_region(pk_devno, 1);
}

module_init(pk_init);
module_exit(pk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("compile probe for the peter_kernel API set");
