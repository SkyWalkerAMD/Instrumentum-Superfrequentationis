// SPDX-License-Identifier: GPL-2.0-only
#include "intel_thermal.h"
#include <cerrno>

namespace octool { namespace core {
namespace {
constexpr std::uint64_t offsetMask = UINT64_C(0x3f) << 24;
constexpr std::uint64_t targetLock = UINT64_C(1) << 31;
constexpr std::uint64_t programmable = UINT64_C(1) << 30;
bool validSnapshot(const IntelThermalSnapshot &s) {
    return !s.error && s.valid && hasIntelThermalProfile(s.identity) && intelDefaultTccTemperature(s.raw);
}
}
bool hasIntelThermalProfile(const CpuIdentity &id) {
    return !id.error && id.intel && id.family == 6 &&
        (id.model == 0xb7 || id.model == 0x8f || id.model == 0xcf);
}
unsigned intelDefaultTccTemperature(std::uint64_t raw) { return unsigned((raw >> 16) & 255); }
unsigned intelTccOffset(std::uint64_t raw) { return unsigned((raw >> 24) & 63); }
int intelTccTemperature(std::uint64_t raw) { return int(intelDefaultTccTemperature(raw))-int(intelTccOffset(raw)); }
bool intelThermalEditable(const IntelThermalSnapshot &s) {
    return validSnapshot(s) && (s.platformInfo & programmable) && !(s.raw & targetLock) && intelTccTemperature(s.raw) > 0;
}
int encodeIntelTccOffset(unsigned degrees, std::uint64_t previous, std::uint64_t &encoded) {
    if (degrees > 63 || degrees >= intelDefaultTccTemperature(previous)) return -ERANGE;
    encoded = (previous & ~offsetMask) | (std::uint64_t(degrees) << 24); return 0;
}
IntelThermalSnapshot readIntelThermal(HardwareSession &s, unsigned cpu) {
    IntelThermalSnapshot out; out.cpu = cpu; out.identity = identifyCpu(s,cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelThermalProfile(out.identity) || out.identity.maximum < 6) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu,1);
    if ((out.error = features.error)) return out;
    if (features.words[0] != out.identity.signature) { out.error = -ENODEV; return out; }
    if (!(features.words[3] & (1u << 5)) || (features.words[2] & (1u << 31))) { out.error = -ENOTSUP; return out; }
    const auto thermal = s.cpuid(cpu,6);
    if ((out.error = thermal.error)) return out;
    if (!(thermal.words[0] & 1)) { out.error = -ENOTSUP; return out; }
    const auto platform = s.execute(msrRequest(cpu,0xce));
    if ((out.error = platform.error)) return out;
    out.platformInfo = platform.value;
    const auto target = s.execute(msrRequest(cpu,0x1a2));
    if ((out.error = target.error)) return out;
    out.raw = target.value;
    if (!intelDefaultTccTemperature(out.raw)) { out.error = -ENODATA; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.valid = true; return out;
}
IntelThermalUpdate applyIntelThermal(HardwareSession &s, const IntelThermalSnapshot &old, unsigned degrees) {
    IntelThermalUpdate out;
    if (!validSnapshot(old)) { out.error = -EINVAL; return out; }
    if ((out.error = encodeIntelTccOffset(degrees,old.raw,out.expected))) return out;
    out.expectedValid = true;
    if (!intelThermalEditable(old)) { out.error = -EPERM; return out; }
    const auto fresh = readIntelThermal(s,old.cpu);
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!intelThermalEditable(fresh)) { out.error = -EPERM; return out; }
    if (fresh.raw != old.raw || fresh.platformInfo != old.platformInfo) { out.error = -EAGAIN; return out; }
    if (out.expected == fresh.raw) out.after = fresh;
    else {
        RegisterUpdate update; update.target = msrRequest(old.cpu,0x1a2);
        update.mask = offsetMask; update.bits = out.expected & offsetMask;
        update.expected = old.raw; update.compareMask = UINT64_MAX; update.lockMask = targetLock;
        static_cast<UpdateResult &>(out) = updateRegisters(s,{update});
        if (out.error) return out;
        out.after = readIntelThermal(s,old.cpu);
    }
    if ((out.error = out.after.error)) return out;
    if (out.after.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!intelThermalEditable(out.after) || out.after.platformInfo != old.platformInfo) { out.error = -EAGAIN; return out; }
    if (out.after.raw != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; return out;
}
} }
