// SPDX-License-Identifier: GPL-2.0
/*
 * octool_parity.c - MMIO parity checker for the octool module rewrite.
 *
 * Question it answers
 * -------------------
 * "Does the new octool_hwio module return, on real hardware, exactly what the
 *  original .ko returns for every MMIO address the real octool binary reads?"
 *
 * It answers that WITHOUT modifying octool. The address corpus comes from a
 * capture of a normal octool session (see octool_capture.c); this tool only
 * replays those addresses and compares two module implementations. Reads only
 * by default - it never writes hardware.
 *
 * Modes
 * -----
 *  live  (--old DEV --new DEV): both modules loaded at once (the new one under
 *        a second name, e.g. `modprobe octool_hwio devname=mydev_v2`). For each
 *        recorded read the tool does three back-to-back reads - old, new, old -
 *        so hardware that legitimately changes between reads (sensors, counters)
 *        is recognised as volatile instead of being flagged. A *stable* address
 *        where old != new is a real parity failure. This mode gives a verdict.
 *
 *  offline (--new DEV only): compares live reads from the new module against the
 *        values captured from the old module earlier. Weaker - the time gap
 *        widens the volatile set and a value octool itself changed later can look
 *        like a diff - so results are advisory. Prefer live mode for a verdict.
 *
 *  --selftest: no hardware. Runs the diff engine over in-process reference
 *        transports to prove it classifies MATCH / MISMATCH / VOLATILE
 *        correctly. This validates the harness itself.
 *
 * Exit: 0 if no stable mismatch (and, in selftest, all expectations met);
 *       1 on any stable MISMATCH; 2 on usage/IO error.
 *
 * Build: cc -O2 -Wall -Wextra -o octool_parity octool_parity.c ../hal/octool_hwio.c
 */
#include "../hal/octool_hwio.h"
#include "../abi/octool_hwio_abi.h"
#include "octool_parity_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

/* ---- read a trace into memory ------------------------------------------- */
struct rd_item {
	uint64_t phys;
	int      width;
	uint64_t cap_result;   /* old module's captured value (offline mode) */
	int      cap_completed;
};

static int op_read_width(uint64_t cmd)
{
	switch (cmd) {
	case OCTOOL_OP_RD_MEM8:  return 1;
	case OCTOOL_OP_RD_MEM16: return 2;
	case OCTOOL_OP_RD_MEM32: return 4;
	case OCTOOL_OP_RD_MEM64: return 8;
	default: return 0;
	}
}

static const char *op_name(uint64_t cmd)
{
	switch (cmd) {
	case OCTOOL_OP_RD_MEM8: case OCTOOL_OP_RD_MEM16:
	case OCTOOL_OP_RD_MEM32: case OCTOOL_OP_RD_MEM64: return "RD_MEM";
	case OCTOOL_OP_WR_MEM8: case OCTOOL_OP_WR_MEM16:
	case OCTOOL_OP_WR_MEM32: case OCTOOL_OP_WR_MEM64: return "WR_MEM";
	default: return "other";
	}
}

struct corpus {
	struct rd_item *items;
	size_t n, cap;
	size_t skipped_write;
	size_t skipped_other;
	size_t dup;
};

static void corpus_add(struct corpus *c, uint64_t phys, int width,
		       uint64_t cap_result, int cap_completed)
{
	size_t i;

	/* de-dup identical (phys,width): the corpus is a *set* of addresses to
	 * probe; octool reads many hot registers thousands of times. */
	for (i = 0; i < c->n; i++)
		if (c->items[i].phys == phys && c->items[i].width == width) {
			c->dup++;
			return;
		}
	if (c->n == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 256;
		c->items = realloc(c->items, c->cap * sizeof(*c->items));
		if (!c->items) { perror("realloc"); exit(2); }
	}
	c->items[c->n].phys = phys;
	c->items[c->n].width = width;
	c->items[c->n].cap_result = cap_result;
	c->items[c->n].cap_completed = cap_completed;
	c->n++;
}

