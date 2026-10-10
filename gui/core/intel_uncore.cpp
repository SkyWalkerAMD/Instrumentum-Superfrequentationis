// SPDX-License-Identifier: GPL-2.0-only
#include "intel_uncore.h"
#include <cerrno>

namespace octool { namespace core {
bool hasIntelUncoreProfile(const CpuIdentity &id) {
    return !id.error && id.intel && id.family == 6 && (id.model == 0xb7 || id.model == 0x8f);
}
unsigned intelUncoreMinimum(std::uint64_t raw) { return unsigned((raw >> 8) & 127); }
unsigned intelUncoreMaximum(std::uint64_t raw) { return unsigned(raw & 127); }
bool intelUncoreEditable(const IntelUncoreSnapshot &s) {
    return !s.error && s.valid && hasIntelUncoreProfile(s.identity) &&
        intelUncoreMinimum(s.raw) && intelUncoreMinimum(s.raw) <= intelUncoreMaximum(s.raw);
}
int encodeIntelUncoreRange(std::uint64_t previous, unsigned minimum,
                          unsigned maximum, std::uint64_t &encoded) {
    if (!minimum || minimum > maximum || maximum > 127) return -ERANGE;
    encoded = (previous & ~UINT64_C(0x7f7f)) | (std::uint64_t(minimum) << 8) | maximum;
    return 0;
}
IntelUncoreSnapshot readIntelUncore(HardwareSession &s, unsigned cpu) {
    IntelUncoreSnapshot out; out.cpu = cpu; out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelUncoreProfile(out.identity)) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    // Match the Linux uncore driver's hypervisor exclusion. A virtual MSR
    // implementation is not evidence of the physical platform contract.
    if (!(features.words[3] & (1u << 5)) || (features.words[2] & (1u << 31))) {
        out.error = -ENOTSUP; return out;
    }
    const auto value = s.execute(msrRequest(cpu, 0x620));
    if ((out.error = value.error)) return out;
    if ((out.error = s.checkpoint())) return out;
    out.raw = value.value; out.valid = true; return out;
}
IntelUncoreUpdate applyIntelUncoreRange(HardwareSession &s,
    const IntelUncoreSnapshot &old, unsigned minimum, unsigned maximum) {
    IntelUncoreUpdate out;
    if (!intelUncoreEditable(old)) { out.error = -EINVAL; return out; }
    if ((out.error = encodeIntelUncoreRange(old.raw, minimum, maximum, out.expected))) return out;
    const auto fresh = readIntelUncore(s, old.cpu);
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (fresh.raw != old.raw) { out.error = -EAGAIN; return out; }
    if (out.expected == old.raw) out.after = fresh;
    else {
        RegisterUpdate update; update.target = msrRequest(old.cpu, 0x620);
        update.mask = UINT64_C(0x7f7f); update.bits = out.expected & update.mask;
        update.expected = old.raw; update.compareMask = UINT64_MAX;
        static_cast<UpdateResult &>(out) = updateRegisters(s, {update});
        if (out.error) return out;
        out.after = readIntelUncore(s, old.cpu);
    }
    if ((out.error = out.after.error)) return out;
    if (out.after.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (out.after.raw != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; return out;
}
} }
