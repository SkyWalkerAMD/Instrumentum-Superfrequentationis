// SPDX-License-Identifier: GPL-2.0
/* Experimental, opt-in guard for the eight pinned original MMIO wrappers.
 * It never changes a request or mailbox. An error ends the process with 74;
 * returning an errno to these callers would merely enter their infinite loop.
 * Not a hardware access broker, signal handler, or general write interposer.
 * Generate legacy-mailbox-layout.h with analysis/tools/test-legacy-guard.py.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct legacy_function {
    uintptr_t address, returned;
    size_t size;
    unsigned opcode;
    const unsigned char *code;
};
#include "legacy-mailbox-layout.h"

static ssize_t (*next_write)(int, const void *, size_t);
static pthread_once_t once = PTHREAD_ONCE_INIT;
static _Thread_local int resolving;
static uintptr_t image_base;
static int matched;
static atomic_flag active = ATOMIC_FLAG_INIT;

static int contains(const struct dl_phdr_info *info, uintptr_t off,
                    size_t len, unsigned flags)
{
    unsigned i;
    for (i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type == PT_LOAD && (p->p_flags & flags) == flags &&
            off >= p->p_vaddr && len <= p->p_memsz &&
            off - p->p_vaddr <= p->p_memsz - len)
            return 1;
    }
    return 0;
}

static int find_original(struct dl_phdr_info *info, size_t size, void *data)
{
    size_t i;
    (void)size;
    (void)data;
    /* Both ordinary exec and explicit ld.so invocation may be used. Do not
     * identify the main image using /proc/self/exe (then it names ld.so).
     * These eight byte ranges and all globals must belong to one ELF image.
     */
    for (i = 0; i < LEGACY_FUNCTION_COUNT; ++i) {
        const struct legacy_function *f = &legacy_functions[i];
        if (!contains(info, f->address, f->size, PF_R | PF_X) ||
            memcmp((void *)(info->dlpi_addr + f->address), f->code, f->size))
            return 0;
    }
    if (!contains(info, LEGACY_FD, sizeof(int), PF_R | PF_W) ||
        !contains(info, LEGACY_MAILBOX, sizeof(uintptr_t), PF_R | PF_W) ||
        !contains(info, LEGACY_REQUEST, 96, PF_R | PF_W))
        return 0;
    image_base = info->dlpi_addr;
    matched = 1;
    return 1;
}

static void resolve(void)
{
    resolving = 1;
    next_write = dlsym(RTLD_NEXT, "write");
    dl_iterate_phdr(find_original, NULL);
    resolving = 0;
}

static _Noreturn void fail(const char *reason, int fd, unsigned op,
                          ssize_t wrote, int error, uint64_t done)
{
    char line[320];
    int n = snprintf(line, sizeof(line),
        "{\"event\":\"octool-mailbox-guard\",\"reason\":\"%s\","
        "\"fd\":%d,\"opcode\":%u,\"wrote\":%ld,\"errno\":%d,"
        "\"done\":\"%016lx\",\"exit\":74}\n",
        reason, fd, op, (long)wrote, error, (unsigned long)done);
    if (n > 0)
        (void)syscall(SYS_write, STDERR_FILENO, line,
                      (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    /* No Qt/C++ cleanup may issue another hardware operation after failure. */
    _exit(74);
}

static uint64_t monotonic_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        fail("clock", -1, 0, -1, errno, 0);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

ssize_t write(int fd, const void *buf, size_t len)
{
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    const struct legacy_function *f = NULL;
    const uint64_t *mailbox;
    uint64_t done, started, opcode;
    ssize_t wrote;
    int saved_errno;
    size_t i;

    if (resolving)
        return syscall(SYS_write, fd, buf, len);
    pthread_once(&once, resolve);
    if (!next_write)
        fail("resolve-write", fd, 0, -1, ENOSYS, 0);
    if (matched)
        for (i = 0; i < LEGACY_FUNCTION_COUNT; ++i)
            if (caller == image_base + legacy_functions[i].returned) {
                f = &legacy_functions[i];
                break;
            }
    if (!f)
        return next_write(fd, buf, len);

    if (atomic_flag_test_and_set_explicit(&active, memory_order_acquire))
        fail("overlapping-write", fd, f->opcode, -1, EBUSY, 0);
    if (len != 96 || buf != (void *)(image_base + LEGACY_REQUEST) ||
        fd != *(int *)(image_base + LEGACY_FD))
        fail("request-shape", fd, f->opcode, -1, EINVAL, 0);
    memcpy(&opcode, buf, sizeof(opcode));
    if (opcode != f->opcode)
        fail("request-opcode", fd, f->opcode, -1, EINVAL, 0);
    mailbox = *(const uint64_t **)(image_base + LEGACY_MAILBOX);
    if (!mailbox || ((uintptr_t)mailbox & 7))
        fail("mailbox-pointer", fd, f->opcode, -1, EINVAL, 0);
    if (__atomic_load_n(mailbox, __ATOMIC_ACQUIRE) != 0)
        fail("mailbox-not-cleared", fd, f->opcode, -1, EBUSY, 0);

    /* Exactly one original write. Never retry a short/failed hardware write:
     * it may already have changed the device. Never rewrite a completion word.
     */
    wrote = next_write(fd, buf, len);
    saved_errno = errno;
    if (wrote != (ssize_t)len)
        fail(wrote < 0 ? "write-error" : "short-write", fd, f->opcode,
             wrote, wrote < 0 ? saved_errno : EIO,
             __atomic_load_n(mailbox, __ATOMIC_ACQUIRE));

    /* This bound begins AFTER write returns. It cannot interrupt a blocked
     * kernel write, fix the old module's done-before-result race, or make the
     * original shared request globals safe for concurrent callers.
     */
    started = monotonic_ns();
    for (;;) {
        const struct timespec nap = {0, 100000};
        done = __atomic_load_n(mailbox, __ATOMIC_ACQUIRE);
        if (done == 1)
            break;
        if (done) {
            int32_t status = (int32_t)(done >> 32);
            if ((uint32_t)done == 1 && status >= -4095 && status < 0)
                fail("driver-error", fd, f->opcode, wrote, -status, done);
            fail("invalid-completion", fd, f->opcode, wrote, EPROTO, done);
        }
        if (monotonic_ns() - started >= 1000000000ULL)
            fail("completion-timeout", fd, f->opcode, wrote, ETIMEDOUT, 0);
        (void)nanosleep(&nap, NULL);
    }
    atomic_flag_clear_explicit(&active, memory_order_release);
    errno = saved_errno;
    return wrote;
}