static int load_trace(const char *path, struct corpus *c)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	struct octool_trace_hdr h;
	struct octool_trace_rec rec;
	ssize_t n;

	if (fd < 0) { fprintf(stderr, "open %s: %s\n", path, strerror(errno)); return -1; }
	if (read(fd, &h, sizeof(h)) != (ssize_t)sizeof(h) ||
	    h.magic != OCTOOL_TRACE_MAGIC ||
	    h.reqsz != OCTOOL_TRACE_REQSZ || h.mboxw != OCTOOL_TRACE_MBOXW) {
		fprintf(stderr, "%s: not a valid octool trace\n", path);
		close(fd);
		return -1;
	}
	while ((n = read(fd, &rec, sizeof(rec))) == (ssize_t)sizeof(rec)) {
		struct octool_hwio_req r;
		int w;

		memcpy(&r, rec.req, sizeof(r));
		w = op_read_width(r.cmd);
		if (w) {
			corpus_add(c, r.data0, w,
				   rec.mbox[OCTOOL_MBOX_RESULT], rec.completed);
		} else if (!strcmp(op_name(r.cmd), "WR_MEM")) {
			c->skipped_write++;
		} else {
			c->skipped_other++;
		}
	}
	close(fd);
	if (n != 0) {
		fprintf(stderr, "%s: truncated trace record or read error\n", path);
		return -1;
	}
	return 0;
}

/* ---- classification ----------------------------------------------------- */
enum verdict { V_MATCH, V_MISMATCH, V_VOLATILE_OK, V_VOLATILE_OOB,
	       V_UNREADABLE, V_ERR_PARITY, V_DIFF_ADVISORY };

static const char *verdict_str(enum verdict v)
{
	switch (v) {
	case V_MATCH:         return "match";
	case V_MISMATCH:      return "MISMATCH";
	case V_VOLATILE_OK:   return "volatile";
	case V_VOLATILE_OOB:  return "volatile-oob";
	case V_UNREADABLE:    return "unreadable(both)";
	case V_ERR_PARITY:    return "ERR-PARITY";
	case V_DIFF_ADVISORY: return "diff?";
	}
	return "?";
}

/* live: old, new, old back-to-back */
static enum verdict classify_live(hwio_t *o, hwio_t *nw, uint64_t phys, int w,
				  uint64_t *vo, uint64_t *vn)
{
	uint64_t o1 = 0, o2 = 0, nv = 0;
	int ro1, rn, ro2;

	ro1 = hwio_mem_read(o,  phys, w, &o1);
	rn  = hwio_mem_read(nw, phys, w, &nv);
	ro2 = hwio_mem_read(o,  phys, w, &o2);
	*vo = o1; *vn = nv;

	if ((ro1 == 0) != (rn == 0))
		return V_ERR_PARITY;
	if (ro1 != 0 && rn != 0)
		return V_UNREADABLE;          /* both refuse - not a divergence */
	if (ro2 != 0)
		return V_VOLATILE_OK;         /* old read wobbled - no clean compare */
	if (o1 == o2)
		return (nv == o1) ? V_MATCH : V_MISMATCH;
	/* volatile: new must land within the band the old module spans */
	{
		uint64_t lo = o1 < o2 ? o1 : o2, hi = o1 < o2 ? o2 : o1;
		return (nv >= lo && nv <= hi) ? V_VOLATILE_OK : V_VOLATILE_OOB;
	}
}

/* offline: new live vs captured old value */
static enum verdict classify_offline(hwio_t *nw, const struct rd_item *it,
				     uint64_t *vn)
{
	uint64_t n1 = 0, n2 = 0;
	int r1 = hwio_mem_read(nw, it->phys, it->width, &n1);
	int r2 = hwio_mem_read(nw, it->phys, it->width, &n2);

	*vn = n1;
	if ((r1 == 0) != (it->cap_completed ? 1 : 0))
		return V_ERR_PARITY;
	if (r1 != 0 || r2 != 0)
		return V_UNREADABLE;
	if (n1 != n2)
		return V_VOLATILE_OK;         /* changing now - can't compare */
	return (n1 == it->cap_result) ? V_MATCH : V_DIFF_ADVISORY;
}

struct tally { size_t v[7]; };

