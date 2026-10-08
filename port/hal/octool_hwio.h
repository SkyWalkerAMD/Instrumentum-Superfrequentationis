/* SPDX-License-Identifier: GPL-2.0 */
/*
 * octool_hwio.h - userspace hardware-access layer for octool.
 *
 * One API for MSR / MMIO / port-I/O / PCI-config / EC / CPUID access, with a
 * backend chosen at runtime per operation family:
 *
 *   1. the octool_hwio kernel module (/dev/mydev), subject to kernel trust,
 *      device permissions and hardware support. Signing alone is not proof
 *      of Secure Boot / lockdown acceptance;
 *   2. direct userspace paths (/dev/cpu/N/msr, /dev/mem, iopl()+in/out,
 *      /sys/bus/pci) - only when the kernel is NOT locked down.
 *
 * The reconstructed GUI uses this API. Loading the module does not redirect
 * the source-lost GUI's direct helpers or repair its loader/error handling.
 *
 * Thread-safety: open one hwio handle per thread, or serialize calls on a
 * shared handle. The kernel module supports concurrent opens (one mailbox per
 * open); a single hwio handle owns one open. Direct port I/O currently caches
 * iopl state per handle: that handle must remain on the same OS thread.
 */
#ifndef OCTOOL_HWIO_H
#define OCTOOL_HWIO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hwio hwio_t;

/* Backend actually in use for a family (for diagnostics/UI). */
enum hwio_backend {
	HWIO_BE_NONE = 0,	/* unavailable (e.g. locked down, no module) */
	HWIO_BE_MODULE,		/* via /dev/mydev */
	HWIO_BE_DIRECT,		/* via /dev/cpu/msr, /dev/mem, iopl, sysfs */
};

enum hwio_family {
	HWIO_FAM_MSR = 0,
	HWIO_FAM_MMIO,
	HWIO_FAM_IO,
	HWIO_FAM_PCI,
	HWIO_FAM_EC,
	HWIO_FAM_CPU,		/* cpuid / tsc / cores */
	HWIO_FAM__COUNT
};

/*
 * Transport abstraction. The default transport is the char device; the test
 * suite injects an in-process reference transport. submit() fills the mailbox
 * exactly as the kernel module would (mbox[0]=done, mbox[1..]=result).
 */
struct hwio_transport {
	void *ctx;
	/* Return 0 on delivery (mailbox filled), <0 errno on transport failure. */
	int (*submit)(void *ctx, const void *req96, uint64_t *mbox, size_t mbox_words);
	void (*close)(void *ctx);
	const char *name;
};

/* Open the layer. If dev_path is NULL, "/dev/mydev" is used. Never fails hard:
 * returns a handle whose families fall back or report HWIO_BE_NONE. Returns
 * NULL only on out-of-memory. */
hwio_t *hwio_open(const char *dev_path);

/* Open with an explicit transport (tests, or a custom device path already
 * opened). Takes ownership of t->ctx via t->close on hwio_close. */
hwio_t *hwio_open_transport(const struct hwio_transport *t);

void hwio_close(hwio_t *h);

/* True if the running kernel is in any lockdown mode (integrity/confidentiality). */
int hwio_is_locked_down(void);

/* Which backend serves a family on this handle. */
enum hwio_backend hwio_backend_for(hwio_t *h, enum hwio_family fam);
const char *hwio_backend_str(enum hwio_backend b);

/* ---- operations. All return 0 on success, <0 (negative errno) on failure. */

/* MSR on a logical CPU. */
int hwio_rdmsr(hwio_t *h, unsigned cpu, uint32_t reg, uint64_t *val);
int hwio_wrmsr(hwio_t *h, unsigned cpu, uint32_t reg, uint64_t val);

/* Physical-memory / MMIO. width is 1,2,4,8. */
int hwio_mem_read(hwio_t *h, uint64_t phys, int width, uint64_t *val);
int hwio_mem_write(hwio_t *h, uint64_t phys, int width, uint64_t val);

/* Port I/O. width is 1,2,4. */
int hwio_io_read(hwio_t *h, uint16_t port, int width, uint32_t *val);
int hwio_io_write(hwio_t *h, uint16_t port, int width, uint32_t val);

/* PCI domain 0, conventional 256-byte config space. width is 1,2,4; naturally
 * aligned offset, device 0..31, function 0..7. Invalid inputs return -EINVAL. */
int hwio_pci_read(hwio_t *h, uint8_t bus, uint8_t dev, uint8_t fn,
		  uint16_t off, int width, uint32_t *val);
int hwio_pci_write(hwio_t *h, uint8_t bus, uint8_t dev, uint8_t fn,
		   uint16_t off, int width, uint32_t val);

/* Embedded controller (ACPI EC index space). */
int hwio_ec_read(hwio_t *h, uint8_t index, uint8_t *val);
int hwio_ec_write(hwio_t *h, uint8_t index, uint8_t val);

/* CPU info. On the direct backend CPUID/TSC run on the calling CPU; callers
 * needing a specific CPU must pin and restore their thread's affinity. */
int hwio_cpuid(hwio_t *h, unsigned cpu, uint32_t leaf, uint32_t subleaf,
	       uint32_t out[4]);
int hwio_rdtsc(hwio_t *h, unsigned cpu, uint64_t *tsc);
int hwio_cpu_cores(hwio_t *h, unsigned *cores);

#ifdef __cplusplus
}
#endif
#endif /* OCTOOL_HWIO_H */
