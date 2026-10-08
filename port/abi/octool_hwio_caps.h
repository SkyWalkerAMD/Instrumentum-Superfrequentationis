/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Additive, read-only metadata query. The legacy 96-byte wire ABI is separate.
 * GET_CAPS_V1 has a fixed layout and meaning; incompatible changes require a
 * new ioctl number, not reinterpretation of these fields. x86-64 userspace. */
#ifndef OCTOOL_HWIO_CAPS_H
#define OCTOOL_HWIO_CAPS_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define OCTOOL_CAPS_MAGIC 0x4f435432U
#define OCTOOL_CAPS_VERSION 1U
#define OCTOOL_CAP_MSR  (1ULL << 0)
#define OCTOOL_CAP_MMIO (1ULL << 1)
#define OCTOOL_CAP_IO   (1ULL << 2)
#define OCTOOL_CAP_PCI  (1ULL << 3)
#define OCTOOL_CAP_EC   (1ULL << 4)
#define OCTOOL_CAP_CPU  (1ULL << 5)
#define OCTOOL_CAP_ALL  (OCTOOL_CAP_MSR | OCTOOL_CAP_MMIO | OCTOOL_CAP_IO | \
                        OCTOOL_CAP_PCI | OCTOOL_CAP_EC | OCTOOL_CAP_CPU)

struct octool_hwio_caps {
	__u32 magic;
	__u16 version;
	__u16 size;
	__u64 features;
	__u64 reserved[2]; /* zero in V1 */
};

#define OCTOOL_HWIO_GET_CAPS_V1 _IOR('O', 0x80, struct octool_hwio_caps)
#endif
