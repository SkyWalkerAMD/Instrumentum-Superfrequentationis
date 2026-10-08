// SPDX-License-Identifier: GPL-2.0
/*
 * octool_hwio.c - userspace hardware-access layer. See octool_hwio.h.
 *
 * Backend selection per family:
 *   - If the octool_hwio module is present and usable, everything goes through
 *     it. Loading/trust, device permissions and operation success must still
 *     be verified on the target; a signing certificate alone is insufficient.
 *   - Otherwise, when not locked down, direct userspace paths are used.
 *   - Under lockdown with no module, the family reports HWIO_BE_NONE and its
 *     ops return -EPERM.
 */
#include "octool_hwio.h"
#include "../abi/octool_hwio_abi.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#if defined(__x86_64__) || defined(__i386__)
#include <sys/io.h>	/* iopl, inb/outb - direct port I/O fallback */
#define HAVE_PORT_IO 1
#endif

struct hwio {
	struct hwio_transport t;	/* module or reference transport */
	int have_module;
	int locked_down;
	enum hwio_backend be[HWIO_FAM__COUNT];

	/* direct-path state (lazily set up) */
	int mem_fd;			/* /dev/mem */
	int io_ready;			/* iopl() done */
};

/* ---- lockdown detection ------------------------------------------------- */
int hwio_is_locked_down(void)
{
	FILE *f = fopen("/sys/kernel/security/lockdown", "r");
	char buf[128];
	int locked = 0;

	if (!f)
		return 0;	/* file absent => lockdown LSM not active */
	if (fgets(buf, sizeof(buf), f)) {
		/* format: "none [integrity] confidentiality" - active mode in [] */
		char *l = strchr(buf, '[');
		if (l && strncmp(l, "[none]", 6) != 0)
			locked = 1;
	}
	fclose(f);
	return locked;
}

/* ---- module transport --------------------------------------------------- */
struct mod_ctx {
	int fd;
	volatile uint64_t *mbox;	/* mmap'd page */
	uint64_t id;
};

static int mod_submit(void *vctx, const void *req96, uint64_t *out, size_t words)
{
	struct mod_ctx *c = vctx;
	const struct octool_hwio_req *rq = req96;
	struct octool_hwio_req r = *rq;
	size_t i;
	ssize_t written;
	uint64_t done;
	long spins = 0;

	/* CPU operations use user_id as the requested CPU, not the open token.
	 * Keep the legacy token unchanged on the MMIO wire path. */
	if (r.cmd != OCTOOL_OP_RD_MSR && r.cmd != OCTOOL_OP_WR_MSR &&
	    r.cmd != OCTOOL_OP_CPUID && r.cmd != OCTOOL_OP_RD_TSC)
		r.user_id = c->id;
	c->mbox[OCTOOL_MBOX_DONE] = 0;
	written = write(c->fd, &r, sizeof(r));
	if (written != (ssize_t)sizeof(r))
		return written < 0 ? -errno : -EIO;
	/* module fills the mailbox from inside write(); poll defensively */
	while ((done = __atomic_load_n(&c->mbox[OCTOOL_MBOX_DONE], __ATOMIC_ACQUIRE)) == 0) {
		if (++spins > 100000000L)
			return -ETIMEDOUT;
	}
	/* Accept only success or the module's documented negative errno encoding.
	 * Other nonzero values are not evidence of a completed request. */
	{
		int32_t st = (int32_t)(done >> 32);
		if ((uint32_t)done != 1 || st > 0 || st < -4095)
			return -EPROTO;
		if (st)
			return st;
	}
	for (i = 0; i < words; i++)
		out[i] = c->mbox[i];
	return 0;
}

static void mod_close(void *vctx)
{
	struct mod_ctx *c = vctx;

	if (c->mbox && c->mbox != MAP_FAILED)
		munmap((void *)c->mbox, sysconf(_SC_PAGESIZE));
	if (c->fd >= 0)
		close(c->fd);
	free(c);
}

