// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "capture_test.h"
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct capture_test_state capture_test;
static ssize_t (*send_request)(int, const void *, size_t);
static uint64_t request[12] = {0x0c, 71, 0x1000, 0xfeed, 4, 5, 6, 7, 8, 9, 10, 11};
static int device_fd;
static int mode(const char *s) { return !strcmp(capture_test.mode, s); }
static void *submit_thread(void *unused)
{
    (void)unused;
    assert(send_request(device_fd, request, sizeof(request)) == 96);
    assert(errno == EUCLEAN);
    return NULL;
}
int main(int argc, char **argv)
{
    uint64_t *mapping;
    const char *path = getenv("OCTOOL_CAP_DEV");
    int r, other, calls = 2;
    ssize_t wrote;
    long page = sysconf(_SC_PAGESIZE);
    assert(argc == 2 && path && page >= 4096);
    alarm(10);
    send_request = dlsym(RTLD_DEFAULT, "write");
    assert(send_request);
    device_fd = open(path, O_RDWR | O_CLOEXEC);
    assert(device_fd >= 0 && ftruncate(device_fd, page*2) == 0);
    /* Independent userspace responder mapping, intentionally not observed.
     * It stays valid when the application's mapping is removed. */
    capture_test.mailbox = (void *)syscall(SYS_mmap, NULL, page*2,
        PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, 0);
    assert(capture_test.mailbox != MAP_FAILED);
    mapping = mmap(NULL, page*2, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, 0);
    assert(mapping != MAP_FAILED);
    assert(send_request(device_fd, request, sizeof(request)) == 96 && errno == EUCLEAN);
    assert(atomic_load(&capture_test.calls) == 1);
    request[2] = 0x2000;
    __atomic_store_n(capture_test.mailbox, 0, __ATOMIC_RELEASE);
    capture_test.mode = argv[1];

    if (mode("abrupt")) _exit(0);
    if (mode("signal")) { raise(SIGTERM); return 90; }
    if (mode("fork")) {
        pid_t child = fork(); int status;
        assert(child >= 0);
        if (!child) exit(0);
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
    }
    if (mode("munmap")) assert(munmap(mapping, 1) == 0);
    if (mode("mprotect")) assert(mprotect(mapping, page, PROT_NONE) == 0);
    if (mode("read-protect")) assert(mprotect(mapping, page, PROT_READ) == 0);
    if (mode("mremap")) {
        void *p = mremap(mapping, page*2, page*3, MREMAP_MAYMOVE);
        assert(p != MAP_FAILED);
    }
    if (mode("fixed")) assert(mmap(mapping, page, PROT_READ | PROT_WRITE,
        MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == mapping);
    if (mode("double-map")) assert(mmap64(NULL, page, PROT_READ | PROT_WRITE,
        MAP_SHARED, device_fd, 0) != MAP_FAILED);
    if (mode("small-map")) assert(mmap(NULL, 8, PROT_READ | PROT_WRITE,
        MAP_SHARED, device_fd, 0) != MAP_FAILED);
    if (mode("private-map")) assert(mmap(NULL, page, PROT_READ | PROT_WRITE,
        MAP_PRIVATE, device_fd, 0) != MAP_FAILED);
    if (mode("dup")) assert(dup(device_fd) >= 0);
    if (mode("dup2") || mode("dup3")) {
        other = open("/dev/null", O_WRONLY); assert(other >= 0);
        assert((mode("dup2") ? dup2(device_fd, other) : dup3(device_fd, other, O_CLOEXEC)) == other);
    }
    if (mode("dup-same")) assert(dup2(device_fd, device_fd) == device_fd);
    if (mode("dup-fail")) assert(dup2(-1, device_fd) == -1 && errno == EBADF);
    if (mode("fcntl-alias")) {
        device_fd = fcntl(device_fd, F_DUPFD_CLOEXEC, 0); assert(device_fd >= 0);
    }
    if (mode("reuse") || mode("dup-replace")) {
        if (mode("reuse")) {
            int old = device_fd;
            assert(close(device_fd) == 0);
            device_fd = open("/dev/null", O_WRONLY); assert(device_fd == old);
        } else {
            other = open("/dev/null", O_WRONLY); assert(other >= 0);
            assert(dup2(other, device_fd) == device_fd);
        }
        assert(send_request(device_fd, request, 96) == 96);
        assert(atomic_load(&capture_test.calls) == 1);
        return 0;
    }
    if (mode("reopen-alias") || mode("openat-alias") || mode("openat64-alias")) {
        const char *alias = getenv("CAPTURE_ALIAS"); assert(alias);
        assert(close(device_fd) == 0);
        device_fd = mode("openat-alias") ? openat(AT_FDCWD, alias, O_RDWR) :
                    mode("openat64-alias") ? openat64(AT_FDCWD, alias, O_RDWR) : open64(alias, O_RDWR);
        assert(device_fd >= 0);
        assert(mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, 0) != MAP_FAILED);
    }
    if (mode("overlap") || mode("unmap-during") || mode("close-during")) {
        pthread_t t;
        assert(!pthread_create(&t, NULL, submit_thread, NULL));
        while (!atomic_load(&capture_test.entered)) {
            const struct timespec nap = {0, 1000000}; nanosleep(&nap, NULL);
        }
        if (mode("unmap-during")) assert(munmap(mapping, page) == 0);
        if (mode("close-during")) assert(close(device_fd) == 0);
        if (mode("overlap")) {
            assert(send_request(device_fd, request, 96) == 96 && errno == EUCLEAN);
            calls++;
        }
        atomic_store(&capture_test.release, 1);
        assert(!pthread_join(t, NULL));
    } else {
        const void *p = mode("bad-pointer") ? (const void *)(uintptr_t)1 : request;
        wrote = send_request(device_fd, p, mode("bad-count") ? 12 : sizeof(request));
        if (mode("write-fail")) assert(wrote == -1 && errno == EIO);
        else if (mode("bad-pointer")) assert(wrote == -1 && errno == EFAULT);
        else if (mode("bad-count")) assert(wrote == -1 && errno == EINVAL);
        else if (mode("short-write")) assert(wrote == 12 && errno == ENOTTY);
        else assert(wrote == 96 && errno == EUCLEAN);
    }
    r = atomic_load(&capture_test.calls);
    assert(r == calls); /* No hardware request may be retried by capture. */
    if (mode("mutate-request")) assert(request[2] == 0xdeadbeef);
    if (mode("success")) assert(munmap(mapping, page*2) == 0);
    return 0;
}
