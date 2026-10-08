/* SPDX-License-Identifier: GPL-2.0 */
/*
 * octool_parity_trace.h - on-disk format shared by the capture shim
 * (octool_capture.c) and the parity diff tool (octool_parity.c).
 *
 * A trace is a small fixed header followed by a flat array of records. Each
 * record is one request octool wrote to /dev/mydev together with the mailbox
 * contents observed after the module completed it. The parity tool never needs
 * to understand octool's internals - it just replays the recorded requests and
 * compares. The addresses therefore come entirely from octool at runtime; this
 * format hardcodes nothing about what any address means.
 */
#ifndef OCTOOL_PARITY_TRACE_H
#define OCTOOL_PARITY_TRACE_H

#include <stdint.h>

#define OCTOOL_TRACE_MAGIC   0x4f43545250520001ULL /* "OCTRPR" + version 1 */
#define OCTOOL_TRACE_MAGIC_V2 0x4f43545250520002ULL /* finalized, exact nrec */
#define OCTOOL_TRACE_MBOXW   5                      /* mailbox words captured */
#define OCTOOL_TRACE_REQSZ   96                     /* struct octool_hwio_req */

struct octool_trace_hdr {
	uint64_t magic;      /* v1, finalized v2, or 0 while capture incomplete */
	uint64_t reqsz;      /* == OCTOOL_TRACE_REQSZ */
	uint64_t mboxw;      /* == OCTOOL_TRACE_MBOXW */
	uint64_t nrec;       /* exact nonzero v2 count; v1 may stream with 0 */
};

struct octool_trace_rec {
	uint8_t  req[OCTOOL_TRACE_REQSZ];    /* exact bytes octool wrote */
	uint64_t mbox[OCTOOL_TRACE_MBOXW];   /* slot[0..4] after completion */
	uint64_t seq;                        /* per-fd sequence number */
	int32_t  wrote;                      /* return value of write() */
	int32_t  completed;                  /* 1 only if the full done word was 1 */
};

#endif /* OCTOOL_PARITY_TRACE_H */