static int live_exit_code(const struct tally *t)
{
	if (t->v[V_MISMATCH] || t->v[V_ERR_PARITY]) return 1;
	return t->v[V_MATCH] ? 0 : 2;
}

static void run(struct corpus *c, hwio_t *o, hwio_t *nw, int offline,
		int max_show, struct tally *t)
{
	size_t i, shown = 0;

	for (i = 0; i < c->n; i++) {
		struct rd_item *it = &c->items[i];
		uint64_t vo = 0, vn = 0;
		enum verdict v;

		if (offline)
			v = classify_offline(nw, it, &vn);
		else
			v = classify_live(o, nw, it->phys, it->width, &vo, &vn);
		t->v[v]++;

		if ((v == V_MISMATCH || v == V_ERR_PARITY || v == V_VOLATILE_OOB ||
		     v == V_DIFF_ADVISORY) && shown < (size_t)max_show) {
			if (offline)
				printf("  %-12s phys=%#018llx w=%d  new=%#llx cap_old=%#llx\n",
				       verdict_str(v), (unsigned long long)it->phys, it->width,
				       (unsigned long long)vn, (unsigned long long)it->cap_result);
			else
				printf("  %-12s phys=%#018llx w=%d  old=%#llx new=%#llx\n",
				       verdict_str(v), (unsigned long long)it->phys, it->width,
				       (unsigned long long)vo, (unsigned long long)vn);
			shown++;
		}
	}
}

/* ======================================================================== */
/* selftest: in-process reference transports, no hardware                    */
/* ======================================================================== */
#define ST_MEM 4096
#define ST_VOL_ADDR 0x800          /* an address the refs treat as volatile */
#define ST_MISMATCH_ADDR 0x40      /* differs between old and new (stable)   */

struct stref { uint8_t mem[ST_MEM]; int is_new; };
static unsigned st_vol_counter;    /* shared, so interleaved reads advance   */

static int st_submit(void *vctx, const void *req96, uint64_t *mbox, size_t words)
{
	struct stref *s = vctx;
	const struct octool_hwio_req *r = req96;
	uint64_t res = 0;
	int w = op_read_width(r->cmd);

	memset(mbox, 0, words * sizeof(uint64_t));
	if (w) {
		uint64_t a = r->data0 % ST_MEM;
		if (a == ST_VOL_ADDR) {
			res = st_vol_counter++;               /* changes every read */
		} else {
			memcpy(&res, &s->mem[a], (size_t)w);
			/* new ref diverges at one stable address */
			if (s->is_new && a == ST_MISMATCH_ADDR)
				res ^= 0xff;
		}
		mbox[OCTOOL_MBOX_RESULT] = res;
		mbox[OCTOOL_MBOX_DONE] = 1;
		return 0;
	}
	mbox[OCTOOL_MBOX_DONE] = (uint64_t)(uint32_t)-22 << 32 | 1;
	return -22;
}

static hwio_t *st_open(struct stref *s, int is_new)
{
	struct hwio_transport t = { .ctx = s, .submit = st_submit, .close = NULL,
				    .name = is_new ? "st-new" : "st-old" };
	memset(s->mem, 0, sizeof(s->mem));
	for (size_t i = 0; i < ST_MEM; i++)
		s->mem[i] = (uint8_t)(i * 7 + 3);   /* identical content both sides */
	s->is_new = is_new;
	return hwio_open_transport(&t);
}

/* write one request+mailbox record into a trace, as the capture shim would */
static void st_emit(int fd, uint64_t cmd, uint64_t data0, uint64_t result)
{
	struct octool_hwio_req r;
	struct octool_trace_rec rec;

	memset(&r, 0, sizeof(r));
	r.cmd = cmd; r.data0 = data0;
	memset(&rec, 0, sizeof(rec));
	memcpy(rec.req, &r, sizeof(r));
	rec.mbox[OCTOOL_MBOX_RESULT] = result;
	rec.mbox[OCTOOL_MBOX_DONE] = 1;
	rec.completed = 1;
	rec.wrote = OCTOOL_TRACE_REQSZ;
	if (write(fd, &rec, sizeof(rec)) != (ssize_t)sizeof(rec)) { perror("write"); exit(2); }
}

