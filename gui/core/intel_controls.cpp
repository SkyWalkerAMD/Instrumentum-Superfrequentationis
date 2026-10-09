// SPDX-License-Identifier: GPL-2.0-only
#include "intel_controls.h"
#include <cerrno>
#include <cmath>
#include <limits>

namespace octool { namespace core {
bool hasIntelRaplProfile(const CpuIdentity &id) {
    if (!id.intel || id.family != 6) return false;
    // Linux v6.12 intel_rapl_common.c / intel-family.h, Core RAPL units.
    // No inference from a motherboard name; unlisted models get HWP only.
    switch (id.model) {
    case 0x2a: case 0x2d: case 0x3a: case 0x3e: case 0x3c: case 0x3f:
    case 0x45: case 0x46: case 0x3d: case 0x47: case 0x4f: case 0x56:
    case 0x4e: case 0x5e: case 0x55: case 0x8e: case 0x9e: case 0xa5:
    case 0xa6: case 0x6a: case 0x6c: case 0x7d: case 0x7e: case 0x8c:
    case 0x8d: case 0xa7: case 0x97: case 0x9a: case 0xb7: case 0xba:
    case 0xbf: case 0x8f: case 0xcf: return true;
    default: return false;
    }
}
double raplWindowSeconds(unsigned code, unsigned exponent) {
    if (code > 127 || exponent > 15) return 0;
    return std::ldexp(1.0 + double(code >> 5)/4.0, int(code & 31)-int(exponent));
}
int encodeRaplWindow(double seconds, unsigned exponent, unsigned &code) {
    if (!std::isfinite(seconds) || exponent > 15 || seconds < raplWindowSeconds(0, exponent) ||
        seconds > raplWindowSeconds(127, exponent)) return -ERANGE;
    double best = std::numeric_limits<double>::max();
    // Round down: the selected averaging window never exceeds the request.
    for (unsigned c = 0; c < 128; ++c) {
        const double delta = seconds - raplWindowSeconds(c, exponent);
        if (delta >= 0 && delta < best) { best = delta; code = c; }
    }
    return 0;
}
IntelSnapshot readIntelControls(HardwareSession &s, unsigned cpu) {
    IntelSnapshot out; out.cpu = cpu; out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!out.identity.intel) { out.error = -ENOTSUP; return out; }
    auto add = [&out](const char *name, const char *unit, std::uint32_t msr, const Reply &r,
                     double value, bool edit, IntelField field) {
        IntelReading item; item.name = name; item.unit = unit; item.msr = msr;
        item.error = r.error; item.raw = r.value; item.value = value;
        item.decoded = !r.error; item.editable = edit && !r.error; item.field = field;
        out.readings.push_back(item);
    };
    out.rapl = hasIntelRaplProfile(out.identity);
    if (out.rapl) {
        out.units = s.execute(msrRequest(cpu, 0x606));
        out.powerLimit = s.execute(msrRequest(cpu, 0x610));
        auto value = out.powerLimit;
        if (out.units.error) value.error = out.units.error;
        const unsigned p = unsigned(out.units.value & 15), t = unsigned((out.units.value >> 16) & 15);
        const bool editable = !(out.powerLimit.value & (UINT64_C(1) << 63));
        add("Package PL1", "W", 0x610, value, std::ldexp(double(value.value & 0x7fff), -int(p)), editable, IntelField::Pl1);
        add("Package PL2", "W", 0x610, value, std::ldexp(double((value.value >> 32) & 0x7fff), -int(p)), editable, IntelField::Pl2);
        add("PL1 enabled", "0 / 1", 0x610, value, double((value.value >> 15) & 1), editable, IntelField::Pl1Enable);
        add("PL2 enabled", "0 / 1", 0x610, value, double((value.value >> 47) & 1), editable, IntelField::Pl2Enable);
        add("PL1 clamp", "0 / 1", 0x610, value, double((value.value >> 16) & 1), editable, IntelField::Pl1Clamp);
        add("PL2 clamp", "0 / 1", 0x610, value, double((value.value >> 48) & 1), editable, IntelField::Pl2Clamp);
        add("PL1 averaging window", "s", 0x610, value, raplWindowSeconds(unsigned((value.value >> 17) & 127), t), editable, IntelField::Pl1Window);
        add("PL2 averaging window", "s", 0x610, value, raplWindowSeconds(unsigned((value.value >> 49) & 127), t), editable, IntelField::Pl2Window);
    }
    if (out.identity.maximum >= 6) {
        const auto features = s.cpuid(cpu, 6);
        if (features.error) { out.error = features.error; return out; }
        out.hwp = (features.words[0] & (1u << 7)) != 0;
        out.epp = (features.words[0] & (1u << 10)) != 0;
        if (out.hwp) {
            const auto enable = s.execute(msrRequest(cpu, 0x770));
            out.hwpEnabled = !enable.error && (enable.value & 1);
            out.hwpCapabilities = s.execute(msrRequest(cpu, 0x771));
            out.hwpRequest = s.execute(msrRequest(cpu, 0x774));
            const auto &r = out.hwpRequest;
            const bool editable = out.hwpEnabled && !out.hwpCapabilities.error && !(r.value & (UINT64_C(1) << 42));
            add("HWP minimum", "performance level", 0x774, r, double(r.value & 255), editable, IntelField::HwpMin);
            add("HWP maximum", "performance level", 0x774, r, double((r.value >> 8) & 255), editable, IntelField::HwpMax);
            add("HWP desired (0 = autonomous)", "performance level", 0x774, r, double((r.value >> 16) & 255), editable, IntelField::HwpDesired);
            if (out.epp) add("HWP energy preference", "0..255", 0x774, r, double((r.value >> 24) & 255), editable, IntelField::HwpEpp);
            add("HWP highest capability", "performance level", 0x771, out.hwpCapabilities, double(out.hwpCapabilities.value & 255), false, IntelField::HwpMax);
            add("HWP lowest capability", "performance level", 0x771, out.hwpCapabilities, double((out.hwpCapabilities.value >> 24) & 255), false, IntelField::HwpMin);
        }
        if ((features.words[0] & 1) && hasIntelRaplProfile(out.identity)) {
            const auto target = s.execute(msrRequest(cpu, 0x1a2));
            auto therm = s.execute(msrRequest(cpu, 0x19c));
            if (target.error) therm.error = target.error;
            if (!therm.error && !(therm.value & (UINT64_C(1) << 31))) therm.error = -ENODATA;
            add("Core digital temperature", "C", 0x19c, therm,
                double(int((target.value >> 16) & 255)-int((therm.value >> 16) & 127)), false, IntelField::Pl1);
        }
    }
    return out;
}
UpdateResult applyIntelControl(HardwareSession &s, const IntelSnapshot &old, IntelField field, double value) {
    UpdateResult result;
    auto fail = [&result](int e) { result.error = e; return result; };
    if (!std::isfinite(value) || old.error) return fail(-EINVAL);
    const auto fresh = readIntelControls(s, old.cpu);
    if (fresh.error) return fail(fresh.error);
    if (fresh.identity.signature != old.identity.signature || !fresh.identity.intel) return fail(-ENODEV);
    bool allowed = false;
    for (const auto &item : fresh.readings) if (item.field == field && item.editable) allowed = true;
    if (!allowed) return fail(-EPERM);
    RegisterUpdate u; unsigned shift = 0; std::uint64_t raw = 0;
    if (field <= IntelField::Pl2Window) {
        if (!old.rapl || old.units.error || old.powerLimit.error || old.units.value != fresh.units.value) return fail(-EAGAIN);
        u.target = msrRequest(old.cpu, 0x610); u.expected = old.powerLimit.value;
        u.lockMask = UINT64_C(1) << 63; u.compareMask = UINT64_MAX;
        if (field == IntelField::Pl1 || field == IntelField::Pl2) {
            const double encoded = std::ldexp(value, int(fresh.units.value & 15));
            if (encoded < 1 || encoded > 32767) return fail(-ERANGE);
            raw = std::uint64_t(std::floor(encoded)); shift = field == IntelField::Pl1 ? 0 : 32;
            u.mask = UINT64_C(0x7fff) << shift;
        } else if (field == IntelField::Pl1Window || field == IntelField::Pl2Window) {
            unsigned code = 0;
            const int error = encodeRaplWindow(value, unsigned((fresh.units.value >> 16) & 15), code);
            if (error) return fail(error);
            raw = code; shift = field == IntelField::Pl1Window ? 17 : 49; u.mask = UINT64_C(127) << shift;
        } else {
            if (value != 0 && value != 1) return fail(-ERANGE);
            raw = std::uint64_t(value);
            shift = field == IntelField::Pl1Enable ? 15 : field == IntelField::Pl2Enable ? 47 : field == IntelField::Pl1Clamp ? 16 : 48;
            u.mask = UINT64_C(1) << shift;
        }
    } else {
        if (!old.hwp || old.hwpRequest.error || old.hwpCapabilities.error ||
            old.hwpCapabilities.value != fresh.hwpCapabilities.value) return fail(-EAGAIN);
        if (value < 0 || value > 255 || value != std::floor(value)) return fail(-ERANGE);
        raw = std::uint64_t(value);
        const unsigned lo = unsigned((fresh.hwpCapabilities.value >> 24) & 255), hi = unsigned(fresh.hwpCapabilities.value & 255);
        if (lo > hi) return fail(-ERANGE);
        if (field != IntelField::HwpEpp && !(field == IntelField::HwpDesired && raw == 0) && (raw < lo || raw > hi)) return fail(-ERANGE);
        shift = field == IntelField::HwpMin ? 0 : field == IntelField::HwpMax ? 8 : field == IntelField::HwpDesired ? 16 : 24;
        u.mask = UINT64_C(255) << shift; u.target = msrRequest(old.cpu, 0x774);
        u.expected = old.hwpRequest.value; u.compareMask = UINT64_MAX;
        const auto next = (fresh.hwpRequest.value & ~u.mask) | (raw << shift);
        const auto minimum = next & 255, maximum = (next >> 8) & 255, desired = (next >> 16) & 255;
        if (minimum > maximum || (desired && (desired < minimum || desired > maximum))) return fail(-ERANGE);
    }
    u.bits = raw << shift;
    return updateRegisters(s, {u});
}
} }
