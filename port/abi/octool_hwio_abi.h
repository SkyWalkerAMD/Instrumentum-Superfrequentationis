/* SPDX-License-Identifier: GPL-2.0 */
/*
 * octool_hwio_abi.h - the wire contract between octool (userspace) and the
 * octool_hwio kernel module. Included by BOTH sides so there is one source of
 * truth. Safe to include from kernel C and from plain userspace C/C++.
 *
 * The 8 MMIO opcodes and the request/mailbox layout are fixed by the existing
 * shipped octool binary and MUST NOT change (verified by disassembly). The
 * non-MMIO opcodes are this project's own numbering.
 */
#ifndef OCTOOL_HWIO_ABI_H
#define OCTOOL_HWIO_ABI_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint64_t u64;
#endif

/* Default device node; octool opens /dev/mydev. */
#define OCTOOL_HWIO_DEV "mydev"

/*
 * Request: written to the device with a single write() of exactly
 * sizeof(struct octool_hwio_req) == 96 bytes. Field order is fixed.
 */
struct octool_hwio_req {
	u64 cmd;	/* OCTOOL_OP_* */
	u64 user_id;	/* per-open token from read(); CPU index for MSR/CPUID/TSC */
	u64 data0;	/* addr / port / reg / leaf / bus */
	u64 data1;	/* value / subleaf / dev */
	u64 data2;	/* fn */
	u64 data3;	/* off */
	u64 data4;	/* width */
	u64 data5;	/* value (PCI write) */
	u64 data6;
	u64 data7;
	u64 data8;
	u64 data9;
};

/*
 * Mailbox: one page shared via mmap(offset 0), as an array of u64.
 * slot[0] = done flag (0 -> 1). slot[1] = scalar result (byte offset 8).
 * CPUID returns four dwords in slot[1..4]. Both indices are fixed by the
 * shipped binary.
 */
#define OCTOOL_MBOX_DONE     0
#define OCTOOL_MBOX_RESULT   1
#define OCTOOL_MBOX_RESULT2  2
#define OCTOOL_MBOX_RESULT3  3
#define OCTOOL_MBOX_RESULT4  4

/* MMIO opcodes - bit-compatible with the shipped octool binary (do not change) */
#define OCTOOL_OP_RD_MEM64  0x0aULL
#define OCTOOL_OP_WR_MEM64  0x0bULL
#define OCTOOL_OP_RD_MEM32  0x0cULL
#define OCTOOL_OP_WR_MEM32  0x0dULL
#define OCTOOL_OP_RD_MEM16  0x0eULL
#define OCTOOL_OP_WR_MEM16  0x0fULL
#define OCTOOL_OP_RD_MEM8   0x10ULL
#define OCTOOL_OP_WR_MEM8   0x11ULL

/* Non-MMIO opcodes - this project's numbering (kept clear of 0x0a-0x11) */
#define OCTOOL_OP_RD_MSR    0x20ULL
#define OCTOOL_OP_WR_MSR    0x21ULL
#define OCTOOL_OP_RD_TSC    0x22ULL
#define OCTOOL_OP_CPUID     0x23ULL
#define OCTOOL_OP_IN_8      0x30ULL
#define OCTOOL_OP_IN_16     0x31ULL
#define OCTOOL_OP_IN_32     0x32ULL
#define OCTOOL_OP_OUT_8     0x33ULL
#define OCTOOL_OP_OUT_16    0x34ULL
#define OCTOOL_OP_OUT_32    0x35ULL
#define OCTOOL_OP_PCI_RD    0x40ULL
#define OCTOOL_OP_PCI_WR    0x41ULL
#define OCTOOL_OP_EC_RD     0x50ULL
#define OCTOOL_OP_EC_WR     0x51ULL
#define OCTOOL_OP_CPU_CORES 0x60ULL

#endif /* OCTOOL_HWIO_ABI_H */
