// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <asm/msr.h>
u64 octool_probe_tsc(void)
{
#ifdef PK_TSC_USE_RDTSCLL
	u64 value;
	rdtscll(value);
	return value;
#else
	return rdtsc();
#endif
}
