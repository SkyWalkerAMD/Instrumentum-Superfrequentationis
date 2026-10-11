// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <string>
#include <vector>

namespace octool { namespace core {
struct IntelMemoryTarget { unsigned cpu = 0, controller = 0, channel = 0; };
struct IntelMemoryRegister {
    std::uint32_t offset = 0;
    std::uint64_t raw = 0;
};
struct IntelMemoryRegisterSpec { std::uint32_t offset; int width; };
struct IntelMemoryField {
    const char *name;
    std::uint32_t offset;
    unsigned low, high;
    // Bit 0: DDR4, bit 1: DDR5.
    unsigned dramTypes;
};
struct IntelMemoryValue {
    unsigned encoded = 0;
    int error = 0;
};
struct IntelMemorySnapshot {
    int error = 0;
    bool valid = false, offline = true;
    IntelMemoryTarget target;
    CpuIdentity identity;
    std::uint32_t pciIdentity = 0;
    std::uint64_t mchbar = 0;
    unsigned dramType = 0, rankMask = 0, commandStretch = 0, nTo1 = 0;
    bool gear2 = false, gear4 = false, noGear2Divide = false, noGear4Divide = false;
    std::vector<IntelMemoryRegister> registers;
    std::vector<IntelMemoryValue> values;
    std::string detail;
};
const std::vector<IntelMemoryRegisterSpec> &intelMemoryRegisters();
const std::vector<IntelMemoryField> &intelMemoryFields();
// MC0/channel0-relative offsets. Shared D800 has no channel offset; D80C uses
// +4 for channel 1, while channel timing/configuration registers use +0x800.
std::uint32_t intelMemoryOffset(const IntelMemoryTarget &target, std::uint32_t offset);
IntelMemorySnapshot decodeIntelMemory(const IntelMemoryTarget &target, const std::vector<IntelMemoryRegister> &registers);
// Explicit one-channel read, never a write. CPUID 06_B7 and PCI 8086:A700 at
// 0000:00:00.0 only. Two complete matching reads do not promise atomicity.
IntelMemorySnapshot readIntelMemory(HardwareSession &session, const IntelMemoryTarget &target);
// Exact hex strings preserve 64-bit register words through GUI/CLI JSON.
std::string intelMemoryJson(const IntelMemorySnapshot &snapshot);
IntelMemorySnapshot parseIntelMemoryCapture(const std::string &json);
} }
