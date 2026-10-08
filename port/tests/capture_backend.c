// SPDX-License-Identifier: GPL-2.0
/* Synthetic downstream write. Ordinary mmap file, no device/hardware I/O. */
#define _GNU_SOURCE
#include "capture_test.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static int mode(const char *s) { return capture_test.mode && !strcmp(capture_test.mode, s); }
static void *delayed(void *unused)
{
    const struct timespec nap = {0, 20000000};
    (void)unused;
    nanosleep(&nap, NULL);
    __atomic_store_n(capture_test.mailbox, 1, __ATOMIC_RELEASE);
    return NULL;
}
static int target(int fd)
{
    struct stat a, b;
    const char *p = getenv("OCTOOL_CAP_DEV");
    return p && !fstat(fd, &a) && !stat(p, &b) &&
           a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
ssize_t write(int fd, const void *buf, size_t count)
{
    if (!target(fd)) {
        if (mode("trace-fail") && count == 152) { errno = ENOSPC; return -1; }
        if (mode("trace-partial-fail")) {
            if (!capture_test.trace_stage++ && count == 152)
                return syscall(SYS_write, fd, buf, 7);
            errno = EIO; return -1;
        }
        if (mode("trace-short") && !capture_test.trace_stage++ && count == 152)
            return syscall(SYS_write, fd, buf, 7);
        if (mode("trace-eintr") && !capture_test.trace_stage++) { errno = EINTR; return -1; }
        return syscall(SYS_write, fd, buf, count);
    }
    atomic_fetch_add(&capture_test.calls, 1);
    if (mode("bad-pointer")) { errno = EFAULT; return -1; }
    if (count != 96) { errno = EINVAL; return -1; }
    memcpy(capture_test.wire, buf, 96);
    if (mode("overlap") || mode("unmap-during") || mode("close-during")) {
        if (!atomic_exchange(&capture_test.entered, 1))
            while (!atomic_load(&capture_test.release)) {
                const struct timespec nap = {0, 1000000}; nanosleep(&nap, NULL);
            }
    }
    if (mode("write-fail")) { errno = EIO; return -1; }
    if (mode("short-write")) { errno = ENOTTY; return 12; }
    if (!mode("timeout")) {
        capture_test.mailbox[1] = UINT64_C(0x123456789abcdef0);
        capture_test.mailbox[2] = 2; capture_test.mailbox[3] = 3; capture_test.mailbox[4] = 4;
        __atomic_store_n(capture_test.mailbox,
            mode("driver-error") ? UINT64_C(0xfffffff400000001) : mode("bad-done") ? 2 : 1,
            __ATOMIC_RELEASE);
    }
    if (mode("delayed")) {
        pthread_t t;
        __atomic_store_n(capture_test.mailbox, 0, __ATOMIC_RELEASE);
        if (pthread_create(&t, NULL, delayed, NULL) || pthread_detach(t)) abort();
    }
    if (mode("mutate-request")) ((uint64_t *)buf)[2] = UINT64_C(0xdeadbeef);
    errno = EUCLEAN; /* Observer must preserve downstream errno even on success. */
    return 96;
}
ssize_t pwrite(int fd, const void *buf, size_t count, off_t off)
{
    if (mode("finalize-fail")) { errno = ENOSPC; return -1; }
    if (mode("marker-partial") && off < 8) {
        if (!capture_test.trace_stage++ && count == 8)
            return syscall(SYS_pwrite64, fd, buf, 4, off);
        if (count != 32) { errno = EIO; return -1; }
        capture_test.trace_stage = 0;
    }
    return syscall(SYS_pwrite64, fd, buf, count, off);
}
int fdatasync(int fd)
{
    if (mode("sync-fail")) { errno = EIO; return -1; }
    return (int)syscall(SYS_fdatasync, fd);
}
