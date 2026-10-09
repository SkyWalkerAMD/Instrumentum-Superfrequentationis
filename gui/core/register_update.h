// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hardware.h"
#include <vector>

namespace octool { namespace core {
struct RegisterUpdate {
    Request target; // read request, value must be zero
    std::uint64_t mask = 0, bits = 0;
    std::uint64_t expected = 0, compareMask = 0;
    std::uint64_t lockMask = 0;
};
struct UpdateResult {
    int error = 0;
    unsigned completed = 0, failedIndex = 0;
    bool writeAttempted = false;
    std::vector<std::uint64_t> submitted;
};
// Only ordinary readable configuration registers belong here: no W1C, FIFO,
// write-only or command registers. Preflight the WHOLE plan before writing.
// No rollback/replay. On a failed write its hardware completion is unknown.
UpdateResult updateRegisters(HardwareSession &session, const std::vector<RegisterUpdate> &plan);
struct CpuIdentity {
    int error = 0;
    bool intel = false, amd = false;
    unsigned family = 0, model = 0, stepping = 0;
    std::uint32_t maximum = 0, signature = 0;
};
CpuIdentity identifyCpu(HardwareSession &session, unsigned cpu);
Request msrRequest(unsigned cpu, std::uint32_t index);
} }
