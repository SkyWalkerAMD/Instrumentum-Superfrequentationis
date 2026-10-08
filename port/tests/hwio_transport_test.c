// SPDX-License-Identifier: GPL-2.0
/* Exercise the real device transport through syscall interposition. No device
 * is opened and no hardware instruction/physical-memory access is executed. */
#include "../hal/octool_hwio.h"
#include "../abi/octool_hwio_abi.h"
#include "../abi/octool_hwio_caps.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static uint64_t mailbox[1024];
static struct octool_hwio_req last;
static int direct_mode, short_io, reply_error;
static unsigned fortified_reads;
static unsigned io_calls, closes, unmaps;
static unsigned queries, mappings;
static int query_return, query_errno = ENOTTY;
static struct octool_hwio_caps caps_reply = {
    .magic = OCTOOL_CAPS_MAGIC, .version = OCTOOL_CAPS_VERSION,
    .size = sizeof(struct octool_hwio_caps), .features = OCTOOL_CAP_ALL
};
_Static_assert(sizeof(struct octool_hwio_caps) == 32, "caps size is fixed");
_Static_assert(offsetof(struct octool_hwio_caps, features) == 8, "caps features offset");
_Static_assert(OCTOOL_HWIO_GET_CAPS_V1 == 0x80204f80UL, "x86-64 ioctl encoding");
static ssize_t token_read = 8;
static uint64_t forced_done;
static size_t memory_length;
static const uint64_t token = 71;
int __wrap_open(const char *path, int flags, ...)
{
    (void)flags;
    ++io_calls;
    if (direct_mode && !strcmp(path, "/dev/mydev")) { errno = ENOENT; return -1; }
    return 600;
}
int __wrap_close(int fd) { assert(fd == 600); ++closes; return 0; }
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    assert(fd == 600 && request == OCTOOL_HWIO_GET_CAPS_V1);
    ++queries; ++io_calls;
    if (query_return < 0) { errno = query_errno; return -1; }
    va_start(ap, request);
    struct octool_hwio_caps *caps = va_arg(ap, struct octool_hwio_caps *);
    va_end(ap);
    *caps = caps_reply;
    return query_return;
}
void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    (void)addr; (void)prot; (void)flags; (void)off;
    ++io_calls;
    ++mappings;
    assert(len == (memory_length ? memory_length : (size_t)sysconf(_SC_PAGESIZE)));
    assert(fd == 600); return mailbox;
}
int __wrap_munmap(void *addr, size_t length)
{
    assert(addr == mailbox);
    assert(length == (memory_length ? memory_length : (size_t)sysconf(_SC_PAGESIZE)));
    ++unmaps; return 0;
}
ssize_t __wrap_read(int fd, void *buf, size_t len)
{
    ++io_calls;
    assert(fd == 600 && len == 8);
    if (token_read < 0) { errno = EACCES; return -1; }
    memcpy(buf, &token, (size_t)token_read); return token_read;
}
ssize_t __wrap_write(int fd, const void *buf, size_t len)
{
    assert(fd == 600 && len == 96); memcpy(&last, buf, len);
    ++io_calls;
    if (short_io) { errno = 0; return 12; }
    mailbox[0] = forced_done ? forced_done : ((uint64_t)(uint32_t)reply_error << 32) | 1;
    mailbox[1] = 0xfedcba9876543210ULL;
    mailbox[2] = 2; mailbox[3] = 3; mailbox[4] = 4;
    return len;
}
ssize_t __wrap_pread(int fd, void *buf, size_t len, off_t off)
{ (void)off; ++io_calls; assert(fd == 600); memset(buf, 0, len); errno = 0; return short_io ? 0 : (ssize_t)len; }
/* Some distro compilers redirect the bounded PCI buffer read to this libc
 * entry point. Keep the bound check and route it to the same fake syscall. */
ssize_t __wrap___pread_chk(int fd, void *buf, size_t len, off_t off, size_t capacity)
{
    assert(len <= capacity);
    fortified_reads++;
    return __wrap_pread(fd, buf, len, off);
}
ssize_t __wrap_pwrite(int fd, const void *buf, size_t len, off_t off)
{ (void)buf; (void)off; ++io_calls; assert(fd == 600); errno = 0; return short_io ? 0 : (ssize_t)len; }

