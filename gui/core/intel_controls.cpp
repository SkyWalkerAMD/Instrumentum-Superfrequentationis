// SPDX-License-Identifier: GPL-2.0-only
#include "intel_controls.h"
#include <cerrno>
#include <cmath>
#include <limits>

namespace octool { namespace core {
namespace {
int compareHwpContext(const IntelSnapshot &old, const IntelSnapshot &fresh) {
    if (old.hwp != fresh.hwp || old.hwpEnabled != fresh.hwpEnabled ||
        old.epp != fresh.epp || old.hwpActivityWindow != fresh.hwpActivityWindow ||
        old.hwpPackage != fresh.hwpPackage || old.hwpFlexible != fresh.hwpFlexible ||
        old.hwpPeci != fresh.hwpPeci || old.hwpControl != fresh.hwpControl) return -EAGAIN;
    for (bool peci : {true, false}) {
        if (!(peci ? old.hwpPeci : old.hwpControl)) continue;
        const auto &a = peci ? old.hwpPeciRequest : old.hwpControlValue;
        const auto &b = peci ? fresh.hwpPeciRequest : fresh.hwpControlValue;
        if (a.error) return a.error;
        if (b.error) return b.error;
        if (a.value != b.value) return -EAGAIN;
    }
    return 0;
}
int comparePackageHwpContext(const IntelSnapshot &old, const IntelSnapshot &fresh) {
    if (const int error = compareHwpContext(old, fresh)) return error;
    if (!old.hwpPackage || !fresh.hwpPackage) return -EAGAIN;
    if (old.hwpRequest.error) return old.hwpRequest.error;
    if (fresh.hwpRequest.error) return fresh.hwpRequest.error;
    // Keep the selected CPU's request and selection flags stable as well as
    // the shared PECI/polarity context. Other CPUs and the OS can still race;
    // this is a verified stored hint, never an atomic package-wide policy.
    return old.hwpRequest.value == fresh.hwpRequest.value ? 0 : -EAGAIN;
}
IntelField hwpLocalField(IntelField field) {
    switch (field) {
    case IntelField::HwpPackageMin: return IntelField::HwpMin;
    case IntelField::HwpPackageMax: return IntelField::HwpMax;
    case IntelField::HwpPackageDesired: return IntelField::HwpDesired;
    case IntelField::HwpPackageEpp: return IntelField::HwpEpp;
    case IntelField::HwpPackageActivityWindow: return IntelField::HwpActivityWindow;
    default: return field;
    }
}
}
bool isIntelHwpPackageField(IntelField field) {
    return field >= IntelField::HwpPackageMin && field <= IntelField::HwpPackageActivityWindow;
}
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
std::uint32_t hwpActivityWindowMicroseconds(unsigned code) {
    if (code > 1023) return 0;
    std::uint32_t value = code & 127;
    for (unsigned i = 0; i < (code >> 7); ++i) value *= 10;
    return value;
}
int encodeHwpActivityWindow(std::uint32_t microseconds, unsigned &code) {
    if (microseconds > 1270000000u) return -ERANGE;
    // Intel SDM 16.4.4: 7-bit mantissa * 10^(3-bit exponent), in us.
    // Zero requests the hardware-selected window; otherwise round down.
    unsigned selected = 0; std::uint32_t best = 0;
    for (unsigned c = 1; c < 1024; ++c) {
        const auto candidate = hwpActivityWindowMicroseconds(c);
        if (candidate > best && candidate <= microseconds) { best = candidate; selected = c; }
    }
    code = selected; return 0;
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
    auto addHwpStatus = [&] {
        if (!out.hwp) return;
        // Architectural HWP history, not present performance or a limit cause.
        // SDM Table 2-2: bits 0 and 2 require CPUID.06H:EAX[7], independently
        // of the interrupt/notification capability in bit 8. Never clear here.
        const auto status = s.execute(msrRequest(cpu, 0x777));
        add("HWP guaranteed performance change history", "0 / 1", 0x777, status,
            double(status.value & 1), false, IntelField::HwpMin);
        add("HWP excursion to minimum history", "0 / 1", 0x777, status,
            double((status.value >> 2) & 1), false, IntelField::HwpMin);
        if (out.hwpHighestChange) add("HWP highest performance change history", "0 / 1", 0x777, status,
            double((status.value >> 3) & 1), false, IntelField::Pl1);
        if (out.hwpPeci) {
            add("HWP PECI override entry history", "0 / 1", 0x777, status,
                double((status.value >> 4) & 1), false, IntelField::Pl1);
            add("HWP PECI override exit history", "0 / 1", 0x777, status,
                double((status.value >> 5) & 1), false, IntelField::Pl1);
        }
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
        out.hwpActivityWindow = (features.words[0] & (1u << 9)) != 0;
        out.hwpPackage = (features.words[0] & (1u << 11)) != 0;
        out.hwpFlexible = (features.words[0] & (1u << 17)) != 0;
        out.hwpPeci = (features.words[0] & (1u << 16)) != 0;
        out.hwpControl = (features.words[0] & (1u << 22)) != 0;
        out.hwpNotifications = (features.words[0] & (1u << 8)) != 0;
        out.hwpHighestChange = (features.words[0] & (1u << 15)) != 0;
        if (out.hwp) {
            const auto enable = s.execute(msrRequest(cpu, 0x770));
            out.hwpEnabled = !enable.error && (enable.value & 1);
            out.hwpCapabilities = s.execute(msrRequest(cpu, 0x771));
            out.hwpRequest = s.execute(msrRequest(cpu, 0x774));
            if (out.hwpPackage) out.hwpPackageRequest = s.execute(msrRequest(cpu, 0x772));
            if (out.hwpPeci) out.hwpPeciRequest = s.execute(msrRequest(cpu, 0x775));
            if (out.hwpControl) {
                // SDM 16.4.4.4: accessing 776 before HWP is enabled faults.
                if (!out.hwpEnabled) out.hwpControlValue.error = enable.error ? enable.error : -EPERM;
                else out.hwpControlValue = s.execute(msrRequest(cpu, 0x776));
            }
            const auto &r = out.hwpRequest;
            const bool package = bool(r.value & (UINT64_C(1) << 42)) !=
                bool(out.hwpControl && (out.hwpControlValue.value & 1));
            const bool editable = out.hwpEnabled && !out.hwpCapabilities.error && !package &&
                (!out.hwpPeci || !out.hwpPeciRequest.error) && (!out.hwpControl || !out.hwpControlValue.error);
            const auto overridden = [&](unsigned bit) {
                return out.hwpPeci && (out.hwpPeciRequest.value & (UINT64_C(1) << bit));
            };
            add("HWP minimum", "performance level", 0x774, r, double(r.value & 255), editable && !overridden(63), IntelField::HwpMin);
            add("HWP maximum", "performance level", 0x774, r, double((r.value >> 8) & 255), editable && !overridden(62), IntelField::HwpMax);
            add("HWP desired (0 = autonomous)", "performance level", 0x774, r, double((r.value >> 16) & 255), editable, IntelField::HwpDesired);
            if (out.epp) add("HWP energy preference", "0..255", 0x774, r, double((r.value >> 24) & 255), editable && !overridden(60), IntelField::HwpEpp);
            if (out.hwpActivityWindow) add("HWP activity window (0 = automatic)", "us", 0x774, r,
                hwpActivityWindowMicroseconds(unsigned((r.value >> 32) & 1023)),
                editable && !((r.value >> 16) & 255), IntelField::HwpActivityWindow);
            add("HWP highest capability", "performance level", 0x771, out.hwpCapabilities, double(out.hwpCapabilities.value & 255), false, IntelField::HwpMax);
            add("HWP guaranteed capability", "performance level", 0x771, out.hwpCapabilities, double((out.hwpCapabilities.value >> 8) & 255), false, IntelField::HwpMax);
            add("HWP most efficient capability", "performance level", 0x771, out.hwpCapabilities, double((out.hwpCapabilities.value >> 16) & 255), false, IntelField::HwpMin);
            add("HWP lowest capability", "performance level", 0x771, out.hwpCapabilities, double((out.hwpCapabilities.value >> 24) & 255), false, IntelField::HwpMin);
            // Stored hints and selection flags, not an atomic resolved request
            // or a measurement of the performance delivered by the processor.
            if (out.hwpPackage) {
                const auto &p = out.hwpPackageRequest;
                const bool packageEditable = out.hwpEnabled && !out.hwpCapabilities.error && !r.error &&
                    (!out.hwpPeci || !out.hwpPeciRequest.error) && (!out.hwpControl || !out.hwpControlValue.error);
                add("HWP package minimum", "performance level", 0x772, p, double(p.value & 255), packageEditable && !overridden(63), IntelField::HwpPackageMin);
                add("HWP package maximum", "performance level", 0x772, p, double((p.value >> 8) & 255), packageEditable && !overridden(62), IntelField::HwpPackageMax);
                add("HWP package desired (0 = autonomous)", "performance level", 0x772, p, double((p.value >> 16) & 255), packageEditable, IntelField::HwpPackageDesired);
                if (out.epp) add("HWP package energy preference", "0..255", 0x772, p, double((p.value >> 24) & 255), packageEditable && !overridden(60), IntelField::HwpPackageEpp);
                if (out.hwpActivityWindow) add("HWP package activity window (0 = automatic)", "us", 0x772, p,
                    hwpActivityWindowMicroseconds(unsigned((p.value >> 32) & 1023)), packageEditable && !((p.value >> 16) & 255), IntelField::HwpPackageActivityWindow);
                add("HWP package control bit", "0 / 1", 0x774, r, double((r.value >> 42) & 1), false, IntelField::Pl1);
            }
            if (out.hwpFlexible) {
                static const char *const names[] = {"HWP minimum valid bit", "HWP maximum valid bit", "HWP desired valid bit", "HWP EPP valid bit", "HWP activity window valid bit"};
                for (unsigned i=0;i<5;++i) {
                    if ((i==3 && !out.epp) || (i==4 && !out.hwpActivityWindow)) continue;
                    add(names[i], "0 / 1", 0x774, r, double((r.value >> (63-i)) & 1), false, IntelField::Pl1);
                }
            }
            if (out.hwpPeci) {
                const auto &p = out.hwpPeciRequest;
                add("HWP PECI minimum", "performance level", 0x775, p, double(p.value & 255), false, IntelField::Pl1);
                add("HWP PECI maximum", "performance level", 0x775, p, double((p.value >> 8) & 255), false, IntelField::Pl1);
                add("HWP PECI energy preference", "0..255", 0x775, p, double((p.value >> 24) & 255), false, IntelField::Pl1);
                add("HWP PECI minimum override", "0 / 1", 0x775, p, double((p.value >> 63) & 1), false, IntelField::Pl1);
                add("HWP PECI maximum override", "0 / 1", 0x775, p, double((p.value >> 62) & 1), false, IntelField::Pl1);
                add("HWP PECI EPP override", "0 / 1", 0x775, p, double((p.value >> 60) & 1), false, IntelField::Pl1);
            }
            if (out.hwpControl) add("HWP package control polarity", "0 / 1", 0x776, out.hwpControlValue,
                double(out.hwpControlValue.value & 1), false, IntelField::Pl1);
            if (out.hwpNotifications) {
                const auto interrupt = s.execute(msrRequest(cpu, 0x773));
                add("HWP guaranteed change interrupt enabled", "0 / 1", 0x773, interrupt, double(interrupt.value & 1), false, IntelField::Pl1);
                add("HWP minimum excursion interrupt enabled", "0 / 1", 0x773, interrupt, double((interrupt.value >> 1) & 1), false, IntelField::Pl1);
                if (out.hwpHighestChange) add("HWP highest change interrupt enabled", "0 / 1", 0x773, interrupt, double((interrupt.value >> 2) & 1), false, IntelField::Pl1);
                if (out.hwpPeci) add("HWP PECI override interrupt enabled", "0 / 1", 0x773, interrupt, double((interrupt.value >> 3) & 1), false, IntelField::Pl1);
            }
        }
        if ((features.words[0] & ((1u << 6) | 1)) && hasIntelRaplProfile(out.identity)) {
            auto target = s.execute(msrRequest(cpu, 0x1a2));
            const int tjmax = int((target.value >> 16) & 255);
            if (!target.error && !tjmax) target.error = -ENODATA;
            add("Default TCC activation temperature (TjMax)", "C", 0x1a2, target, tjmax, false, IntelField::Pl1);
            // Linux v6.12 intel_tcc.c: Tiger/Raptor Lake use 8-bit digital
            // readings; SPR/EMR use 7-bit core and 8-bit package readings.
            const auto model = out.identity.model;
            const bool wide = model == 0x8c || model == 0x8d || model == 0x97 ||
                model == 0x9a || model == 0xb7 || model == 0xba;
            for (bool package : {false,true}) {
                if (!(features.words[0] & (package ? 1u << 6 : 1u))) continue;
                const unsigned msr = package ? 0x1b1 : 0x19c;
                const unsigned mask = wide || (package && (model == 0x8f || model == 0xcf)) ? 255 : 127;
                const auto status = s.execute(msrRequest(cpu, msr));
                if (package && (model == 0xb7 || model == 0x8f || model == 0xcf)) {
                    // IA32_PACKAGE_THERM_STATUS is the source of the original
                    // get_hedt_clipping_log(). Decode only documented bits
                    // 0..11; bits 12..15 are reserved, not current-limit flags.
                    // Event flags remain meaningful even if the temperature
                    // conversion lacks a valid TjMax or digital sensor value.
                    static const char *const names[6][2] = {
                        {"Package thermal event active", "Package thermal event history"},
                        {"Package PROCHOT event active", "Package PROCHOT event history"},
                        {"Package critical temperature active", "Package critical temperature history"},
                        {"Package thermal threshold 1 active", "Package thermal threshold 1 history"},
                        {"Package thermal threshold 2 active", "Package thermal threshold 2 history"},
                        {"Package power limit active", "Package power limit history"}
                    };
                    for (unsigned event=0;event<6;++event) for (unsigned log=0;log<2;++log)
                        add(names[event][log], "0 / 1", msr, status,
                            double((status.value >> (2*event+log)) & 1), false, IntelField::Pl1);
                }
                auto therm = status;
                if (target.error) therm.error = target.error;
                if (!therm.error && !(therm.value & (UINT64_C(1) << 31))) therm.error = -ENODATA;
                add(package ? "Package digital temperature" : "Core digital temperature", "C", msr, therm,
                    double(tjmax-int((therm.value >> 16) & mask)), false, IntelField::Pl1);
            }
        }
    }
    if (out.identity.family == 6 && (out.identity.model == 0xb7 || out.identity.model == 0x8f || out.identity.model == 0xcf)) {
        // Original MAX_NON_TURBO_LIM_RATIO and Intel SDM PLATFORM_INFO.
        // This read-only fused ratio is not a current-frequency measurement.
        auto platform = s.execute(msrRequest(cpu, 0xce));
        const auto ratio = unsigned((platform.value >> 8) & 255);
        if (!platform.error && !ratio) platform.error = -ENODATA;
        // Only this explicit profile is used for energy telemetry. PP0 is a
        // package-wide cores domain on B7, not the selected physical core.
        // SPR/EMR use their unit register, unlike older Xeon's 15.3 uJ DRAM
        // override (Linux v6.12 rapl_defaults_spr_server). See recovery notes.
        auto energyUnits = out.units;
        const auto features = s.cpuid(cpu, 1);
        if (features.error) out.error = energyUnits.error = features.error;
        else if (features.words[0] != out.identity.signature) out.error = energyUnits.error = -ENODEV;
        else if (!(features.words[3] & (1u << 5)) || (features.words[2] & (1u << 31))) energyUnits.error = -ENOTSUP;
        const auto exponent = unsigned((energyUnits.value >> 8) & 31);
        const double energyUnit = std::ldexp(1.0, -int(exponent));
        add("RAPL energy counter unit", "J/tick", 0x606, energyUnits, energyUnit, false, IntelField::Pl1);
        add("RAPL energy counter wrap range", "J", 0x606, energyUnits, std::ldexp(1.0, 32-int(exponent)), false, IntelField::Pl1);
        for (bool package : {true,false}) {
            const unsigned msr = package ? 0x611 : out.identity.model == 0xb7 ? 0x639 : 0x619;
            const char *name = package ? "Package energy counter" : out.identity.model == 0xb7 ?
                "CPU cores energy counter (PP0)" : "DRAM energy counter (BIOS mode 0)";
            Reply energy;
            if (energyUnits.error) energy.error = energyUnits.error;
            else energy = s.execute(msrRequest(cpu, msr));
            add(name, "J", msr, energy, double(std::uint32_t(energy.value))*energyUnit, false, IntelField::Pl1);
        }
        addHwpStatus();
        if (out.identity.model == 0xb7) {
            // Linux v6.12 intel_rapl_msr/common: Raptor Lake PL4 is MSR 601,
            // low 13 bits, using the RAPL power unit. Keep SDM bit-31 lock.
            // This is independent of ICCmax / OC mailbox rail controls.
            out.pl4 = true;
            if (energyUnits.error) out.peakPowerLimit.error = energyUnits.error;
            else out.peakPowerLimit = s.execute(msrRequest(cpu, 0x601));
            add("Package PL4 (peak power)", "W", 0x601, out.peakPowerLimit,
                std::ldexp(double(out.peakPowerLimit.value & 0x1fff), -int(out.units.value & 15)),
                !(out.peakPowerLimit.value & (UINT64_C(1) << 31)), IntelField::Pl4);
        }
        add("Maximum non-turbo ratio", "ratio", 0xce, platform, ratio, false, IntelField::Pl1);
    } else addHwpStatus();
    return out;
}
IntelControlUpdate applyIntelControl(HardwareSession &s, const IntelSnapshot &old, IntelField field, double value) {
    IntelControlUpdate result;
    auto fail = [&result](int e) { result.error = e; return result; };
    if (!std::isfinite(value) || old.error || old.identity.error || !old.identity.intel ||
        field < IntelField::Pl1 || field > IntelField::HwpPackageActivityWindow) return fail(-EINVAL);
    const auto fresh = readIntelControls(s, old.cpu);
    if (fresh.error) return fail(fresh.error);
    if (fresh.identity.signature != old.identity.signature || !fresh.identity.intel) return fail(-ENODEV);
    bool allowed = false;
    for (const auto &item : fresh.readings) if (item.field == field && item.editable) allowed = true;
    if (!allowed) return fail(-EPERM);
    RegisterUpdate u; unsigned shift = 0; std::uint64_t raw = 0;
    const bool peak = field == IntelField::Pl4;
    const bool rapl = peak || field <= IntelField::Pl2Window;
    const bool package = isIntelHwpPackageField(field);
    const IntelField hwpField = hwpLocalField(field);
    if (rapl) {
        const auto &limit = peak ? old.peakPowerLimit : old.powerLimit;
        if (!old.rapl || old.units.error || limit.error || old.units.value != fresh.units.value ||
            (peak && (!old.pl4 || !fresh.pl4))) return fail(-EAGAIN);
        u.target = msrRequest(old.cpu, peak ? 0x601 : 0x610); u.expected = limit.value;
        u.lockMask = UINT64_C(1) << (peak ? 31 : 63); u.compareMask = UINT64_MAX;
        if (peak || field == IntelField::Pl1 || field == IntelField::Pl2) {
            const double encoded = std::ldexp(value, int(fresh.units.value & 15));
            if (encoded < 1 || encoded > (peak ? 8191 : 32767)) return fail(-ERANGE);
            raw = std::uint64_t(std::floor(encoded)); shift = field == IntelField::Pl1 ? 0 : 32;
            if (peak) shift = 0;
            u.mask = (peak ? UINT64_C(0x1fff) : UINT64_C(0x7fff)) << shift;
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
        const int contextError = package ? comparePackageHwpContext(old, fresh) : compareHwpContext(old, fresh);
        if (contextError) return fail(contextError);
        const auto &request = package ? old.hwpPackageRequest : old.hwpRequest;
        const auto &currentRequest = package ? fresh.hwpPackageRequest : fresh.hwpRequest;
        if (!old.hwp || request.error || old.hwpCapabilities.error ||
            old.hwpCapabilities.value != fresh.hwpCapabilities.value) return fail(-EAGAIN);
        const bool window = hwpField == IntelField::HwpActivityWindow;
        if (value < 0 || value > (window ? 1270000000.0 : 255.0) || value != std::floor(value)) return fail(-ERANGE);
        raw = std::uint64_t(value);
        const unsigned lo = unsigned((fresh.hwpCapabilities.value >> 24) & 255), hi = unsigned(fresh.hwpCapabilities.value & 255);
        if (lo > hi) return fail(-ERANGE);
        if (!window && hwpField != IntelField::HwpEpp && !(hwpField == IntelField::HwpDesired && raw == 0) && (raw < lo || raw > hi)) return fail(-ERANGE);
        shift = hwpField == IntelField::HwpMin ? 0 : hwpField == IntelField::HwpMax ? 8 : hwpField == IntelField::HwpDesired ? 16 : 24;
        if (window) {
            unsigned encoded = 0;
            const int error = encodeHwpActivityWindow(std::uint32_t(raw), encoded);
            if (error) return fail(error);
            raw = encoded; shift = 32;
        }
        u.mask = (window ? UINT64_C(1023) : UINT64_C(255)) << shift; u.target = msrRequest(old.cpu, package ? 0x772 : 0x774);
        u.expected = request.value; u.compareMask = UINT64_MAX;
        const auto next = (currentRequest.value & ~u.mask) | (raw << shift);
        const auto minimum = next & 255, maximum = (next >> 8) & 255, desired = (next >> 16) & 255;
        if (minimum > maximum || (desired && (desired < minimum || desired > maximum))) return fail(-ERANGE);
    }
    u.bits = raw << shift;
    result.msr = std::uint32_t(u.target.address);
    result.expected = (u.expected & ~u.mask) | u.bits;
    const auto &current = peak ? fresh.peakPowerLimit : rapl ? fresh.powerLimit : package ? fresh.hwpPackageRequest : fresh.hwpRequest;
    if (current.error) return fail(current.error);
    if (current.value != u.expected) return fail(-EAGAIN);
    if (result.expected != u.expected) {
        static_cast<UpdateResult &>(result) = updateRegisters(s, {u});
        if (result.error) return result;
        result.after = readIntelControls(s, old.cpu);
    } else {
        // A fresh, identical snapshot is sufficient for a no-op. Do not write
        // simply to confirm a setting that is already present.
        result.after = fresh;
    }
    const auto &after = result.after;
    const auto &observed = peak ? after.peakPowerLimit : rapl ? after.powerLimit : package ? after.hwpPackageRequest : after.hwpRequest;
    result.readbackValid = !after.error && !observed.error && (peak ? after.pl4 : rapl ? after.rapl : after.hwp && (!package || after.hwpPackage));
    if (after.error) return fail(after.error);
    if (observed.error) return fail(observed.error);
    if (!after.identity.intel || after.identity.signature != old.identity.signature) return fail(-ENODEV);
    if (rapl) {
        if (after.units.error) return fail(after.units.error);
        if (!after.rapl || (peak && !after.pl4) || after.units.value != old.units.value) return fail(-EAGAIN);
    } else {
        const int contextError = package ? comparePackageHwpContext(old, after) : compareHwpContext(old, after);
        if (contextError) return fail(contextError);
        if (after.hwpCapabilities.error) return fail(after.hwpCapabilities.error);
        if (!after.hwp || !after.hwpEnabled || (hwpField == IntelField::HwpEpp && !after.epp) ||
            (hwpField == IntelField::HwpActivityWindow && !after.hwpActivityWindow) ||
            after.hwpCapabilities.value != old.hwpCapabilities.value) return fail(-EAGAIN);
    }
    if (observed.value != result.expected) return fail(-EIO);
    if ((result.error = s.checkpoint())) return result;
    for (const auto &item : after.readings) {
        if (item.msr == result.msr && item.field == field && item.decoded && !item.error) {
            result.readbackValue = item.value;
            result.verified = true;
            result.unchanged = !result.writeAttempted;
            return result;
        }
    }
    return fail(-ENODATA);
}
} }
