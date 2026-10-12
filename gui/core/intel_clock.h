// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <string>
#include <vector>

namespace octool { namespace core {
struct IntelClockRegister { std::uint32_t offset = 0; std::uint64_t raw = 0; };
struct IntelClockRegisterSpec { std::uint32_t offset; int width; };
struct IntelClockSnapshot {
    int error = 0, bclkError = 0, mrcError = 0;
    bool valid = false, offline = true;
    unsigned cpu = 0;
    CpuIdentity identity;
    std::uint32_t pciIdentity = 0, bclkKhz = 0;
    std::uint64_t mchbar = 0;
    unsigned mrcGear = 0, mrcReference = 0, mrcRatio = 0;
    unsigned mrcVddqMillivolts = 0, mrcIccMaxEncoded = 0;
    std::vector<IntelClockRegister> registers;
    std::string detail;
};
// Shared PCU offsets: never add an MC or channel stride.
const std::vector<IntelClockRegisterSpec> &intelClockRegisters();
IntelClockSnapshot decodeIntelClock(const std::vector<IntelClockRegister> &registers);
// Read-only B7/A700 access. Two matching reads are not an atomic measurement.
IntelClockSnapshot readIntelClock(HardwareSession &session, unsigned cpu);
std::string intelClockJson(const IntelClockSnapshot &snapshot);
IntelClockSnapshot parseIntelClockCapture(const std::string &json);
} }
