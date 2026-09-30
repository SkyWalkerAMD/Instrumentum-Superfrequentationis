// SPDX-License-Identifier: GPL-2.0
/* Exercise the real device transport through syscall interposition. No device
 * is opened and no hardware instruction/physical-memory access is executed. */
#include "../hal/octool_hwio.h"
#include "../abi/octool_hwio_abi.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static uint64_t mailbox[512];
static struct octool_hwio_req last;
static int direct_mode, short_io, reply_error;
static unsigned fortified_reads;
static const uint64_t token = 71;
int __wrap_open(const char *path, int flags, ...)
{
    (void)flags;
    if (direct_mode && !strcmp(path, "/dev/mydev")) { errno = ENOENT; return -1; }
    return 600;
}
int __wrap_close(int fd) { assert(fd == 600); return 0; }
void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    (void)addr; (void)len; (void)prot; (void)flags; (void)off;
    assert(fd == 600); return mailbox;
}
int __wrap_munmap(void *addr, size_t length) { (void)length; assert(addr == mailbox); return 0; }
ssize_t __wrap_read(int fd, void *buf, size_t len)
{ assert(fd == 600 && len == 8); memcpy(buf, &token, 8); return 8; }
ssize_t __wrap_write(int fd, const void *buf, size_t len)
{
    assert(fd == 600 && len == 96); memcpy(&last, buf, len);
    if (short_io) { errno = 0; return 12; }
    mailbox[0] = ((uint64_t)(uint32_t)reply_error << 32) | 1;
    mailbox[1] = 0xfedcba9876543210ULL;
    mailbox[2] = 2; mailbox[3] = 3; mailbox[4] = 4;
    return len;
}
ssize_t __wrap_pread(int fd, void *buf, size_t len, off_t off)
{ (void)off; assert(fd == 600); memset(buf, 0, len); errno = 0; return short_io ? 0 : (ssize_t)len; }
/* Some distro compilers redirect the bounded PCI buffer read to this libc
 * entry point. Keep the bound check and route it to the same fake syscall. */
ssize_t __wrap___pread_chk(int fd, void *buf, size_t len, off_t off, size_t capacity)
{
    assert(len <= capacity);
    fortified_reads++;
    return __wrap_pread(fd, buf, len, off);
}
ssize_t __wrap_pwrite(int fd, const void *buf, size_t len, off_t off)
{ (void)buf; (void)off; assert(fd == 600); errno = 0; return short_io ? 0 : (ssize_t)len; }

int main(void)
{
    hwio_t *h = hwio_open(NULL);
    uint64_t value = 0;
    uint32_t words[4], pci;
    assert(h && hwio_backend_for(h, HWIO_FAM_MSR) == HWIO_BE_MODULE);
    assert(hwio_rdmsr(h, 3, 0x123, &value) == 0 && last.user_id == 3);
    assert(value == 0xfedcba9876543210ULL && last.data0 == 0x123);
    assert(hwio_wrmsr(h, 0, 0x123, value) == 0 && last.user_id == 0);
    assert(hwio_cpuid(h, 5, 1, 2, words) == 0 && last.user_id == 5 && words[3] == 4);
    assert(hwio_rdtsc(h, 7, &value) == 0 && last.user_id == 7);
    assert(hwio_mem_write(h, 0x8000, 8, 0x1122334455667788ULL) == 0);
    assert(last.cmd == 0x0b && last.user_id == token && last.data0 == 0x8000 && last.data1 == 0x1122334455667788ULL);
    assert(hwio_mem_read(h, 0x8000, 8, &value) == 0 && last.cmd == 0x0a && last.user_id == token);
    reply_error = -EACCES;
    assert(hwio_rdmsr(h, 3, 0x123, &value) == -EACCES);
    reply_error = 0; short_io = 1;
    assert(hwio_wrmsr(h, 3, 0x123, 1) == -EIO);
    hwio_close(h);
    direct_mode = 1;
    h = hwio_open(NULL);
    assert(h && hwio_backend_for(h, HWIO_FAM_MSR) == HWIO_BE_DIRECT);
    assert(hwio_rdmsr(h, 3, 0x123, &value) == -EIO);
    assert(hwio_wrmsr(h, 3, 0x123, 1) == -EIO);
    assert(hwio_pci_read(h, 0, 0, 0, 0, 4, &pci) == -EIO);
    assert(hwio_pci_write(h, 0, 0, 0, 0, 4, 1) == -EIO);
    hwio_close(h);
    puts("PASS: real transport preserves target CPU and legacy MMIO token; short I/O fails");
    printf("Fortified pread calls intercepted: %u\n", fortified_reads);
    return 0;
}
