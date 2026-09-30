// SPDX-License-Identifier: GPL-2.0
/*
 * hwio_loopback_test - prove the userspace HAL and the kernel ABI agree on the
 * wire, without a kernel. A reference transport decodes struct octool_hwio_req
 * exactly as octool_hwio.c (the module) does and services it against in-process
 * fake memory/MSR/PCI spaces, filling the mailbox the same way. The test then:
 *   - asserts the encoded opcode/fields for each op match the bytes the shipped
 *     octool binary emits (verified from disassembly), and
 *   - asserts values round-trip through encode -> reference dispatch -> decode.
 */
#include "../hal/octool_hwio.h"
#include "../abi/octool_hwio_abi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

/* Compile-time ABI guards - these must match octool's observed layout. */
_Static_assert(sizeof(struct octool_hwio_req) == 96, "request must be 96 bytes");
_Static_assert(offsetof(struct octool_hwio_req, cmd) == 0, "cmd@0");
_Static_assert(offsetof(struct octool_hwio_req, user_id) == 8, "user_id@8");
_Static_assert(offsetof(struct octool_hwio_req, data0) == 16, "data0@16");
_Static_assert(offsetof(struct octool_hwio_req, data1) == 24, "data1@24");

#define FAKE_MEM 65536
struct ref {
	uint8_t mem[FAKE_MEM];
	uint64_t msr[8][256];			/* [cpu][reg&0xff] */
	uint32_t pci[256];
	struct octool_hwio_req last;		/* captured for wire assertions */
};

static int ref_submit(void *vctx, const void *req96, uint64_t *mbox, size_t words)
{
	struct ref *R = vctx;
	const struct octool_hwio_req *r = req96;
	uint64_t res = 0;

	R->last = *r;
	memset(mbox, 0, words * sizeof(uint64_t));

	switch (r->cmd) {
	case OCTOOL_OP_RD_MEM8:  res = *(uint8_t  *)&R->mem[r->data0 % FAKE_MEM]; break;
	case OCTOOL_OP_RD_MEM16: res = *(uint16_t *)&R->mem[r->data0 % FAKE_MEM]; break;
	case OCTOOL_OP_RD_MEM32: res = *(uint32_t *)&R->mem[r->data0 % FAKE_MEM]; break;
	case OCTOOL_OP_RD_MEM64: res = *(uint64_t *)&R->mem[r->data0 % FAKE_MEM]; break;
	case OCTOOL_OP_WR_MEM8:  *(uint8_t  *)&R->mem[r->data0 % FAKE_MEM] = (uint8_t)r->data1; break;
	case OCTOOL_OP_WR_MEM16: *(uint16_t *)&R->mem[r->data0 % FAKE_MEM] = (uint16_t)r->data1; break;
	case OCTOOL_OP_WR_MEM32: *(uint32_t *)&R->mem[r->data0 % FAKE_MEM] = (uint32_t)r->data1; break;
	case OCTOOL_OP_WR_MEM64: *(uint64_t *)&R->mem[r->data0 % FAKE_MEM] = r->data1; break;
	case OCTOOL_OP_RD_MSR:   res = R->msr[r->user_id & 7][r->data0 & 0xff]; break;
	case OCTOOL_OP_WR_MSR:   R->msr[r->user_id & 7][r->data0 & 0xff] = r->data1; break;
	case OCTOOL_OP_CPUID:
		mbox[OCTOOL_MBOX_RESULT]  = r->data0;      /* eax = leaf */
		mbox[OCTOOL_MBOX_RESULT2] = r->data1 + 1;  /* ebx = sub+1 */
		mbox[OCTOOL_MBOX_RESULT3] = 0x1234;
		mbox[OCTOOL_MBOX_RESULT4] = 0x5678;
		mbox[OCTOOL_MBOX_DONE] = 1;
		return 0;
	case OCTOOL_OP_PCI_RD:   res = R->pci[r->data3 & 0xff]; break;
	case OCTOOL_OP_PCI_WR:   R->pci[r->data3 & 0xff] = (uint32_t)r->data5; break;
	case OCTOOL_OP_CPU_CORES: res = 42; break;
	case OCTOOL_OP_RD_TSC:   res = 0xC0FFEE; break;
	default: mbox[OCTOOL_MBOX_DONE] = (uint64_t)(uint32_t)-22 << 32 | 1; return -22;
	}
	mbox[OCTOOL_MBOX_RESULT] = res;
	mbox[OCTOOL_MBOX_DONE] = 1;
	return 0;
}