static int module_transport(const char *dev_path, struct hwio_transport *t)
{
	struct mod_ctx *c;
	char path[256];
	long pg = sysconf(_SC_PAGESIZE);
	ssize_t n;

	if (!dev_path) {
		snprintf(path, sizeof(path), "/dev/%s", OCTOOL_HWIO_DEV);
		dev_path = path;
	}
	c = calloc(1, sizeof(*c));
	if (!c)
		return -ENOMEM;
	c->fd = open(dev_path, O_RDWR | O_CLOEXEC);
	if (c->fd < 0) {
		free(c);
		return -errno;
	}
	c->mbox = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_SHARED, c->fd, 0);
	if (c->mbox == MAP_FAILED) {
		close(c->fd);
		free(c);
		return -errno;
	}
	n = read(c->fd, &c->id, sizeof(c->id));	/* fetch per-open token */
	if (n != (ssize_t)sizeof(c->id)) {
		int rc = n < 0 ? -errno : -EIO;
		mod_close(c);
		return rc;
	}
	t->ctx = c;
	t->submit = mod_submit;
	t->close = mod_close;
	t->name = "module(/dev/mydev)";
	return 0;
}

/* ---- request helpers ---------------------------------------------------- */
static void req_init(struct octool_hwio_req *r, uint64_t cmd)
{
	memset(r, 0, sizeof(*r));
	r->cmd = cmd;
}

static int via_module(hwio_t *h, struct octool_hwio_req *r,
		      uint64_t *out, size_t words)
{
	return h->t.submit(h->t.ctx, r, out, words);
}

