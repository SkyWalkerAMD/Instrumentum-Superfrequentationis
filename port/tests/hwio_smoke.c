// SPDX-License-Identifier: GPL-2.0
/*
 * hwio_smoke - on-target smoke test. Run as root on a real machine after
 * loading the module (modprobe octool_hwio). It exercises the real HAL against
 * the real device and cross-checks results that have a ground truth:
 *   - CPUID via the module vs. the native cpuid instruction (must match);
 *   - core count via the module vs. sysconf;
 *   - optional MSR read (default IA32_TIME_STAMP_COUNTER 0x10, which is safe to
 *     read) printed for eyeballing.
 * It prints which backend each family resolved to. This is the verification
 * that cannot be done off-hardware.
 */
#include "../hal/octool_hwio.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void native_cpuid(unsigned leaf, unsigned sub, unsigned o[4])
{
#if defined(__x86_64__) || defined(__i386__)
	unsigned a = leaf, b = 0, c = sub, d = 0;
	__asm__ volatile("cpuid" : "+a"(a), "+b"(b), "+c"(c), "+d"(d));
	o[0] = a; o[1] = b; o[2] = c; o[3] = d;
#else
	(void)leaf; (void)sub; memset(o, 0, 16);
#endif
}

int main(int argc, char **argv)
{
	hwio_t *h = hwio_open(NULL);
	int fails = 0;
	unsigned cid_m[4], cid_n[4], cores = 0;
	static const char *fam[] = { "MSR", "MMIO", "IO", "PCI", "EC", "CPU" };
	int i;

	if (!h) { fprintf(stderr, "hwio_open failed\n"); return 2; }

	printf("lockdown: %s\n", hwio_is_locked_down() ? "YES" : "no");
	for (i = 0; i < HWIO_FAM__COUNT; i++)
		printf("  %-4s -> %s\n", fam[i],
		       hwio_backend_str(hwio_backend_for(h, (enum hwio_family)i)));
	/* A direct-backend fallback is not evidence that the module works. */
	for (i = 0; i < HWIO_FAM__COUNT; i++) {
		if (hwio_backend_for(h, (enum hwio_family)i) != HWIO_BE_MODULE) {
			fprintf(stderr, "FAIL: %s is not served by the module\n", fam[i]);
			fails++;
		}
	}
	if (fails) { hwio_close(h); return 1; }

	/* CPUID leaf 0 must match native exactly when served by the module. */
	if (hwio_backend_for(h, HWIO_FAM_CPU) == HWIO_BE_MODULE) {
		if (hwio_cpuid(h, 0, 0, 0, cid_m) == 0) {
			native_cpuid(0, 0, cid_n);
			if (memcmp(cid_m, cid_n, sizeof(cid_m)) != 0) {
				printf("FAIL cpuid: module=%x-%x-%x-%x native=%x-%x-%x-%x\n",
				       cid_m[0],cid_m[1],cid_m[2],cid_m[3],
				       cid_n[0],cid_n[1],cid_n[2],cid_n[3]);
				fails++;
			} else {
				char v[13]; memcpy(v,&cid_m[1],4); memcpy(v+4,&cid_m[3],4);
				memcpy(v+8,&cid_m[2],4); v[12]=0;
				printf("cpuid via module OK (vendor '%s')\n", v);
			}
		} else { printf("FAIL cpuid call\n"); fails++; }
	}

	if (hwio_cpu_cores(h, &cores) == 0) {
		printf("cores: %u\n", cores);
		if ((long)cores != sysconf(_SC_NPROCESSORS_ONLN)) {
			printf("FAIL cores: differs from sysconf\n"); fails++;
		}
	} else { printf("FAIL cores\n"); fails++; }

	/* Optional MSR read: hwio_smoke [cpu] [reg-hex] */
	{
		unsigned cpu = argc > 1 ? (unsigned)strtoul(argv[1], 0, 0) : 0;
		unsigned reg = argc > 2 ? (unsigned)strtoul(argv[2], 0, 16) : 0x10;
		uint64_t v;
		int rc = hwio_rdmsr(h, cpu, reg, &v);
		if (rc == 0) printf("MSR[cpu%u] %#x = %#llx\n", cpu, reg, (unsigned long long)v);
		else printf("MSR read rc=%d (backend=%s)\n", rc,
			    hwio_backend_str(hwio_backend_for(h, HWIO_FAM_MSR)));
	}

	hwio_close(h);
	printf(fails ? "SMOKE FAILED (%d)\n" : "SMOKE OK\n", fails);
	return fails ? 1 : 0;
}
