// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"

namespace octool { namespace core {
enum class IntelEnergyField { EfficientTurbo, RaceToHalt };
struct IntelEnergySnapshot {
    int error = 0;
    unsigned cpu = 0;
    CpuIdentity identity;
    bool valid = false;
    std::uint64_t raw = 0;
};
struct IntelEnergyUpdate : UpdateResult {
    bool verified = false, unchanged = false, expectedValid = false;
    std::uint64_t expected = 0;
    IntelEnergySnapshot after;
};
// Raptor Lake-S (06_B7), bare metal with MSR support. Access through the
// selected logical CPU; do not infer that this changes every core/package.
bool hasIntelEnergyProfile(const CpuIdentity &identity);
bool intelEnergyEditable(const IntelEnergySnapshot &snapshot);
bool intelEfficientTurbo(std::uint64_t raw);
bool intelRaceToHalt(std::uint64_t raw);
// Public values are enabled=0/1; the hardware fields are disable bits.
int encodeIntelEnergy(IntelEnergyField field, unsigned enabled,
                      std::uint64_t previous, std::uint64_t &encoded);
IntelEnergySnapshot readIntelEnergy(HardwareSession &session, unsigned cpu);
// One MSR 1FC write, full old-value comparison and full readback. Other
// power/thermal controls are preserved. No retry or automatic rollback.
IntelEnergyUpdate applyIntelEnergy(HardwareSession &session,
    const IntelEnergySnapshot &snapshot, IntelEnergyField field, unsigned enabled);
} }
