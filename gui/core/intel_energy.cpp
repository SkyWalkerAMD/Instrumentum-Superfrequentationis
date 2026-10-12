// SPDX-License-Identifier: GPL-2.0-only
#include "intel_energy.h"
#include <cerrno>

namespace octool { namespace core {
namespace {
std::uint64_t fieldMask(IntelEnergyField field) {
    switch (field) {
    case IntelEnergyField::EfficientTurbo: return UINT64_C(1) << 19;
    case IntelEnergyField::RaceToHalt: return UINT64_C(1) << 20;
    }
    return 0;
}
}
bool hasIntelEnergyProfile(const CpuIdentity &id) {
    return !id.error && id.intel && id.family == 6 && id.model == 0xb7;
}
bool intelEnergyEditable(const IntelEnergySnapshot &s) {
    return !s.error && s.valid && hasIntelEnergyProfile(s.identity);
}
bool intelEfficientTurbo(std::uint64_t raw) { return !(raw & (UINT64_C(1) << 19)); }
bool intelRaceToHalt(std::uint64_t raw) { return !(raw & (UINT64_C(1) << 20)); }
int encodeIntelEnergy(IntelEnergyField field, unsigned enabled,
                      std::uint64_t previous, std::uint64_t &encoded) {
    const auto mask = fieldMask(field);
    if (!mask) return -EINVAL;
    if (enabled > 1) return -ERANGE;
    encoded = enabled ? previous & ~mask : previous | mask;
    return 0;
}
IntelEnergySnapshot readIntelEnergy(HardwareSession &s, unsigned cpu) {
    IntelEnergySnapshot out; out.cpu = cpu; out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelEnergyProfile(out.identity)) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (features.words[0] != out.identity.signature) { out.error = -ENODEV; return out; }
    if (!(features.words[3] & (1u << 5)) || (features.words[2] & (1u << 31))) {
        out.error = -ENOTSUP; return out;
    }
    const auto value = s.execute(msrRequest(cpu, 0x1fc));
    if ((out.error = value.error)) return out;
    if ((out.error = s.checkpoint())) return out;
    out.raw = value.value; out.valid = true; return out;
}
IntelEnergyUpdate applyIntelEnergy(HardwareSession &s, const IntelEnergySnapshot &old,
                                   IntelEnergyField field, unsigned enabled) {
    IntelEnergyUpdate out;
    if (!intelEnergyEditable(old)) { out.error = -EINVAL; return out; }
    if ((out.error = encodeIntelEnergy(field, enabled, old.raw, out.expected))) return out;
    out.expectedValid = true;
    const auto fresh = readIntelEnergy(s, old.cpu);
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (fresh.raw != old.raw) { out.error = -EAGAIN; return out; }
    if (out.expected == old.raw) out.after = fresh;
    else {
        RegisterUpdate update; update.target = msrRequest(old.cpu, 0x1fc);
        update.mask = fieldMask(field); update.bits = out.expected & update.mask;
        update.expected = old.raw; update.compareMask = UINT64_MAX;
        static_cast<UpdateResult &>(out) = updateRegisters(s, {update});
        if (out.error) return out;
        out.after = readIntelEnergy(s, old.cpu);
    }
    if ((out.error = out.after.error)) return out;
    if (out.after.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (out.after.raw != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; return out;
}
} }
