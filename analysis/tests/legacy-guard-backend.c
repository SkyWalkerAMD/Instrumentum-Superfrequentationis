// SPDX-License-Identifier: GPL-2.0
/* Synthetic backend only. No open(), /dev file, ioctl or hardware access. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static uint64_t *mailbox;
static const char *test_case;
static unsigned calls;

void fake_configure(uint64_t *page, const char *name)
{
    mailbox = page;
    test_case = name;
    calls = 0;
}

static void *complete_later(void *unused)
{
    const struct timespec nap = {0, 20000000};
    (void)unused;
    (void)nanosleep(&nap, NULL);
    mailbox[1] = UINT64_C(0xfedcba9876543210);
    __atomic_store_n(mailbox, 1, __ATOMIC_RELEASE);
    return NULL;
}

ssize_t write(int fd, const void *buf, size_t size)
{
    char line[360], hex[193];
    const unsigned char *p = buf;
    int n;
    unsigned i;
    pthread_t worker;
    if (fd != 600)
        return syscall(SYS_write, fd, buf, size);
    if (!mailbox || !test_case || size != 96 || ++calls != 1)
        _exit(90);
    for (i = 0; i < 96; ++i)
        (void)snprintf(hex + i * 2, 3, "%02x", p[i]);
    n = snprintf(line, sizeof(line),
        "{\"event\":\"backend\",\"fd\":600,\"size\":96,"
        "\"before\":\"%016lx\",\"request\":\"%s\"}\n",
        (unsigned long)__atomic_load_n(mailbox, __ATOMIC_ACQUIRE), hex);
    (void)syscall(SYS_write, STDOUT_FILENO, line, (size_t)n);
    if (!strcmp(test_case, "delayed_success")) {
        if (pthread_create(&worker, NULL, complete_later, NULL))
            _exit(91);
        (void)pthread_detach(worker);
        return 96;
    }
    mailbox[1] = UINT64_C(0xfedcba9876543210);
    if (!strcmp(test_case, "success") || !strcmp(test_case, "write_error_but_done"))
        __atomic_store_n(mailbox, 1, __ATOMIC_RELEASE);
    else if (!strcmp(test_case, "encoded_enomem"))
        __atomic_store_n(mailbox, UINT64_C(0xfffffff400000001), __ATOMIC_RELEASE);
    else if (!strcmp(test_case, "encoded_einval"))
        __atomic_store_n(mailbox, UINT64_C(0xffffffea00000001), __ATOMIC_RELEASE);
    else if (!strcmp(test_case, "malformed_done"))
        __atomic_store_n(mailbox, 2, __ATOMIC_RELEASE);
    if (!strcmp(test_case, "write_error") || !strcmp(test_case, "write_error_but_done")) {
        errno = EIO;
        return -1;
    }
    if (!strcmp(test_case, "short_write"))
        return 12;
    return 96;
}
