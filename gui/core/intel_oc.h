// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"

namespace octool { namespace core {
enum class IntelOcDomain { Core = 0, Cache = 2 };
struct IntelOcResponse {
    int error = 0;
    bool commandAttempted = false, completed = false;
    unsigned firmwareStatus = 0;
    std::uint32_t data = 0;
};
struct IntelOcSnapshot {
    int error = 0;
    unsigned cpu = 0;
    IntelOcDomain domain = IntelOcDomain::Core;
    CpuIdentity identity;
    bool valid = false, locked = true;
    std::uint64_t flexRatio = 0;
    IntelOcResponse response;
};
enum class IntelOcStage { Validate, Preflight, Compare, Write, Verify, Complete };
struct IntelOcUpdate {
    int error = 0;
    IntelOcStage stage = IntelOcStage::Validate;
    bool writeAttempted = false, verified = false;
    std::uint32_t submitted = 0;
    IntelOcResponse response;
    IntelOcSnapshot readback;
};
// This recovered client profile is deliberately separate from SPR/GNR/NVL.
bool hasIntelOcProfile(const CpuIdentity &identity);
double intelOcOffsetMillivolts(std::uint32_t data);
int encodeIntelOcOffset(double millivolts, std::uint32_t previous,
                        std::uint32_t &encoded);
// Queries submit command 0x10 to MSR 0x150 but never a voltage-change command.
// All calls belong inside one HardwareService transaction.
IntelOcSnapshot readIntelOc(HardwareSession &session, unsigned cpu, IntelOcDomain domain);
IntelOcUpdate applyIntelOcOffset(HardwareSession &session, const IntelOcSnapshot &snapshot,
                                double millivolts);
} }
