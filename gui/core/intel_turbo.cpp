// SPDX-License-Identifier: GPL-2.0-only
#include "intel_turbo.h"
#include <cerrno>

namespace octool { namespace core {
namespace {
bool validKind(IntelTurboKind kind) {
    return kind == IntelTurboKind::Primary || kind == IntelTurboKind::Secondary;
}
bool sameContext(const IntelTurboSnapshot &a, const IntelTurboSnapshot &b) {
    return a.identity.signature == b.identity.signature && a.hybrid == b.hybrid &&
        a.platformInfo == b.platformInfo && a.flexRatio == b.flexRatio && a.coreCounts == b.coreCounts;
}
}
bool hasIntelTurboProfile(const CpuIdentity &id) {
    return !id.error && id.intel && id.family == 6 && id.model == 0xb7;
}
std::uint32_t intelTurboRatioMsr(IntelTurboKind kind) {
    return kind == IntelTurboKind::Primary ? 0x1ad : kind == IntelTurboKind::Secondary ? 0x650 : 0;
}
std::uint32_t intelTurboCountMsr(IntelTurboKind kind) {
    return kind == IntelTurboKind::Primary ? 0x1ae : kind == IntelTurboKind::Secondary ? 0x651 : 0;
}
unsigned intelTurboByte(std::uint64_t word, unsigned group) {
    return group < 8 ? unsigned((word >> (group * 8)) & 255) : 0;
}
bool validIntelTurboLayout(std::uint64_t ratios, std::uint64_t counts) {
    unsigned previousCount = 0, previousRatio = 255;
    for (unsigned group = 0; group < 8; ++group) {
        const unsigned count = intelTurboByte(counts, group), ratio = intelTurboByte(ratios, group);
        if (!count) continue;
        if (count <= previousCount || !ratio || ratio > previousRatio) return false;
        previousCount = count; previousRatio = ratio;
    }
    return previousCount != 0;
}
int encodeIntelTurboRatio(const IntelTurboSnapshot &old, unsigned group, unsigned ratio, std::uint64_t &encoded) {
    if (group >= 8 || !ratio || ratio > 85) return -ERANGE;
    if (!validIntelTurboLayout(old.ratios, old.coreCounts) || !intelTurboByte(old.coreCounts, group)) return -EINVAL;
    const unsigned shift = group * 8;
    const auto next = (old.ratios & ~(UINT64_C(255) << shift)) | (std::uint64_t(ratio) << shift);
    if (!validIntelTurboLayout(next, old.coreCounts)) return -ERANGE;
    encoded = next; return 0;
}
IntelTurboSnapshot readIntelTurbo(HardwareSession &s, unsigned cpu, IntelTurboKind kind) {
    IntelTurboSnapshot out; out.cpu = cpu; out.kind = kind;
    if (!validKind(kind)) { out.error = -EINVAL; return out; }
    out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelTurboProfile(out.identity) || out.identity.maximum < 7) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (!(features.words[3] & (1u << 5))) { out.error = -ENOTSUP; return out; }
    const auto extended = s.cpuid(cpu, 7, 0);
    if ((out.error = extended.error)) return out;
    out.hybrid = (extended.words[3] & (1u << 15)) != 0;
    if (kind == IntelTurboKind::Secondary && !out.hybrid) { out.error = -ENOTSUP; return out; }
    const std::uint32_t addresses[] = {0xce, 0x194, intelTurboRatioMsr(kind), intelTurboCountMsr(kind)};
    std::uint64_t *values[] = {&out.platformInfo, &out.flexRatio, &out.ratios, &out.coreCounts};
    for (unsigned i = 0; i < 4; ++i) {
        const auto r = s.execute(msrRequest(cpu, addresses[i]));
        if ((out.error = r.error)) return out;
        *values[i] = r.value;
    }
    if ((out.error = s.checkpoint())) return out;
    out.programmable = (out.platformInfo & (UINT64_C(1) << 28)) != 0;
    out.locked = (out.flexRatio & (UINT64_C(1) << 20)) != 0;
    out.layoutValid = validIntelTurboLayout(out.ratios, out.coreCounts);
    out.valid = true; return out;
}
IntelTurboUpdate applyIntelTurboRatio(HardwareSession &s, const IntelTurboSnapshot &old, unsigned group, unsigned ratio) {
    IntelTurboUpdate out;
    if (old.error || !old.valid || !hasIntelTurboProfile(old.identity) || !validKind(old.kind)) {
        out.error = -EINVAL; return out;
    }
    if ((out.error = encodeIntelTurboRatio(old, group, ratio, out.expected))) return out;
    // Derive permission from the raw snapshot too; callers cannot override it
    // just by changing the presentation flags.
    if (!(old.platformInfo & (UINT64_C(1) << 28)) || (old.flexRatio & (UINT64_C(1) << 20))) {
        out.error = -EPERM; return out;
    }
    const auto fresh = readIntelTurbo(s, old.cpu, old.kind);
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!fresh.programmable || fresh.locked) { out.error = -EPERM; return out; }
    if (!sameContext(old, fresh) || fresh.ratios != old.ratios) { out.error = -EAGAIN; return out; }
    if (out.expected == old.ratios) out.after = fresh;
    else {
        RegisterUpdate update; update.target = msrRequest(old.cpu, intelTurboRatioMsr(old.kind));
        update.mask = UINT64_C(255) << (group * 8); update.bits = std::uint64_t(ratio) << (group * 8);
        update.expected = old.ratios; update.compareMask = UINT64_MAX;
        static_cast<UpdateResult &>(out) = updateRegisters(s, {update});
        if (out.error) return out;
        out.after = readIntelTurbo(s, old.cpu, old.kind);
    }
    if ((out.error = out.after.error)) return out;
    if (out.after.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!sameContext(old, out.after) || !out.after.programmable || out.after.locked) { out.error = -EAGAIN; return out; }
    if (out.after.ratios != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; return out;
}
} }