/* prove the on-disk trace format round-trips between writer and reader */
static int selftest_trace(void)
{
	char tmpl[] = "/tmp/octool_trace_XXXXXX";
	int fd = mkstemp(tmpl);
	struct octool_trace_hdr h = { OCTOOL_TRACE_MAGIC, OCTOOL_TRACE_REQSZ,
				      OCTOOL_TRACE_MBOXW, 0 };
	struct corpus c = {0};
	int rc = 0;

	if (fd < 0) { perror("mkstemp"); return 1; }
	if (write(fd, &h, sizeof(h)) != (ssize_t)sizeof(h)) { perror("write"); return 1; }
	st_emit(fd, OCTOOL_OP_RD_MEM32, 0x1000, 0xaaaa);  /* read A          */
	st_emit(fd, OCTOOL_OP_RD_MEM32, 0x1000, 0xaaaa);  /* read A again -> dup */
	st_emit(fd, OCTOOL_OP_RD_MEM8,  0x2000, 0x5a);    /* read B          */
	st_emit(fd, OCTOOL_OP_WR_MEM32, 0x3000, 0);       /* write -> skipped */
	st_emit(fd, OCTOOL_OP_CPUID,    0x0,    0);       /* other -> skipped */
	close(fd);

	if (load_trace(tmpl, &c) != 0) { unlink(tmpl); return 1; }
	printf("selftest-trace: unique=%zu dup=%zu writes=%zu other=%zu\n",
	       c.n, c.dup, c.skipped_write, c.skipped_other);
	if (c.n != 2)            { printf("FAIL: expected 2 unique addresses\n"); rc = 1; }
	if (c.dup != 1)          { printf("FAIL: expected 1 dup\n"); rc = 1; }
	if (c.skipped_write != 1){ printf("FAIL: expected 1 write skipped\n"); rc = 1; }
	if (c.skipped_other != 1){ printf("FAIL: expected 1 other skipped\n"); rc = 1; }

	free(c.items);
	unlink(tmpl);
	if (!rc) printf("selftest-trace OK: capture/parity trace format round-trips\n");
	return rc;
}

static int selftest(void)
{
	static struct stref so, sn;
	hwio_t *o = st_open(&so, 0);
	hwio_t *nw = st_open(&sn, 1);
	struct corpus c = {0};
	struct tally t = {{0}};
	int rc = 0;

	rc |= selftest_trace();

	/* build a synthetic corpus: a matching addr, the mismatch addr, the
	 * volatile addr. */
	corpus_add(&c, 0x10, 4, 0, 1);
	corpus_add(&c, ST_MISMATCH_ADDR, 4, 0, 1);
	corpus_add(&c, ST_VOL_ADDR, 4, 0, 1);

	printf("selftest: corpus=%zu (live diff engine over reference transports)\n", c.n);
	run(&c, o, nw, 0, 16, &t);

	printf("  match=%zu mismatch=%zu volatile=%zu vol-oob=%zu\n",
	       t.v[V_MATCH], t.v[V_MISMATCH], t.v[V_VOLATILE_OK], t.v[V_VOLATILE_OOB]);

	if (t.v[V_MATCH] != 1)    { printf("FAIL: expected 1 match\n"); rc = 1; }
	if (t.v[V_MISMATCH] != 1) { printf("FAIL: expected 1 mismatch\n"); rc = 1; }
	if (t.v[V_VOLATILE_OK] != 1) { printf("FAIL: expected 1 volatile\n"); rc = 1; }
	{
		struct tally empty = {{0}}, stable = {{0}}, unreadable = {{0}};
		stable.v[V_MATCH] = 1;
		unreadable.v[V_UNREADABLE] = 1;
		if (live_exit_code(&t) != 1 || live_exit_code(&empty) != 2 ||
		    live_exit_code(&stable) != 0 || live_exit_code(&unreadable) != 2) {
			printf("FAIL: live acceptance exit-code gate\n"); rc = 1;
		}
	}

	free(c.items);
	hwio_close(o);
	hwio_close(nw);
	printf(rc ? "SELFTEST FAILED\n" : "SELFTEST OK: engine classifies match/mismatch/volatile correctly\n");
	return rc;
}