/* ---- direct fallbacks --------------------------------------------------- */
static int direct_rdmsr(unsigned cpu, uint32_t reg, uint64_t *val)
{
	char p[64];
	int fd, rc = 0;
	ssize_t n;

	snprintf(p, sizeof(p), "/dev/cpu/%u/msr", cpu);
	fd = open(p, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	n = pread(fd, val, 8, reg);
	if (n != 8)
		rc = n < 0 ? -errno : -EIO;
	close(fd);
	return rc;
}

static int direct_wrmsr(unsigned cpu, uint32_t reg, uint64_t val)
{
	char p[64];
	int fd, rc = 0;
	ssize_t n;

	snprintf(p, sizeof(p), "/dev/cpu/%u/msr", cpu);
	fd = open(p, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	n = pwrite(fd, &val, 8, reg);
	if (n != 8)
		rc = n < 0 ? -errno : -EIO;
	close(fd);
	return rc;
}

static int direct_mem(hwio_t *h, uint64_t phys, int width, uint64_t *val, int write_op)
{
	long pg = sysconf(_SC_PAGESIZE);
	off_t base = (off_t)(phys & ~(uint64_t)(pg - 1));
	unsigned off = (unsigned)(phys & (pg - 1));
	size_t length = (size_t)off + (size_t)width;
	void *map;

	if (h->mem_fd < 0) {
		h->mem_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
		if (h->mem_fd < 0)
			return -errno;
	}
	/* mmap rounds the length up; include the second page for a crossing read.
	 * Do not split one MMIO operation into several hardware transactions. */
	map = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, h->mem_fd, base);
	if (map == MAP_FAILED)
		return -errno;
	volatile void *p = (char *)map + off;
	if (write_op) {
		switch (width) {
		case 1: *(volatile uint8_t *)p = (uint8_t)*val; break;
		case 2: *(volatile uint16_t *)p = (uint16_t)*val; break;
		case 4: *(volatile uint32_t *)p = (uint32_t)*val; break;
		case 8: *(volatile uint64_t *)p = *val; break;
		default: munmap(map, length); return -EINVAL;
		}
	} else {
		switch (width) {
		case 1: *val = *(volatile uint8_t *)p; break;
		case 2: *val = *(volatile uint16_t *)p; break;
		case 4: *val = *(volatile uint32_t *)p; break;
		case 8: *val = *(volatile uint64_t *)p; break;
		default: munmap(map, length); return -EINVAL;
		}
	}
	munmap(map, length);
	return 0;
}

static int direct_pci_read(uint8_t bus, uint8_t dev, uint8_t fn,
			   uint16_t off, int width, uint32_t *val)
{
	char p[96];
	int fd;
	uint32_t v = 0;
	ssize_t n;

	snprintf(p, sizeof(p), "/sys/bus/pci/devices/0000:%02x:%02x.%u/config",
		 bus, dev, fn);
	fd = open(p, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	n = pread(fd, &v, width, off);
	if (n != width) {
		int rc = n < 0 ? -errno : -EIO;
		close(fd);
		return rc;
	}
	close(fd);
	*val = v;
	return 0;
}

static int direct_pci_write(uint8_t bus, uint8_t dev, uint8_t fn,
			    uint16_t off, int width, uint32_t val)
{
	char p[96];
	int fd, rc = 0;
	ssize_t n;

	snprintf(p, sizeof(p), "/sys/bus/pci/devices/0000:%02x:%02x.%u/config",
		 bus, dev, fn);
	fd = open(p, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	n = pwrite(fd, &val, width, off);
	if (n != width)
		rc = n < 0 ? -errno : -EIO;
	close(fd);
	return rc;
}

#ifdef HAVE_PORT_IO
static int ensure_iopl(hwio_t *h)
{
	if (h->io_ready)
		return 0;
	if (iopl(3) != 0)
		return -errno;
	h->io_ready = 1;
	return 0;
}
#endif

/* ---- backend planning --------------------------------------------------- */
static void plan_backends(hwio_t *h)
{
	int i;
	enum hwio_backend b;

	if (h->have_module)
		b = HWIO_BE_MODULE;
	else if (!h->locked_down)
		b = HWIO_BE_DIRECT;
	else
		b = HWIO_BE_NONE;
	for (i = 0; i < HWIO_FAM__COUNT; i++)
		h->be[i] = b;

	/* CPUID/TSC/cores can always be answered locally without privilege, so
	 * never mark that family unavailable. */
	if (h->be[HWIO_FAM_CPU] == HWIO_BE_NONE)
		h->be[HWIO_FAM_CPU] = HWIO_BE_DIRECT;
}

/* ---- open/close --------------------------------------------------------- */
hwio_t *hwio_open(const char *dev_path)
{
	hwio_t *h = calloc(1, sizeof(*h));

	if (!h)
		return NULL;
	h->mem_fd = -1;
	h->locked_down = hwio_is_locked_down();
	if (module_transport(dev_path, &h->t) == 0)
		h->have_module = 1;
	plan_backends(h);
	return h;
}

hwio_t *hwio_open_transport(const struct hwio_transport *t)
{
	hwio_t *h = calloc(1, sizeof(*h));

	if (!h)
		return NULL;
	h->mem_fd = -1;
	h->t = *t;
	h->have_module = 1;	/* an explicit transport behaves like the module */
	plan_backends(h);
	return h;
}

void hwio_close(hwio_t *h)
{
	if (!h)
		return;
	if (h->t.close)
		h->t.close(h->t.ctx);
	if (h->mem_fd >= 0)
		close(h->mem_fd);
	free(h);
}

enum hwio_backend hwio_backend_for(hwio_t *h, enum hwio_family fam)
{
	if (!h || fam >= HWIO_FAM__COUNT)
		return HWIO_BE_NONE;
	return h->be[fam];
}

const char *hwio_backend_str(enum hwio_backend b)
{
	switch (b) {
	case HWIO_BE_MODULE: return "module";
	case HWIO_BE_DIRECT: return "direct";
	default: return "none";
	}
}

/* ---- operations --------------------------------------------------------- */
#define WORDS 5	/* enough for CPUID's four dwords */

int hwio_rdmsr(hwio_t *h, unsigned cpu, uint32_t reg, uint64_t *val)
{
	if (h->be[HWIO_FAM_MSR] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS]; int rc;
		req_init(&r, OCTOOL_OP_RD_MSR); r.user_id = cpu; r.data0 = reg;
		rc = via_module(h, &r, m, WORDS);
		if (!rc) *val = m[OCTOOL_MBOX_RESULT];
		return rc;
	}
	if (h->be[HWIO_FAM_MSR] == HWIO_BE_DIRECT)
		return direct_rdmsr(cpu, reg, val);
	return -EPERM;
}

int hwio_wrmsr(hwio_t *h, unsigned cpu, uint32_t reg, uint64_t val)
{
	if (h->be[HWIO_FAM_MSR] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS];
		req_init(&r, OCTOOL_OP_WR_MSR); r.user_id = cpu; r.data0 = reg; r.data1 = val;
		return via_module(h, &r, m, WORDS);
	}
	if (h->be[HWIO_FAM_MSR] == HWIO_BE_DIRECT)
		return direct_wrmsr(cpu, reg, val);
	return -EPERM;
}

static uint64_t mmio_read_op(int w)
{
	switch (w) { case 1: return OCTOOL_OP_RD_MEM8; case 2: return OCTOOL_OP_RD_MEM16;
	case 4: return OCTOOL_OP_RD_MEM32; case 8: return OCTOOL_OP_RD_MEM64; } return 0;
}
static uint64_t mmio_write_op(int w)
{
	switch (w) { case 1: return OCTOOL_OP_WR_MEM8; case 2: return OCTOOL_OP_WR_MEM16;
	case 4: return OCTOOL_OP_WR_MEM32; case 8: return OCTOOL_OP_WR_MEM64; } return 0;
}

int hwio_mem_read(hwio_t *h, uint64_t phys, int width, uint64_t *val)
{
	uint64_t op = mmio_read_op(width);

	if (!op || phys > UINT64_MAX - (uint64_t)(width - 1)) return -EINVAL;
	if (h->be[HWIO_FAM_MMIO] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS]; int rc;
		req_init(&r, op); r.data0 = phys;
		rc = via_module(h, &r, m, WORDS);
		if (!rc) *val = m[OCTOOL_MBOX_RESULT];
		return rc;
	}
	if (h->be[HWIO_FAM_MMIO] == HWIO_BE_DIRECT)
		return direct_mem(h, phys, width, val, 0);
	return -EPERM;
}

