// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_oc.h"

namespace octool { namespace core {
struct IntelBclkVoltageSnapshot {
    int error = 0;
    unsigned cpu = 0;
    CpuIdentity identity;
    bool valid = false;
    std::uint64_t flexRatio = 0;
    IntelOcResponse response;
};
struct IntelBclkVoltageUpdate {
    int error = 0;
    IntelOcStage stage = IntelOcStage::Validate;
    bool writeAttempted = false, verified = false, unchanged = false, expectedValid = false;
    std::uint32_t expected = 0;
    IntelOcResponse response;
    IntelBclkVoltageSnapshot after;
};
bool intelBclkVoltageEnabled(std::uint32_t word);
int encodeIntelBclkVoltage(unsigned enabled, std::uint32_t previous, std::uint32_t &encoded);
bool intelBclkVoltageEditable(const IntelBclkVoltageSnapshot &snapshot);
// Raptor Lake-S 06_B7, bare metal with MSR capability. BCLK-aware
// adaptive voltage is a shared processor configuration, not a voltage reading.
IntelBclkVoltageSnapshot readIntelBclkVoltage(HardwareSession &session, unsigned cpu);
// Query 14 / set 15, positive enable in bit 2. Preserve every other bit.
// Compare identity, OC lock and full previous word before one settings write;
// verify the complete word afterwards. Never retry an uncertain write.
IntelBclkVoltageUpdate applyIntelBclkVoltage(HardwareSession &session,
    const IntelBclkVoltageSnapshot &snapshot, unsigned enabled);
} }