/* ======================================================================== */
static void usage(const char *a0)
{
	fprintf(stderr,
	 "usage:\n"
	 "  %s --selftest\n"
	 "  %s --old /dev/mydev --new /dev/mydev_v2 --trace corpus.bin [--max-show N]\n"
	 "  %s --new /dev/mydev  --trace corpus.bin            (offline, advisory)\n",
	 a0, a0, a0);
}

int main(int argc, char **argv)
{
	const char *old_dev = NULL, *new_dev = NULL, *trace = NULL;
	int do_selftest = 0, max_show = 20, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--selftest")) do_selftest = 1;
		else if (!strcmp(argv[i], "--old") && i+1 < argc) old_dev = argv[++i];
		else if (!strcmp(argv[i], "--new") && i+1 < argc) new_dev = argv[++i];
		else if (!strcmp(argv[i], "--trace") && i+1 < argc) trace = argv[++i];
		else if (!strcmp(argv[i], "--max-show") && i+1 < argc) max_show = atoi(argv[++i]);
		else { usage(argv[0]); return 2; }
	}

	if (do_selftest)
		return selftest();

	if (!new_dev || !trace) { usage(argv[0]); return 2; }

	struct corpus c = {0};
	if (load_trace(trace, &c) != 0)
		return 2;
	printf("corpus: %zu unique MMIO-read addresses (%zu dup reads, "
	       "%zu writes skipped, %zu other skipped)\n",
	       c.n, c.dup, c.skipped_write, c.skipped_other);
	if (c.n == 0) { printf("INCONCLUSIVE: no MMIO reads in trace\n"); free(c.items); return 2; }

	int offline = (old_dev == NULL);
	hwio_t *o = NULL, *nw;

	if (!offline) {
		o = hwio_open(old_dev);
		if (!o || hwio_backend_for(o, HWIO_FAM_MMIO) != HWIO_BE_MODULE) {
			fprintf(stderr, "old: %s not served by module (loaded? permitted?)\n", old_dev);
			return 2;
		}
	}
	nw = hwio_open(new_dev);
	if (!nw || hwio_backend_for(nw, HWIO_FAM_MMIO) != HWIO_BE_MODULE) {
		fprintf(stderr, "new: %s not served by module (loaded? permitted?)\n", new_dev);
		return 2;
	}

	printf("mode: %s\n", offline ? "OFFLINE (advisory: new vs captured old)"
				     : "LIVE (old vs new, back-to-back)");
	struct tally t = {{0}};
	run(&c, o, nw, offline, max_show, &t);

	printf("\nresult over %zu addresses:\n", c.n);
	printf("  match            %zu\n", t.v[V_MATCH]);
	printf("  MISMATCH(stable) %zu\n", t.v[V_MISMATCH]);
	printf("  volatile         %zu\n", t.v[V_VOLATILE_OK]);
	printf("  volatile-oob     %zu\n", t.v[V_VOLATILE_OOB]);
	printf("  err-parity       %zu\n", t.v[V_ERR_PARITY]);
	printf("  unreadable(both) %zu\n", t.v[V_UNREADABLE]);
	if (offline)
		printf("  diff?(advisory)  %zu\n", t.v[V_DIFF_ADVISORY]);

	free(c.items);
	if (o) hwio_close(o);
	hwio_close(nw);

	int hard_fail = t.v[V_MISMATCH] + t.v[V_ERR_PARITY];
	if (!offline) {
		if (live_exit_code(&t) == 2) {
			printf("\nPARITY INCONCLUSIVE: no stable readable address\n");
			return 2;
		}
		printf(hard_fail ? "\nPARITY FAILED: %d stable divergence(s)\n"
				 : "\nPARITY OK: new module matches old on every stable MMIO read\n",
		       hard_fail);
		return live_exit_code(&t);
	}
	printf("\nOFFLINE done (advisory). Run live (--old/--new) for a verdict.\n");
	return 0;
}
