// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"

namespace octool { namespace core { namespace intel_mchbar {
// Intel 743846-001: existing, enabled Raptor Lake-S MCHBAR only.
const std::uint64_t baseMask = UINT64_C(0x3fffffe0000);
int identify(HardwareSession &session, unsigned cpu, CpuIdentity &identity);
int readContext(HardwareSession &session, std::uint64_t &bar);
} } }
