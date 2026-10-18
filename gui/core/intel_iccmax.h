// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_oc.h"

namespace octool { namespace core {
struct IntelIccMaxSnapshot {
    int error = 0;
    unsigned cpu = 0, address = 0;
    CpuIdentity identity;
    bool valid = false;
    std::uint64_t flexRatio = 0;
    IntelOcResponse topology, response;
};
struct IntelIccMaxUpdate {
    int error = 0;
    bool writeAttempted = false, verified = false, unchanged = false;
    bool expectedValid = false;
    std::uint32_t expected = 0;
    IntelOcResponse response;
    IntelIccMaxSnapshot after;
};
// Exact Alder Lake signatures documented in the pinned CPU/VR tables. This
// is the shared IA rail (P cores, E cores and ring), not a per-core control.
bool hasIntelIccMaxProfile(const CpuIdentity &identity);
double intelIccMaxAmps(std::uint32_t data);
bool intelIccMaxModeBit31(std::uint32_t data);
bool intelIccMaxEditable(const IntelIccMaxSnapshot &snapshot);
// Policy range 1..511.75 A, exact 0.25 A steps. No rounding, automatic
// bit-31 changes, legacy MSR 601 write or zero/default encoding.
int encodeIntelIccMax(double amps, std::uint32_t previous, std::uint32_t &encoded);
IntelIccMaxSnapshot readIntelIccMax(HardwareSession &session, unsigned cpu);
IntelIccMaxUpdate applyIntelIccMax(HardwareSession &session,
                                 const IntelIccMaxSnapshot &snapshot, double amps);
} }
