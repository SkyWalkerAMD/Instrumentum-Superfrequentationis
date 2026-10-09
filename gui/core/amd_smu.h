// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <array>

namespace octool { namespace core {
enum class SmuProfile { None, Shimada, Phoenix, Gpt };
struct SmuTarget {
    unsigned cpu = 0, bus = 0, device = 0, function = 0;
    SmuProfile profile = SmuProfile::None;
};
struct SmuProbe {
    int error = 0;
    CpuIdentity identity;
    std::uint32_t pciIdentity = 0;
    SmuTarget target;
};
struct SmuCommand {
    std::uint32_t message = 0;
    std::array<std::uint32_t, 6> args{};
};
struct SmuReply {
    int error = 0;
    std::uint32_t response = 0;
    bool messageAttempted = false;
    unsigned completedCommands = 0;
    std::array<std::uint32_t, 6> args{};
};
// Original-program BIOS SMU profiles; NOT the RSMU or MP1 mailboxes used by
// other tools. Probe never writes. Each send rechecks CPU and PCI identity.
SmuProbe probeSmu(HardwareSession &session, const SmuTarget &target);
bool allowedSmuMessage(SmuProfile profile, std::uint32_t message);
SmuReply sendSmu(HardwareSession &session, const SmuTarget &target, const SmuCommand &command);
// Original Shimada per-core format: CCD[31:28], core-in-CCD[23:20], MHz[19:0].
// These are firmware indices, NEVER inferred from Linux logical CPU numbers.
int encodeShimadaCoreFrequency(unsigned ccd, unsigned core, unsigned mhz, std::uint32_t &argument);
} }
