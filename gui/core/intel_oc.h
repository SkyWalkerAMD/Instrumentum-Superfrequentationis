// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <vector>

namespace octool { namespace core {
enum class IntelOcDomain { Core = 0, Cache = 2 };
enum class IntelOcVoltageMode { Adaptive = 0, Override = 1 };
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
double intelOcTargetMillivolts(std::uint32_t data);
IntelOcVoltageMode intelOcVoltageMode(std::uint32_t data);
// Explicit target and mode for one domain, preserving offset and ratio.
// Integer 1..2000 mV is a profile input range, not a safe operating range.
// Zero/default semantics and VF-point/per-core targets are not implemented.
int encodeIntelOcVoltage(unsigned millivolts, IntelOcVoltageMode mode,
                         std::uint32_t previous, std::uint32_t &encoded);
int encodeIntelOcOffset(double millivolts, std::uint32_t previous,
                        std::uint32_t &encoded);
// Low-byte maximum OC ratio. Restrict to 1..85 using the Raptor Lake-S FSP
// profile's documented maximum. Zero/default semantics are not implemented.
// This is a domain limit, not a per-core/active-core-count turbo table.
int encodeIntelOcRatio(unsigned ratio, std::uint32_t previous, std::uint32_t &encoded);
// Queries submit command 0x10 to MSR 0x150 but never a settings-change command.
// All calls belong inside one HardwareService transaction.
IntelOcSnapshot readIntelOc(HardwareSession &session, unsigned cpu, IntelOcDomain domain);
struct IntelVfPoint {
    unsigned point = 0;
    IntelOcResponse response;
};
struct IntelVfSnapshot {
    int error = 0;
    unsigned cpu = 0, selectedPoint = 0;
    IntelOcDomain domain = IntelOcDomain::Core;
    CpuIdentity identity;
    // True when every requested point completed, even if firmware rejected
    // individual points. A transport failure/deadline stops the scan.
    bool scanCompleted = false;
    std::vector<IntelVfPoint> points;
};
// point 0 queries candidates 1..15; 1..15 queries exactly that point.
// This separate snapshot cannot be passed to the domain-wide setters.
// Only query 0x10 is sent: no VF setting, override, or mode change.
IntelVfSnapshot readIntelVf(HardwareSession &session, unsigned cpu,
                          IntelOcDomain domain, unsigned point = 0);
IntelOcUpdate applyIntelOcOffset(HardwareSession &session, const IntelOcSnapshot &snapshot,
                                double millivolts);
IntelOcUpdate applyIntelOcRatio(HardwareSession &session, const IntelOcSnapshot &snapshot,
                               unsigned ratio);
IntelOcUpdate applyIntelOcVoltage(HardwareSession &session, const IntelOcSnapshot &snapshot,
                                 unsigned millivolts, IntelOcVoltageMode mode);
} }