int hwio_mem_write(hwio_t *h, uint64_t phys, int width, uint64_t val)
{
	uint64_t op = mmio_write_op(width);

	if (!op || phys > UINT64_MAX - (uint64_t)(width - 1)) return -EINVAL;
	if (h->be[HWIO_FAM_MMIO] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS];
		req_init(&r, op); r.data0 = phys; r.data1 = val;
		return via_module(h, &r, m, WORDS);
	}
	if (h->be[HWIO_FAM_MMIO] == HWIO_BE_DIRECT)
		return direct_mem(h, phys, width, &val, 1);
	return -EPERM;
}

int hwio_io_read(hwio_t *h, uint16_t port, int width, uint32_t *val)
{
	uint64_t op = width == 1 ? OCTOOL_OP_IN_8 : width == 2 ? OCTOOL_OP_IN_16 :
		      width == 4 ? OCTOOL_OP_IN_32 : 0;

	if (!op) return -EINVAL;
	if (h->be[HWIO_FAM_IO] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS]; int rc;
		req_init(&r, op); r.data0 = port;
		rc = via_module(h, &r, m, WORDS);
		if (!rc) *val = (uint32_t)m[OCTOOL_MBOX_RESULT];
		return rc;
	}
#ifdef HAVE_PORT_IO
	if (h->be[HWIO_FAM_IO] == HWIO_BE_DIRECT) {
		int rc = ensure_iopl(h);
		if (rc) return rc;
		switch (width) { case 1: *val = inb(port); break; case 2: *val = inw(port); break;
		default: *val = inl(port); } return 0;
	}
#endif
	return -EPERM;
}

int hwio_io_write(hwio_t *h, uint16_t port, int width, uint32_t val)
{
	uint64_t op = width == 1 ? OCTOOL_OP_OUT_8 : width == 2 ? OCTOOL_OP_OUT_16 :
		      width == 4 ? OCTOOL_OP_OUT_32 : 0;

	if (!op) return -EINVAL;
	if (h->be[HWIO_FAM_IO] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS];
		req_init(&r, op); r.data0 = port; r.data1 = val;
		return via_module(h, &r, m, WORDS);
	}
#ifdef HAVE_PORT_IO
	if (h->be[HWIO_FAM_IO] == HWIO_BE_DIRECT) {
		int rc = ensure_iopl(h);
		if (rc) return rc;
		switch (width) { case 1: outb((uint8_t)val, port); break;
		case 2: outw((uint16_t)val, port); break; default: outl(val, port); }
		return 0;
	}
#endif
	return -EPERM;
}

static int valid_pci(uint8_t dev, uint8_t fn, uint16_t off, int width)
{
	/* The module's config mechanism and the GUI support domain 0, 256 bytes.
	 * Validate before either backend can truncate an address or use width as
	 * the byte count of a four-byte local buffer. */
	return (width == 1 || width == 2 || width == 4) &&
	       dev < 32 && fn < 8 && off < 256 && !(off & (width - 1));
}

int hwio_pci_read(hwio_t *h, uint8_t bus, uint8_t dev, uint8_t fn,
		  uint16_t off, int width, uint32_t *val)
{
	if (!valid_pci(dev, fn, off, width)) return -EINVAL;
	if (h->be[HWIO_FAM_PCI] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS]; int rc;
		req_init(&r, OCTOOL_OP_PCI_RD);
		r.data0 = bus; r.data1 = dev; r.data2 = fn; r.data3 = off; r.data4 = width;
		rc = via_module(h, &r, m, WORDS);
		if (!rc) *val = (uint32_t)m[OCTOOL_MBOX_RESULT];
		return rc;
	}
	if (h->be[HWIO_FAM_PCI] == HWIO_BE_DIRECT)
		return direct_pci_read(bus, dev, fn, off, width, val);
	return -EPERM;
}

