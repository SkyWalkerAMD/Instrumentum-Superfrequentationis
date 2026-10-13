// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <array>
#include <string>
namespace octool { namespace core {
struct IntelLimitReason { const char *key, *label; unsigned bit; };
const std::array<IntelLimitReason,11> &intelLimitReasons();
std::uint64_t intelLimitLogMask(const std::string &reason);
struct IntelLimitsSnapshot {
    int error=0; unsigned cpu=0; CpuIdentity identity;
    bool valid=false; std::uint64_t raw=0;
};
struct IntelLimitsClear {
    int error=0;
    bool writeAttempted=false, verified=false, unchanged=false;
    std::uint64_t selectedMask=0, submitted=0, stillSetMask=0;
    IntelLimitsSnapshot after;
};
// 06_B7, MSR support, bare metal. Package snapshot through selected CPU.
bool intelLimitsValid(const IntelLimitsSnapshot &snapshot);
IntelLimitsSnapshot readIntelLimits(HardwareSession &session,unsigned cpu);
// W0C history flags; live status flags may change during the operation.
// Unselected known log flags are written as ones to preserve new events.
// Reasserted/uncleared selected logs return EAGAIN without retry.
IntelLimitsClear clearIntelLimitLogs(HardwareSession &session,
    const IntelLimitsSnapshot &snapshot,const std::string &reason);
} }
