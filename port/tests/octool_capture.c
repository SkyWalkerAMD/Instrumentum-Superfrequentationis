// SPDX-License-Identifier: GPL-2.0
/*
 * octool_capture.c - LD_PRELOAD observation shim for parity testing.
 *
 * Purpose
 * -------
 * Record the exact request stream the *unmodified* octool binary sends to its
 * character device (/dev/mydev), so the parity tool can later replay those same
 * requests against a candidate module and confirm identical behaviour. This is
 * a read-only observer: it never changes what octool sends, never issues its own
 * hardware access, and only ever *reads* the mailbox page octool already shares
 * with the module. octool runs exactly as it always does; we just take notes.
 *
 * How it hooks
 * ------------
 *   open/openat  - note the fd when its path is the target device.
 *   mmap         - note the mailbox page for a tracked fd (offset 0).
 *   write        - for a 96-byte write to a tracked fd: call the real write,
 *                  then wait for the module's done flag exactly as octool will
 *                  (the original driver may complete from a kthread, so the
 *                  mailbox is not guaranteed filled the instant write returns),
 *                  snapshot request + mailbox, append one trace record.
 *   close        - forget the fd.
 *
 * Usage
 * -----
 *   OCTOOL_CAP_OUT=corpus.bin \
 *   LD_PRELOAD=./octool_capture.so ./octool        # drive octool normally
 *
 * Env:
 *   OCTOOL_CAP_DEV  device path to watch (default /dev/mydev)
 *   OCTOOL_CAP_OUT  trace output file    (default octool_trace.bin)
 *
 * Build: cc -O2 -fPIC -shared -o octool_capture.so octool_capture.c -ldl -lpthread
 */
#define _GNU_SOURCE
#include "octool_parity_trace.h"
#include "../abi/octool_hwio_abi.h"

#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <stdatomic.h>

#define MAXFD 8192

struct fdslot {
	int                 is_dev;   /* fd refers to the watched device */
	volatile uint64_t  *mbox;     /* mailbox page mmap'd for this fd, or NULL */
	_Atomic uint64_t    seq;      /* per-fd request counter */
};

static struct fdslot g_fd[MAXFD];
static const char   *g_dev = "/dev/mydev";
static int           g_out_fd = -1;
static pthread_mutex_t g_out_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static __thread int  g_in_hook;               /* reentrancy guard */

/* real libc entry points */
static int   (*real_open)(const char *, int, ...);
static int   (*real_open64)(const char *, int, ...);
static int   (*real_openat)(int, const char *, int, ...);
static int   (*real_openat64)(int, const char *, int, ...);
static ssize_t (*real_write)(int, const void *, size_t);
static int   (*real_close)(int);
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);
static void *(*real_mmap64)(void *, size_t, int, int, int, off_t);

static void resolve(void)
{
	real_open     = dlsym(RTLD_NEXT, "open");
	real_open64   = dlsym(RTLD_NEXT, "open64");
	real_openat   = dlsym(RTLD_NEXT, "openat");
	real_openat64 = dlsym(RTLD_NEXT, "openat64");
	real_write    = dlsym(RTLD_NEXT, "write");
	real_close    = dlsym(RTLD_NEXT, "close");
	real_mmap     = dlsym(RTLD_NEXT, "mmap");
	real_mmap64   = dlsym(RTLD_NEXT, "mmap64");

	const char *d = getenv("OCTOOL_CAP_DEV");
	if (d && *d)
		g_dev = d;
	const char *o = getenv("OCTOOL_CAP_OUT");
	if (!o || !*o)
		o = "octool_trace.bin";

	g_out_fd = real_open(o, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (g_out_fd >= 0) {
		struct octool_trace_hdr h = {
			.magic = OCTOOL_TRACE_MAGIC,
			.reqsz = OCTOOL_TRACE_REQSZ,
			.mboxw = OCTOOL_TRACE_MBOXW,
			.nrec  = 0, /* streamed; parity tool counts records */
		};
		(void)!real_write(g_out_fd, &h, sizeof(h));
	}
	fprintf(stderr, "[octool_capture] watching %s -> %s\n", g_dev, o);
}

static void ensure(void) { pthread_once(&g_once, resolve); }

static int path_is_dev(const char *path)
{
	return path && strcmp(path, g_dev) == 0;
}

static void mark_dev(int fd)
{
	if (fd >= 0 && fd < MAXFD) {
		g_fd[fd].is_dev = 1;
		g_fd[fd].mbox = NULL;
		atomic_store(&g_fd[fd].seq, 0);
	}
}

/* ---- open family -------------------------------------------------------- */
int open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	int fd;

	ensure();
	if (flags & O_CREAT) {
		va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
	}
	fd = real_open(path, flags, mode);
	if (!g_in_hook && path_is_dev(path))
		mark_dev(fd);
	return fd;
}

