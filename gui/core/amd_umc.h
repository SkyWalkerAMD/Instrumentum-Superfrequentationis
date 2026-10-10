// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <vector>

namespace octool { namespace core {
struct UmcField {
    unsigned id, group;
    const char *name;
    std::uint32_t offset;
    unsigned low, high;
};
struct UmcRegister {
    std::uint32_t offset = 0, value = 0;
};
struct UmcValue {
    unsigned id = 0;
    std::uint32_t offset = 0, raw = 0, encoded = 0;
    int error = 0;
};
struct UmcDecode {
    int error = 0;
    std::vector<UmcValue> values;
};
struct UmcTarget {
    unsigned cpu = 0, bus = 0, device = 0, function = 0;
    // Address index, NOT a DIMM label or a physical channel number. No search.
    unsigned bank = 0;
    // Explicit refresh register selection: four blocks, four words per block.
    unsigned refreshSlot = 0;
};
struct UmcSnapshot {
    int error = 0;
    UmcTarget target;
    CpuIdentity identity;
    std::uint32_t pciIdentity = 0;
    std::vector<UmcRegister> registers;
    UmcDecode decoded;
};
const std::vector<UmcField> &amdUmcFields();
const char *amdUmcGroupName(unsigned group);
std::vector<std::uint32_t> amdUmcOffsets(unsigned refreshSlot);
// Original field encodings, without invented cycles/MHz or DDR-generation
// interpretation. Missing registers stay missing; duplicates invalidate input.
UmcDecode decodeAmdUmc(const std::vector<UmcRegister> &registers, unsigned refreshSlot);
// Explicit read of one selected bank. CPUID Family 1Ah plus PCI 1022:153a or
// 1022:14d8, matching original Shimada/Granite routing. This does not certify
// a board/firmware. E0 index writes are required, but E4 is NEVER written.
// No constructor-time access, PCI enumeration, channel search or retries.
UmcSnapshot readAmdUmc(HardwareSession &session, const UmcTarget &target);
} }
