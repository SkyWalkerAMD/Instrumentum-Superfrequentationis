// SPDX-License-Identifier: GPL-2.0
/* Compile the production bus adapters against API doubles, never hardware.
 * This verifies our validation, dispatch and ownership, not the kernel's lock
 * implementation. Actual headers and exported symbols are checked by Kbuild. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define PCI_DEVFN(slot, function) (((slot) << 3) | (function))
#define IS_ENABLED(option) OCTOOL_TEST_ACPI

struct pci_dev { int refs, locked; };
static struct pci_dev device;
static char events[32];
static unsigned event_count, seen_bus, seen_devfn, seen_offset, seen_width;
static u32 seen_value;
static bool missing, busy;
static int api_status, converted_status;

static void event(char c)
{
    assert(event_count + 1 < sizeof(events));
    events[event_count++] = c;
    events[event_count] = 0;
}
static struct pci_dev *pci_get_domain_bus_and_slot(int domain, unsigned bus, unsigned devfn)
{
    assert(domain == 0 && device.refs == 0);
    event('G'); seen_bus = bus; seen_devfn = devfn;
    if (missing) return NULL;
    device.refs++;
    return &device;
}
static bool pci_cfg_access_trylock(struct pci_dev *p)
{
    assert(p == &device && p->refs == 1 && !p->locked);
    event('L');
    if (busy) return false;
    p->locked = 1;
    return true;
}
static void pci_cfg_access_unlock(struct pci_dev *p)
{
    assert(p == &device && p->refs == 1 && p->locked);
    event('U'); p->locked = 0;
}
static void pci_dev_put(struct pci_dev *p)
{
    assert(p == &device && p->refs == 1 && !p->locked);
    event('P'); p->refs--;
}
static int pcibios_err_to_errno(int status)
{
    converted_status = status;
    /* The real implementation comes from linux/pci.h. */
    return status == 0x86 ? -ENODEV : status == 0x88 ? -EIO : status;
}
#define CONFIG_API(name, type) \
static int pci_read_config_##name(struct pci_dev *p, int off, type *value) \
{ \
    assert(p == &device && p->refs == 1 && p->locked); \
    event('R'); seen_offset = off; seen_width = sizeof(type); \
    *value = (type)0xfedcba98u; return api_status; \
} \
static int pci_write_config_##name(struct pci_dev *p, int off, type value) \
{ \
    assert(p == &device && p->refs == 1 && p->locked); \
    event('W'); seen_offset = off; seen_width = sizeof(type); \
    seen_value = value; return api_status; \
}
CONFIG_API(byte, u8)
CONFIG_API(word, u16)
CONFIG_API(dword, u32)

#if OCTOOL_TEST_ACPI
static unsigned ec_calls;
static u8 ec_index, ec_value;
static int ec_status;
static int ec_read(u8 index, u8 *value)
{
    ec_calls++; ec_index = index;
    *value = 0xa5; /* Poison output even on failure: adapter must discard it. */
    return ec_status;
}
static int ec_write(u8 index, u8 value)
{
    ec_calls++; ec_index = index; ec_value = value;
    return ec_status;
}
#endif

#include "../kmod/octool_bus_access.h"

static void reset(void)
{
    assert(!device.refs && !device.locked);
    event_count = 0; events[0] = 0;
    missing = busy = false;
    api_status = 0; converted_status = -999;
}

int main(void)
{
    const u64 invalid[][5] = {
        {256,0,0,0,4}, {0,32,0,0,4}, {0,0,8,0,4}, {0,0,0,256,4},
        {0,0,0,1,2}, {0,0,0,2,4}, {0,0,0,0,0}, {0,0,0,0,3}, {0,0,0,0,8},
        {UINT64_MAX,0,0,0,4}, {0,UINT64_MAX,0,0,4}, {0,0,UINT64_MAX,0,4},
        {0,0,0,UINT64_MAX,4}, {0,0,0,0,UINT64_MAX}
    };
    const u64 sentinel = 0xabcdef0123456789ULL;
    u64 result = sentinel;
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        const u64 *f = invalid[i];
        reset();
        assert(octool_pci_access(f[0], f[1], f[2], f[3], f[4], 0, false, &result) == -EINVAL);
        assert(octool_pci_access(f[0], f[1], f[2], f[3], f[4], 0, true, &result) == -EINVAL);
        assert(!event_count && result == sentinel);
    }
    for (unsigned width = 1; width <= 4; width *= 2) {
        const unsigned off = 256 - width;
        const u32 mask = width == 4 ? UINT32_MAX : (1u << (width * 8)) - 1;
        reset();
        assert(octool_pci_access(255, 31, 7, off, width, 0, false, &result) == 0);
        assert(!strcmp(events, "GLRUP") && result == (0xfedcba98u & mask));
        assert(seen_bus == 255 && seen_devfn == 255 && seen_offset == off && seen_width == width);
        assert(converted_status == 0);
        reset(); result = sentinel;
        assert(octool_pci_access(255, 31, 7, off, width, sentinel, true, &result) == 0);
        assert(!strcmp(events, "GLWUP") && seen_value == ((u32)sentinel & mask));
        assert(seen_offset == off && seen_width == width && result == sentinel);
        for (unsigned writing = 0; writing < 2; ++writing) {
            reset(); missing = true;
            assert(octool_pci_access(0,0,0,0,width,0,writing,&result) == -ENODEV);
            assert(!strcmp(events, "G") && result == sentinel);
            reset(); busy = true;
            assert(octool_pci_access(0,0,0,0,width,0,writing,&result) == -EBUSY);
            assert(!strcmp(events, "GLP") && result == sentinel);
            const int errors[] = {0x86, 0x88, -EACCES};
            const int expected[] = {-ENODEV, -EIO, -EACCES};
            for (unsigned n = 0; n < 3; ++n) {
                reset(); api_status = errors[n];
                assert(octool_pci_access(0,0,0,0,width,0,writing,&result) == expected[n]);
                assert(converted_status == errors[n] && result == sentinel);
                assert(!strcmp(events, writing ? "GLWUP" : "GLRUP"));
            }
        }
    }
    reset();
    assert(octool_ec_access(256,0,false,&result) == -EINVAL);
    assert(octool_ec_access(UINT64_MAX,0,false,&result) == -EINVAL);
    assert(octool_ec_access(0,256,true,&result) == -EINVAL);
    assert(octool_ec_access(0,UINT64_MAX,true,&result) == -EINVAL);
#if OCTOOL_TEST_ACPI
    assert(ec_calls == 0);
    assert(octool_ec_access(255,0,false,&result) == 0 && result == 0xa5 && ec_index == 255);
    assert(octool_ec_access(255,0xff,true,&result) == 0 && ec_index == 255 && ec_value == 255);
    const int errors[] = {-ENODEV, -ETIMEDOUT, -EIO};
    for (unsigned n = 0; n < 3; ++n) {
        result = sentinel; ec_status = errors[n];
        assert(octool_ec_access(0,0,false,&result) == errors[n] && result == sentinel);
        assert(octool_ec_access(0,0,true,&result) == errors[n] && result == sentinel);
    }
    assert(ec_calls == 8);
#else
    assert(octool_ec_access(0,0,false,&result) == -EOPNOTSUPP && result == sentinel);
    assert(octool_ec_access(0,0,true,&result) == -EOPNOTSUPP && result == sentinel);
#endif
    printf("PASS: production PCI/EC adapters, ownership, blockers and errors (ACPI=%d)\n", OCTOOL_TEST_ACPI);
    return 0;
}