int open64(const char *path, int flags, ...)
{
	mode_t mode = 0;
	int fd;

	ensure();
	if (flags & O_CREAT) {
		va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
	}
	fd = real_open64 ? real_open64(path, flags, mode) : real_open(path, flags, mode);
	if (!g_in_hook && path_is_dev(path))
		mark_dev(fd);
	return fd;
}

int openat(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	int fd;

	ensure();
	if (flags & O_CREAT) {
		va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
	}
	fd = real_openat(dirfd, path, flags, mode);
	if (!g_in_hook && path_is_dev(path))
		mark_dev(fd);
	return fd;
}

int openat64(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	int fd;

	ensure();
	if (flags & O_CREAT) {
		va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
	}
	fd = real_openat64 ? real_openat64(dirfd, path, flags, mode)
			   : real_openat(dirfd, path, flags, mode);
	if (!g_in_hook && path_is_dev(path))
		mark_dev(fd);
	return fd;
}

/* ---- mmap: capture the mailbox page for a tracked fd --------------------- */
static void note_mmap(int fd, off_t off, void *ret)
{
	if (ret != MAP_FAILED && off == 0 && fd >= 0 && fd < MAXFD && g_fd[fd].is_dev)
		g_fd[fd].mbox = (volatile uint64_t *)ret;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	void *r;

	ensure();
	r = real_mmap(addr, len, prot, flags, fd, off);
	if (!g_in_hook)
		note_mmap(fd, off, r);
	return r;
}

void *mmap64(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	void *r;

	ensure();
	r = real_mmap64 ? real_mmap64(addr, len, prot, flags, fd, off)
			: real_mmap(addr, len, prot, flags, fd, off);
	if (!g_in_hook)
		note_mmap(fd, off, r);
	return r;
}

/* ---- write: the actual capture point ------------------------------------ */
static void record(int fd, const void *req, ssize_t wrote)
{
	struct octool_trace_rec rec;
	volatile uint64_t *mbox = g_fd[fd].mbox;
	int completed = 0;
	long spins = 0;
	int i;

	memset(&rec, 0, sizeof(rec));
	memcpy(rec.req, req, OCTOOL_TRACE_REQSZ);
	rec.seq = atomic_fetch_add(&g_fd[fd].seq, 1);
	rec.wrote = (int32_t)wrote;

	/* Mirror octool's own completion wait: spin on the done flag. The original
	 * driver may fill the mailbox from a kthread after write() returns, so we
	 * must wait exactly as octool does before snapshotting. */
	if (mbox) {
		while (mbox[OCTOOL_MBOX_DONE] == 0) {
			if (++spins > 200000000L)
				break;               /* give up; record completed=0 */
			if ((spins & 0xffff) == 0)
				sched_yield();
		}
		completed = mbox[OCTOOL_MBOX_DONE] != 0;
		for (i = 0; i < OCTOOL_TRACE_MBOXW; i++)
			rec.mbox[i] = mbox[i];
	}
	rec.completed = completed;

	pthread_mutex_lock(&g_out_lock);
	if (g_out_fd >= 0)
		(void)!real_write(g_out_fd, &rec, sizeof(rec));
	pthread_mutex_unlock(&g_out_lock);
}

ssize_t write(int fd, const void *buf, size_t count)
{
	ssize_t r;

	ensure();
	r = real_write(fd, buf, count);
	if (!g_in_hook && r == (ssize_t)OCTOOL_TRACE_REQSZ &&
	    count == OCTOOL_TRACE_REQSZ &&
	    fd >= 0 && fd < MAXFD && g_fd[fd].is_dev) {
		g_in_hook = 1;
		record(fd, buf, r);
		g_in_hook = 0;
	}
	return r;
}

int close(int fd)
{
	ensure();
	if (!g_in_hook && fd >= 0 && fd < MAXFD && g_fd[fd].is_dev) {
		g_fd[fd].is_dev = 0;
		g_fd[fd].mbox = NULL;
	}
	return real_close(fd);
}
