// SPDX-License-Identifier: GPL-2.0
/* LD_PRELOAD observer. Never retries a device write or changes request/mailbox.
 * v2 traces commit on normal exit; lost/ambiguous observations invalidate the
 * whole session. Supported boundaries: docs/capture-integrity.md. */
#define _GNU_SOURCE
#include "octool_parity_trace.h"
#include "../abi/octool_hwio_abi.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#define MAXFD 8192
#define MBOX_BYTES (OCTOOL_TRACE_MBOXW * sizeof(uint64_t))
struct fdslot { int watched; uint64_t cookie, seq; const uint64_t *mbox; };
static struct fdslot slots[MAXFD];
/* Metadata/snapshot/VMA lock, NEVER held over the device write or sleep. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static __thread int inside;
static const char *device = "/dev/mydev";
static int output = -1, initialized, invalid, active, finalized;
static uint64_t next_cookie, records;
static int (*next_open)(const char *, int, ...);
static int (*next_open64)(const char *, int, ...);
static int (*next_openat)(int, const char *, int, ...);
static int (*next_openat64)(int, const char *, int, ...);
static int (*next_close)(int);
static int (*next_dup)(int);
static int (*next_dup2)(int, int);
static int (*next_dup3)(int, int, int);
static ssize_t (*next_write)(int, const void *, size_t);
static void *(*next_mmap)(void *, size_t, int, int, int, off_t);
static void *(*next_mmap64)(void *, size_t, int, int, int, off64_t);
static int (*next_munmap)(void *, size_t);
static int (*next_mprotect)(void *, size_t, int);
static void *(*next_mremap)(void *, size_t, size_t, int, ...);
static int positional(const void *, size_t, off_t);

/* Caller holds lock (or initializes). Diagnostic cannot recurse into write. */
static void reject(const char *reason)
{
    if (finalized && output >= 0) {
        const uint64_t zero = 0;
        /* A later DSO destructor may still issue device operations. Keep our
         * fd open until process teardown and revoke an already written marker
         * before forwarding any such request. Never write through a replaced fd. */
        if (positional(&zero, sizeof(zero), 0) && ftruncate(output, 0)) {
            static const char message[] = "[octool_capture] INCOMPLETE: cannot revoke finalized file; discard it\n";
            (void)syscall(SYS_write, STDERR_FILENO, message, sizeof(message)-1);
        }
        finalized = 0;
    }
    if (!invalid) {
        char line[192];
        int n = snprintf(line, sizeof(line), "[octool_capture] INCOMPLETE: %s\n", reason);
        if (n > 0) (void)syscall(SYS_write, STDERR_FILENO, line,
                     (size_t)n < sizeof(line) ? (size_t)n : sizeof(line)-1);
    }
    invalid = 1;
}
/* Retry ordinary trace-file I/O only, NEVER a device request. */
static int append_bytes(const void *data, size_t len)
{
    const char *p = data;
    while (len) {
        ssize_t n = next_write(output, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || (size_t)n > len) return -1;
        p += n; len -= (size_t)n;
    }
    return 0;
}
static int is_target(int fd)
{
    struct stat a, b;
    if (fd < 0 || fstat(fd, &a) || stat(device, &b)) return 0;
    if (S_ISCHR(b.st_mode)) return S_ISCHR(a.st_mode) && a.st_rdev == b.st_rdev;
    /* Ordinary file permitted for no-hardware regression fixtures. */
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
static int watched(int fd) { return fd >= 0 && fd < MAXFD && slots[fd].watched; }
static void forget(int fd)
{
    if (watched(fd)) {
        if (active) reject("descriptor changed during request");
        memset(&slots[fd], 0, sizeof(slots[fd]));
    }
}
static void fork_prepare(void)
{
    pthread_mutex_lock(&lock);
    reject("forked capture is unsupported");
}
static void fork_parent(void) { pthread_mutex_unlock(&lock); }
static void fork_child(void)
{
    /* Child must not commit parent's shared output file. */
    if (output >= 0) (void)syscall(SYS_close, output);
    output = -1;
    pthread_mutex_unlock(&lock);
}
static void resolve(void)
{
    const char *path;
    struct stat st;
    struct octool_trace_hdr h = {0, OCTOOL_TRACE_REQSZ, OCTOOL_TRACE_MBOXW, 0};
    inside = 1;
#define RESOLVE(name) next_##name = dlsym(RTLD_NEXT, #name)
    RESOLVE(open); RESOLVE(open64); RESOLVE(openat); RESOLVE(openat64);
    RESOLVE(close); RESOLVE(dup); RESOLVE(dup2); RESOLVE(dup3); RESOLVE(write);
    RESOLVE(mmap); RESOLVE(mmap64); RESOLVE(munmap); RESOLVE(mprotect); RESOLVE(mremap);
#undef RESOLVE
    path = getenv("OCTOOL_CAP_DEV");
    if (path && *path) device = path;
    path = getenv("OCTOOL_CAP_OUT");
    if (!path || !*path) path = "octool_trace.bin";
    /* Lock before truncating: an exec'd child inherits LD_PRELOAD/environment
     * but must not destroy its parent's trace. Callers use a fresh temp path
     * so initialization failure can never be mistaken for an old capture. */
    output = next_open(path, O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (output < 0 || fstat(output, &st) || !S_ISREG(st.st_mode) ||
        flock(output, LOCK_EX | LOCK_NB) || ftruncate(output, 0) ||
        append_bytes(&h, sizeof(h))) reject("cannot initialize regular trace file");
    if (pthread_atfork(fork_prepare, fork_parent, fork_child))
        reject("cannot register fork boundary");
    initialized = 1;
    inside = 0;
}
static void ensure(void)
{
    int saved = errno;
    pthread_once(&once, resolve);
    errno = saved;
}
static int has_mode(int flags)
{
    return (flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE;
}
static int note_open(int fd)
{
    int saved = errno;
    pthread_mutex_lock(&lock);
    if (fd >= 0) {
        forget(fd);
        if (is_target(fd)) {
            if (fd >= MAXFD) reject("device fd exceeds capture table");
            else { slots[fd].watched = 1; slots[fd].cookie = ++next_cookie; }
        }
    }
    pthread_mutex_unlock(&lock); errno = saved; return fd;
}
#define OPEN_WRAPPER(name, fallback) \
int name(const char *path, int flags, ...) { \
    mode_t mode = 0; int fd; \
    if (has_mode(flags)) { va_list a; va_start(a, flags); mode = va_arg(a, int); va_end(a); } \
    if (inside) return (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode); \
    ensure(); fd = next_##name ? next_##name(path, flags, mode) : fallback(path, flags, mode); \
    return note_open(fd); \
}
OPEN_WRAPPER(open, next_open)
OPEN_WRAPPER(open64, next_open)
#define OPENAT_WRAPPER(name, fallback) \
int name(int dirfd, const char *path, int flags, ...) { \
    mode_t mode = 0; int fd; \
    if (has_mode(flags)) { va_list a; va_start(a, flags); mode = va_arg(a, int); va_end(a); } \
    if (inside) return (int)syscall(SYS_openat, dirfd, path, flags, mode); \
    ensure(); fd = next_##name ? next_##name(dirfd, path, flags, mode) : fallback(dirfd, path, flags, mode); \
    return note_open(fd); \
}
OPENAT_WRAPPER(openat, next_openat)
OPENAT_WRAPPER(openat64, next_openat)

/* No start+length overflow. Mailbox starts at its page-aligned mapping base. */
static int overlaps(uintptr_t a, size_t n, uintptr_t b, size_t m)
{
    if (!n || !m) return 0;
    return a <= b ? b-a < n : a-b < m;
}
static void invalidate_maps(void *addr, size_t len)
{
    int fd;
    for (fd = 0; fd < MAXFD; ++fd)
        if (slots[fd].mbox && overlaps((uintptr_t)addr, len,
                                      (uintptr_t)slots[fd].mbox, MBOX_BYTES)) {
            slots[fd].mbox = NULL;
            if (active) reject("mailbox mapping changed during request");
        }
}
static void note_map(int fd, size_t len, int prot, int flags, off64_t off, void *p)
{
    if (p == MAP_FAILED || (flags & MAP_ANONYMOUS)) return;
    if (!watched(fd)) {
        if (is_target(fd)) reject("mapping through untracked descriptor alias");
        return;
    }
    if (active) reject("mailbox mapping changed during request");
    if (off || len < MBOX_BYTES || !(prot & PROT_READ) ||
        ((flags & MAP_TYPE) != MAP_SHARED && (flags & MAP_TYPE) != MAP_SHARED_VALIDATE)) {
        reject("unsupported mailbox mapping"); return;
    }
    slots[fd].mbox = p;
}
#define MMAP_WRAPPER(name, offset_type, fallback) \
void *name(void *addr, size_t len, int prot, int flags, int fd, offset_type off) { \
    void *p; int saved; \
    if (inside) return (void *)syscall(SYS_mmap, addr, len, prot, flags, fd, off); \
    ensure(); pthread_mutex_lock(&lock); inside = 1; \
    if (flags & MAP_FIXED) invalidate_maps(addr, len); \
    p = next_##name ? next_##name(addr, len, prot, flags, fd, off) : fallback(addr, len, prot, flags, fd, off); \
    saved = errno; note_map(fd, len, prot, flags, off, p); \
    inside = 0; pthread_mutex_unlock(&lock); errno = saved; return p; \
}
MMAP_WRAPPER(mmap, off_t, next_mmap)
MMAP_WRAPPER(mmap64, off64_t, next_mmap)
int munmap(void *addr, size_t len)
{
    int r, saved;
    if (inside) return (int)syscall(SYS_munmap, addr, len);
    ensure(); pthread_mutex_lock(&lock); inside = 1;
    /* Conservative even on failure: never keep a possibly stale address. */
    invalidate_maps(addr, len);
    r = next_munmap(addr, len); saved = errno;
    inside = 0; pthread_mutex_unlock(&lock); errno = saved; return r;
}
int mprotect(void *addr, size_t len, int prot)
{
    int r, saved;
    if (inside) return (int)syscall(SYS_mprotect, addr, len, prot);
    ensure(); pthread_mutex_lock(&lock); inside = 1;
    if (!(prot & PROT_READ)) invalidate_maps(addr, len);
    r = next_mprotect(addr, len, prot); saved = errno;
    inside = 0; pthread_mutex_unlock(&lock); errno = saved; return r;
}
void *mremap(void *addr, size_t old_len, size_t new_len, int flags, ...)
{
    void *dest = NULL, *p; int saved;
    if (flags & MREMAP_FIXED) {
        va_list a; va_start(a, flags); dest = va_arg(a, void *); va_end(a);
    }
    if (inside) return (void *)syscall(SYS_mremap, addr, old_len, new_len, flags, dest);
    ensure(); pthread_mutex_lock(&lock); inside = 1;
    /* Do not infer alias semantics for old_len=0 or DONTUNMAP. */
    invalidate_maps(addr, old_len ? old_len : 1);
    if (flags & MREMAP_FIXED) invalidate_maps(dest, new_len);
    p = next_mremap(addr, old_len, new_len, flags, dest); saved = errno;
    inside = 0; pthread_mutex_unlock(&lock); errno = saved; return p;
}
int close(int fd)
{
    int saved = errno;
    if (inside) return (int)syscall(SYS_close, fd);
    ensure(); pthread_mutex_lock(&lock);
    forget(fd); /* Linux releases fd even for many close error returns. */
    if (fd == output) { reject("trace descriptor closed by application"); output = -1; }
    pthread_mutex_unlock(&lock); errno = saved;
    return next_close(fd);
}
static int duplicate(int oldfd, int newfd, int flags, int which)
{
    int r, saved;
    ensure(); pthread_mutex_lock(&lock); inside = 1;
    if (which != 1 && newfd == output && oldfd != newfd) {
        reject("trace descriptor replacement attempted");
        output = -1;
    }
    r = which == 1 ? next_dup(oldfd) : which == 2 ? next_dup2(oldfd, newfd)
                                                : next_dup3(oldfd, newfd, flags);
    saved = errno;
    if (r >= 0 && r != oldfd) {
        if (watched(oldfd) || is_target(oldfd)) reject("device descriptor duplication unsupported");
        forget(r);
        if (r == output) { reject("trace descriptor replaced by application"); output = -1; }
    }
    inside = 0; pthread_mutex_unlock(&lock); errno = saved; return r;
}
int dup(int fd) { return inside ? (int)syscall(SYS_dup, fd) : duplicate(fd, -1, 0, 1); }
int dup2(int fd, int dest) { return inside ? (int)syscall(SYS_dup2, fd, dest) : duplicate(fd, dest, 0, 2); }
int dup3(int fd, int dest, int flags) { return inside ? (int)syscall(SYS_dup3, fd, dest, flags) : duplicate(fd, dest, flags, 3); }
static uint64_t now_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { reject("monotonic clock failed"); return 0; }
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
ssize_t write(int fd, const void *buf, size_t count)
{
    struct octool_trace_rec rec = {{0}, {0}, 0, 0, 0};
    uint64_t cookie = 0, start;
    ssize_t r; int saved, entry_errno = errno, observe = 0;
    if (inside) return next_write ? next_write(fd, buf, count) : syscall(SYS_write, fd, buf, count);
    ensure(); pthread_mutex_lock(&lock); inside = 1;
    if (watched(fd)) {
        if (finalized) reject("device request after trace finalization");
        if (!is_target(fd)) { reject("device descriptor identity changed"); forget(fd); }
        else if (!invalid) {
            struct iovec local = {rec.req, sizeof(rec.req)}, remote = {(void *)buf, sizeof(rec.req)};
            if (active) reject("overlapping device requests");
            else if (count != OCTOOL_TRACE_REQSZ) reject("device request is not 96 bytes");
            else if (!slots[fd].mbox) reject("no readable tracked mailbox");
            /* Request is ordinary user memory: avoid turning EFAULT into a
             * userspace signal. Do NOT use GUP-based copying for PFNMAP mailboxes. */
            else if (syscall(SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0) != OCTOOL_TRACE_REQSZ)
                reject("request snapshot unavailable");
            else if (__atomic_load_n(slots[fd].mbox, __ATOMIC_ACQUIRE) != 0)
                reject("mailbox was not cleared before request");
            else {
                cookie = slots[fd].cookie; rec.seq = slots[fd].seq++;
                active++; observe = 1;
            }
        }
    } else if (fd != output && is_target(fd)) reject("write through untracked descriptor alias");
    inside = 0; pthread_mutex_unlock(&lock);
    /* Exactly once, including errors/short writes. */
    errno = entry_errno;
    r = next_write(fd, buf, count); saved = errno;
    if (!observe) { errno = saved; return r; }
    pthread_mutex_lock(&lock); inside = 1;
    rec.wrote = (int32_t)r;
    if (r != (ssize_t)OCTOOL_TRACE_REQSZ) reject("failed or short device write");
    start = now_ns();
    while (!invalid) {
        const uint64_t *mb; uint64_t done; int i;
        if (!watched(fd) || slots[fd].cookie != cookie || !(mb = slots[fd].mbox)) {
            reject("request lost descriptor or mailbox"); break;
        }
        /* Supported VMA changes take this same mutex; no speculative pointers. */
        done = __atomic_load_n(mb, __ATOMIC_ACQUIRE);
        if (done == 1) {
            for (i = 0; i < OCTOOL_TRACE_MBOXW; ++i)
                rec.mbox[i] = __atomic_load_n(mb+i, __ATOMIC_RELAXED);
            if (__atomic_load_n(mb, __ATOMIC_ACQUIRE) != 1 || rec.mbox[0] != 1) {
                reject("mailbox changed during snapshot"); break;
            }
            rec.completed = 1;
            if (output < 0 || append_bytes(&rec, sizeof(rec))) reject("trace record write failed");
            else records++;
            break;
        }
        if (done) { reject("error or invalid mailbox completion"); break; }
        if (now_ns() - start >= 1000000000ULL) { reject("mailbox completion timeout"); break; }
        inside = 0; pthread_mutex_unlock(&lock);
        { const struct timespec nap = {0, 1000000}; (void)nanosleep(&nap, NULL); }
        pthread_mutex_lock(&lock); inside = 1;
    }
    active--;
    inside = 0; pthread_mutex_unlock(&lock); errno = saved; return r;
}
static int positional(const void *data, size_t len, off_t off)
{
    const char *p = data;
    while (len) {
        ssize_t n = pwrite(output, p, len, off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || (size_t)n > len) return -1;
        p += n; len -= (size_t)n; off += n;
    }
    return 0;
}
__attribute__((destructor)) static void finish(void)
{
    struct octool_trace_hdr h = {0, OCTOOL_TRACE_REQSZ, OCTOOL_TRACE_MBOXW, 0};
    uint64_t magic = OCTOOL_TRACE_MAGIC_V2;
    if (!initialized) return;
    pthread_mutex_lock(&lock); inside = 1;
    if (active) reject("process exited during device request");
    if (!records) reject("no complete device requests");
    h.nrec = records;
    /* Publish marker LAST, after all complete records and exact count sync.
     * _exit/signals/I/O failure leave invalid magic. No claim of final marker
     * durability against power loss; a missing marker simply rejects capture. */
    if (!invalid && output >= 0) {
        if (positional(&h, sizeof(h), 0) || fdatasync(output) ||
            positional(&magic, sizeof(magic), 0)) reject("trace finalization failed");
        else finalized = 1;
    }
    /* Kernel process teardown closes this private fd/lock. Keeping it open
     * lets later DSO destructors invalidate a provisional successful marker. */
    inside = 0; pthread_mutex_unlock(&lock);
}