static void invalid_inputs(hwio_t *h)
{
    const int widths[] = {-1, 0, 3, 8, 0x7fffffff};
    const unsigned fields[][3] = {{32,0,0}, {0,8,0}, {0,0,256}, {0,0,65535}, {0,0,1}, {0,0,2}};
    const unsigned before = io_calls;
    uint32_t value = 0xa5a5a5a5;
    uint64_t wide = 0;
    for (size_t i = 0; i < sizeof(widths)/sizeof(widths[0]); ++i) {
        assert(hwio_pci_read(h, 0, 0, 0, 0, widths[i], &value) == -EINVAL);
        assert(hwio_pci_write(h, 0, 0, 0, 0, widths[i], 0) == -EINVAL);
    }
    for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        assert(hwio_pci_read(h, 0, fields[i][0], fields[i][1], fields[i][2], 4, &value) == -EINVAL);
        assert(hwio_pci_write(h, 0, fields[i][0], fields[i][1], fields[i][2], 4, 0) == -EINVAL);
    }
    assert(hwio_mem_read(h, UINT64_MAX, 8, &wide) == -EINVAL);
    assert(hwio_mem_write(h, UINT64_MAX, 8, 0) == -EINVAL);
    assert(value == 0xa5a5a5a5 && io_calls == before);
}

static void capability_checks(void)
{
    const struct octool_hwio_caps valid = caps_reply;
    uint64_t value = 0;
    uint32_t pci = 0;
    uint8_t ec = 0;
    for (unsigned mode = 0; mode < 10; ++mode) {
        unsigned c = closes, u = unmaps, m = mappings;
        caps_reply = valid; query_return = 0;
        switch (mode) {
        case 0: query_return = -1; query_errno = ENOTTY; break;
        case 1: query_return = -1; query_errno = EACCES; break;
        case 2: query_return = 1; break;
        case 3: caps_reply.magic ^= 1; break;
        case 4: caps_reply.version++; break;
        case 5: caps_reply.size--; break;
        case 6: caps_reply.reserved[0] = 1; break;
        case 7: caps_reply.reserved[1] = 1; break;
        case 8: caps_reply.features = 0; break;
        case 9: caps_reply.features = 1ULL << 63; break;
        }
        hwio_t *h = hwio_open(NULL);
        assert(h && hwio_backend_for(h, HWIO_FAM_MMIO) != HWIO_BE_MODULE);
        assert(closes == c + 1 && unmaps == u && mappings == m);
        hwio_close(h);
    }
    caps_reply = valid; query_return = 0;
    caps_reply.features = OCTOOL_CAP_MMIO | (1ULL << 63); /* Ignore unknown feature bits. */
    hwio_t *h = hwio_open(NULL);
    assert(h && hwio_backend_for(h, HWIO_FAM_MMIO) == HWIO_BE_MODULE);
    assert(hwio_backend_for(h, HWIO_FAM_MSR) == HWIO_BE_NONE);
    assert(hwio_backend_for(h, HWIO_FAM_EC) == HWIO_BE_NONE);
    assert(hwio_backend_for(h, HWIO_FAM_CPU) == HWIO_BE_DIRECT);
    unsigned before = io_calls;
    assert(hwio_rdmsr(h, 0, 0, &value) == -EPERM);
    assert(hwio_pci_read(h, 0, 0, 0, 0, 4, &pci) == -EPERM);
    assert(hwio_ec_read(h, 0, &ec) == -EPERM && io_calls == before);
    assert(hwio_mem_read(h, 0x8000, 8, &value) == 0 && last.user_id == token);
    hwio_close(h);
    caps_reply = valid; query_return = -1; query_errno = ENOTTY;
    before = queries;
    h = hwio_open_legacy_mmio("/dev/mydev");
    assert(h && queries == before); /* Explicit old-node opt-in, no ioctl needed. */
    assert(hwio_backend_for(h, HWIO_FAM_MMIO) == HWIO_BE_MODULE);
    before = io_calls;
    assert(hwio_wrmsr(h, 0, 0, 0) == -EPERM);
    assert(hwio_io_write(h, 0, 4, 0) == -EPERM);
    assert(hwio_pci_write(h, 0, 0, 0, 0, 4, 0) == -EPERM);
    assert(hwio_ec_write(h, 0, 0) == -EPERM && io_calls == before);
    assert(hwio_mem_read(h, 0x8000, 8, &value) == 0 && last.cmd == 0x0a && last.user_id == token);
    hwio_close(h);
    query_return = 0;
}