int hwio_pci_write(hwio_t *h, uint8_t bus, uint8_t dev, uint8_t fn,
		   uint16_t off, int width, uint32_t val)
{
	if (!valid_pci(dev, fn, off, width)) return -EINVAL;
	if (h->be[HWIO_FAM_PCI] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS];
		req_init(&r, OCTOOL_OP_PCI_WR);
		r.data0 = bus; r.data1 = dev; r.data2 = fn; r.data3 = off;
		r.data4 = width; r.data5 = val;
		return via_module(h, &r, m, WORDS);
	}
	if (h->be[HWIO_FAM_PCI] == HWIO_BE_DIRECT)
		return direct_pci_write(bus, dev, fn, off, width, val);
	return -EPERM;
}

int hwio_ec_read(hwio_t *h, uint8_t index, uint8_t *val)
{
	if (h->be[HWIO_FAM_EC] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS]; int rc;
		req_init(&r, OCTOOL_OP_EC_RD); r.data0 = index;
		rc = via_module(h, &r, m, WORDS);
		if (!rc) *val = (uint8_t)m[OCTOOL_MBOX_RESULT];
		return rc;
	}
	return -EPERM;	/* no safe generic direct EC path */
}

int hwio_ec_write(hwio_t *h, uint8_t index, uint8_t val)
{
	if (h->be[HWIO_FAM_EC] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS];
		req_init(&r, OCTOOL_OP_EC_WR); r.data0 = index; r.data1 = val;
		return via_module(h, &r, m, WORDS);
	}
	return -EPERM;
}

int hwio_cpuid(hwio_t *h, unsigned cpu, uint32_t leaf, uint32_t subleaf, uint32_t out[4])
{
	if (h->be[HWIO_FAM_CPU] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS]; int rc;
		req_init(&r, OCTOOL_OP_CPUID); r.user_id = cpu; r.data0 = leaf; r.data1 = subleaf;
		rc = via_module(h, &r, m, WORDS);
		if (!rc) { out[0]=(uint32_t)m[OCTOOL_MBOX_RESULT]; out[1]=(uint32_t)m[OCTOOL_MBOX_RESULT2];
			   out[2]=(uint32_t)m[OCTOOL_MBOX_RESULT3]; out[3]=(uint32_t)m[OCTOOL_MBOX_RESULT4]; }
		return rc;
	}
#if defined(__x86_64__) || defined(__i386__)
	{
		uint32_t a=leaf,b=0,c=subleaf,d=0;
		__asm__ volatile("cpuid":"+a"(a),"+b"(b),"+c"(c),"+d"(d));
		out[0]=a; out[1]=b; out[2]=c; out[3]=d;
		return 0;	/* runs on the calling CPU; caller pins if needed */
	}
#else
	return -ENOSYS;
#endif
}

int hwio_rdtsc(hwio_t *h, unsigned cpu, uint64_t *tsc)
{
	if (h->be[HWIO_FAM_CPU] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS]; int rc;
		req_init(&r, OCTOOL_OP_RD_TSC); r.user_id = cpu;
		rc = via_module(h, &r, m, WORDS);
		if (!rc) *tsc = m[OCTOOL_MBOX_RESULT];
		return rc;
	}
#if defined(__x86_64__) || defined(__i386__)
	{
		uint32_t lo,hi;
		__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
		*tsc = ((uint64_t)hi<<32)|lo;
		return 0;
	}
#else
	return -ENOSYS;
#endif
}

int hwio_cpu_cores(hwio_t *h, unsigned *cores)
{
	if (h->be[HWIO_FAM_CPU] == HWIO_BE_MODULE) {
		struct octool_hwio_req r; uint64_t m[WORDS]; int rc;
		req_init(&r, OCTOOL_OP_CPU_CORES);
		rc = via_module(h, &r, m, WORDS);
		if (!rc) *cores = (unsigned)m[OCTOOL_MBOX_RESULT];
		return rc;
	}
	{
		long n = sysconf(_SC_NPROCESSORS_ONLN);
		if (n < 0) return -errno;
		*cores = (unsigned)n;
		return 0;
	}
}