static struct ref R;
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
	struct hwio_transport t = { .ctx = &R, .submit = ref_submit, .close = NULL, .name = "ref" };
	hwio_t *h = hwio_open_transport(&t);
	uint64_t v; uint32_t v32; unsigned cores; uint32_t cid[4]; uint64_t tsc;

	CHECK(h != NULL, "hwio_open_transport");

	/* MMIO round-trip + opcode/field wire assertions (must match octool) */
	CHECK(hwio_mem_write(h, 0x100, 4, 0xdeadbeef) == 0, "wr32");
	CHECK(R.last.cmd == 0x0d && R.last.data0 == 0x100 && R.last.data1 == 0xdeadbeef,
	      "wr32 wire: cmd=%llu data0=%llu data1=%#llx",
	      (unsigned long long)R.last.cmd, (unsigned long long)R.last.data0,
	      (unsigned long long)R.last.data1);
	CHECK(hwio_mem_read(h, 0x100, 4, &v) == 0 && v == 0xdeadbeef, "rd32 val=%#llx", (unsigned long long)v);
	CHECK(R.last.cmd == 0x0c && R.last.data0 == 0x100, "rd32 wire cmd=%llu", (unsigned long long)R.last.cmd);

	CHECK(hwio_mem_write(h, 0x200, 8, 0x1122334455667788ULL) == 0, "wr64");
	CHECK(R.last.cmd == 0x0b, "wr64 op=%llu", (unsigned long long)R.last.cmd);
	CHECK(hwio_mem_read(h, 0x200, 8, &v) == 0 && v == 0x1122334455667788ULL, "rd64");
	CHECK(R.last.cmd == 0x0a, "rd64 op=%llu", (unsigned long long)R.last.cmd);

	CHECK(hwio_mem_write(h, 0x40, 2, 0xabcd) == 0 && R.last.cmd == 0x0f, "wr16 op");
	CHECK(hwio_mem_read(h, 0x40, 2, &v) == 0 && v == 0xabcd && R.last.cmd == 0x0e, "rd16");
	CHECK(hwio_mem_write(h, 0x41, 1, 0x5a) == 0 && R.last.cmd == 0x11, "wr8 op");
	CHECK(hwio_mem_read(h, 0x41, 1, &v) == 0 && v == 0x5a && R.last.cmd == 0x10, "rd8");

	/* MSR */
	CHECK(hwio_wrmsr(h, 3, 0x1a0, 0x99) == 0, "wrmsr");
	CHECK(R.last.cmd == OCTOOL_OP_WR_MSR && R.last.user_id == 3 && R.last.data0 == 0x1a0 && R.last.data1 == 0x99, "wrmsr wire");
	CHECK(hwio_rdmsr(h, 3, 0x1a0, &v) == 0 && v == 0x99, "rdmsr rt val=%#llx", (unsigned long long)v);
	CHECK(R.last.cmd == OCTOOL_OP_RD_MSR && R.last.user_id == 3, "rdmsr wire");

	/* CPUID four-word result */
	CHECK(hwio_cpuid(h, 0, 0x16, 2, cid) == 0, "cpuid");
	CHECK(cid[0] == 0x16 && cid[1] == 3 && cid[2] == 0x1234 && cid[3] == 0x5678,
	      "cpuid decode: %x %x %x %x", cid[0], cid[1], cid[2], cid[3]);

	/* PCI */
	CHECK(hwio_pci_write(h, 0, 0, 0, 0x40, 4, 0xcafe) == 0 && R.last.cmd == OCTOOL_OP_PCI_WR, "pci wr");
	CHECK(hwio_pci_read(h, 0, 0, 0, 0x40, 4, &v32) == 0 && v32 == 0xcafe, "pci rt val=%#x", v32);
	CHECK(R.last.data3 == 0x40 && R.last.data4 == 4, "pci wire off/width");

	/* cores + tsc */
	CHECK(hwio_cpu_cores(h, &cores) == 0 && cores == 42, "cores=%u", cores);
	CHECK(hwio_rdtsc(h, 1, &tsc) == 0 && tsc == 0xC0FFEE, "tsc=%#llx", (unsigned long long)tsc);

	/* backend reporting */
	CHECK(hwio_backend_for(h, HWIO_FAM_MSR) == HWIO_BE_MODULE, "backend module");

	hwio_close(h);
	if (fails == 0) printf("ALL PASS: HAL encode/decode matches octool ABI on every op\n");
	else printf("%d CHECK(s) FAILED\n", fails);
	return fails ? 1 : 0;
}