int main(void)
{
    hwio_t *h = hwio_open(NULL);
    uint64_t value = 0;
    uint32_t words[4], pci;
    assert(h && hwio_backend_for(h, HWIO_FAM_MSR) == HWIO_BE_MODULE);
    invalid_inputs(h);
    assert(hwio_rdmsr(h, 3, 0x123, &value) == 0 && last.user_id == 3);
    assert(value == 0xfedcba9876543210ULL && last.data0 == 0x123);
    assert(hwio_wrmsr(h, 0, 0x123, value) == 0 && last.user_id == 0);
    assert(hwio_cpuid(h, 5, 1, 2, words) == 0 && last.user_id == 5 && words[3] == 4);
    assert(hwio_rdtsc(h, 7, &value) == 0 && last.user_id == 7);
    assert(hwio_mem_write(h, 0x8000, 8, 0x1122334455667788ULL) == 0);
    assert(last.cmd == 0x0b && last.user_id == token && last.data0 == 0x8000 && last.data1 == 0x1122334455667788ULL);
    assert(hwio_mem_read(h, 0x8000, 8, &value) == 0 && last.cmd == 0x0a && last.user_id == token);
    reply_error = -EACCES;
    value = 0xabcdef;
    assert(hwio_rdmsr(h, 3, 0x123, &value) == -EACCES);
    assert(value == 0xabcdef);
    const uint64_t malformed[] = {2, 0x100000001ULL, 0xffffffff00000002ULL, 0xfffff00000000001ULL};
    for (size_t i = 0; i < sizeof(malformed)/sizeof(malformed[0]); ++i) {
        forced_done = malformed[i];
        assert(hwio_mem_read(h, 0x8000, 8, &value) == -EPROTO && value == 0xabcdef);
    }
    forced_done = 0;
    reply_error = 0; short_io = 1;
    assert(hwio_wrmsr(h, 3, 0x123, 1) == -EIO);
    hwio_close(h);
    for (token_read = -1; token_read < 8; ++token_read) {
        unsigned c = closes, u = unmaps;
        h = hwio_open(NULL);
        assert(h && hwio_backend_for(h, HWIO_FAM_MMIO) != HWIO_BE_MODULE);
        assert(closes == c + 1 && unmaps == u + 1);
        hwio_close(h);
    }
    token_read = 8;
    short_io = 0;
    capability_checks();
    short_io = 1;
    direct_mode = 1;
    h = hwio_open(NULL);
    assert(h && hwio_backend_for(h, HWIO_FAM_MSR) == HWIO_BE_DIRECT);
    invalid_inputs(h);
    assert(hwio_rdmsr(h, 3, 0x123, &value) == -EIO);
    assert(hwio_wrmsr(h, 3, 0x123, 1) == -EIO);
    assert(hwio_pci_read(h, 0, 0, 0, 0, 4, &pci) == -EIO);
    assert(hwio_pci_write(h, 0, 0, 0, 0, 4, 1) == -EIO);
    short_io = 0;
    /* Last naturally aligned conventional config register remains accepted. */
    assert(hwio_pci_read(h, 255, 31, 7, 252, 4, &pci) == 0);
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    assert(page == 4096); /* This test, like the supported targets, is x86-64. */
    for (int width = 2; width <= 8; width *= 2) {
        uint64_t expected = 0x1122334455667788ULL & (UINT64_MAX >> (8 * (8 - width)));
        memory_length = page - 1 + (size_t)width;
        memcpy((char *)mailbox + page - 1, &expected, (size_t)width);
        assert(hwio_mem_read(h, page - 1, width, &value) == 0 && value == expected);
        assert(hwio_mem_write(h, page - 1, width, 0) == 0);
        uint64_t after = 0;
        memcpy(&after, (char *)mailbox + page - 1, (size_t)width);
        assert(after == 0);
    }
    memory_length = 0;
    hwio_close(h);
    puts("PASS: CPU/token wire fields, malformed replies, failed tokens, invalid PCI and MMIO mapping boundaries");
    puts("PASS: capability rejection before mmap, partial capabilities and explicit legacy MMIO-only transport");
    printf("Fortified pread calls intercepted: %u\n", fortified_reads);
    return 0;
}
