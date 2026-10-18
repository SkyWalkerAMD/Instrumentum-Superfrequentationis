// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pstates.h"
#include <cerrno>

namespace octool {
namespace core {

const char *amdPstateLayout(unsigned family)
{
    return family == 0x1a ? "fid12-x5" :
        family == 0x17 || family == 0x19 ? "fid8-dfs6" : "unsupported";
}

const char *pstateFrequencyStatusName(PstateFrequencyStatus status)
{
    switch (status) {
    case PstateFrequencyStatus::Disabled: return "disabled";
    case PstateFrequencyStatus::ReservedFid: return "reserved_fid";
    case PstateFrequencyStatus::DividerOff: return "divider_off";
    case PstateFrequencyStatus::UnverifiedDivider: return "unverified_divider";
    case PstateFrequencyStatus::Valid: return "valid";
    default: return "unsupported_family";
    }
}

// Linux cpupower and AMD OSRR 56255; docs/amd-pstates-legacy.md.
// These are configured definitions, not a measurement or a writable policy.
PstateValue decodeAmdPstate(unsigned family, std::uint64_t raw)
{
    PstateValue value;
    if (family != 0x17 && family != 0x19 && family != 0x1a) return value;
    value.enabled = (raw >> 63) != 0;
    value.hasDivider = family != 0x1a;
    value.dividerId = value.hasDivider ? unsigned((raw >> 8) & 63) : 0;
    const unsigned fid = unsigned(raw & (value.hasDivider ? 255 : 4095));
    value.frequencyId = fid;
    value.rawBit32 = (raw & (UINT64_C(1) << 32)) != 0;
    // Preserve bit 32 separately. Some 17h/19h models extend VID here;
    // their low byte must not be advertised as a complete voltage encoding.
    value.vidBits = unsigned((raw >> 14) & 255) |
        (family == 0x1a && value.rawBit32 ? 256u : 0u);
    value.iddValueBits = unsigned((raw >> 22) & 255);
    value.iddDivBits = unsigned((raw >> 30) & 3);
    value.frequencyStatus = PstateFrequencyStatus::Disabled;
    if (!value.enabled) return value;
    value.frequencyStatus = PstateFrequencyStatus::ReservedFid;
    if (fid < 0x10) return value;
    if (value.hasDivider) {
        value.frequencyStatus = PstateFrequencyStatus::DividerOff;
        if (!value.dividerId) return value;
        value.frequencyStatus = PstateFrequencyStatus::UnverifiedDivider;
        // The OSRR value table ends at 2Ch, while nearby prose mentions 30h.
        // Decode the unambiguous intersection; expose other codes as raw.
        if (value.dividerId < 8 || value.dividerId > 0x2c ||
            (value.dividerId > 0x1a && (value.dividerId & 1))) return value;
        value.frequencyMHz = 200.0 * fid / value.dividerId;
    } else value.frequencyMHz = fid * 5.0;
    value.validFrequency = true;
    value.frequencyStatus = PstateFrequencyStatus::Valid;
    return value;
}

PstateValue decodeFamily1aPstate(std::uint64_t raw)
{
    return decodeAmdPstate(0x1a, raw);
}

PstateElectricalEncoding amdPstateElectricalEncoding(unsigned family, unsigned model)
{
    // Pinned coreboot CPU tables, platform Kconfig, MSR layout and SVI paths:
    // docs/amd-pstates-electrical.md. Do not select by family or brand name.
    if ((family == 0x17 && (model == 0x11 || model == 0x18 || model == 0x20)) ||
        (family == 0x19 && model == 0x50)) return PstateElectricalEncoding::Svi2;
    if (family == 0x17 && model == 0xa0) return PstateElectricalEncoding::Svi3;
    return PstateElectricalEncoding::Unavailable;
}

const char *pstateElectricalEncodingName(PstateElectricalEncoding encoding)
{
    switch (encoding) {
    case PstateElectricalEncoding::Svi2: return "svi2";
    case PstateElectricalEncoding::Svi3: return "svi3";
    default: return "unavailable";
    }
}

const char *pstateElectricalStatusName(PstateElectricalStatus status)
{
    switch (status) {
    case PstateElectricalStatus::Virtualized: return "virtualized";
    case PstateElectricalStatus::Disabled: return "disabled";
    case PstateElectricalStatus::ReservedBits: return "reserved_bits";
    case PstateElectricalStatus::Valid: return "valid";
    case PstateElectricalStatus::VoltageOff: return "voltage_off";
    case PstateElectricalStatus::ReservedDivisor: return "reserved_divisor";
    default: return "unsupported_model";
    }
}

PstateElectricalValue decodeAmdPstateElectrical(unsigned family, unsigned model,
                                               bool hypervisor, std::uint64_t raw)
{
    PstateElectricalValue value;
    value.encoding = amdPstateElectricalEncoding(family, model);
    if (value.encoding == PstateElectricalEncoding::Unavailable) return value;
    if (hypervisor) {
        value.voltageStatus = value.currentStatus = PstateElectricalStatus::Virtualized;
        return value;
    }
    if (!(raw & (UINT64_C(1) << 63))) {
        value.voltageStatus = value.currentStatus = PstateElectricalStatus::Disabled;
        return value;
    }
    const bool svi3 = value.encoding == PstateElectricalEncoding::Svi3;
    const std::uint64_t reserved = svi3 ? UINT64_C(0x7ffffffe00000000) : UINT64_C(0x7fffffff00000000);
    if (raw & reserved) {
        value.voltageStatus = value.currentStatus = PstateElectricalStatus::ReservedBits;
        return value;
    }
    value.hasVid = value.hasVoltage = true;
    value.vid = unsigned((raw >> 14) & 255) | (svi3 ? unsigned((raw >> 24) & 256) : 0);
    const bool off = svi3 ? value.vid == 0 : value.vid >= 0xf8;
    // Integer microvolts preserve the 6.25 mV step without integer truncation.
    const unsigned microvolts = off ? 0 : svi3 ? 245000u + 5000u * value.vid : 1550000u - 6250u * value.vid;
    value.voltageMv = microvolts / 1000.0;
    value.voltageStatus = off ? PstateElectricalStatus::VoltageOff : PstateElectricalStatus::Valid;
    const unsigned divisor = unsigned((raw >> 30) & 3);
    value.currentStatus = PstateElectricalStatus::ReservedDivisor;
    if (divisor < 3) {
        static const unsigned divisors[] = {1, 10, 100};
        value.hasCurrent = true;
        value.expectedCoreCurrentA = double((raw >> 22) & 255) / divisors[divisor];
        value.currentStatus = PstateElectricalStatus::Valid;
    }
    return value;
}

PstateSnapshot readAmdPstates(PstateReader &reader, unsigned cpu)
{
    PstateSnapshot snapshot;
    snapshot.cpu = cpu;
    std::uint32_t words[4]{};
    snapshot.error = reader.cpuid(cpu, 0, 0, words);
    if (snapshot.error) return snapshot;
    // CPUID register values for AuthenticAMD, independent of host endianness.
    if (words[1] != 0x68747541u || words[3] != 0x69746e65u ||
        words[2] != 0x444d4163u || words[0] < 1) {
        snapshot.status = PstateStatus::NotAmd;
        return snapshot;
    }
    snapshot.error = reader.cpuid(cpu, 1, 0, words);
    if (snapshot.error) return snapshot;
    snapshot.hasIdentity = true;
    snapshot.hypervisor = (words[2] & (UINT32_C(1) << 31)) != 0;
    const unsigned baseFamily = (words[0] >> 8) & 0xf;
    snapshot.family = baseFamily + (baseFamily == 0xf ? (words[0] >> 20) & 0xff : 0);
    snapshot.model = ((words[0] >> 4) & 0xf) |
        ((baseFamily == 6 || baseFamily == 0xf) ? (words[0] >> 12) & 0xf0 : 0);
    snapshot.stepping = words[0] & 0xf;
    if (snapshot.family != 0x17 && snapshot.family != 0x19 && snapshot.family != 0x1a) {
        snapshot.status = PstateStatus::UnsupportedFamily;
        return snapshot;
    }
    snapshot.error = reader.cpuid(cpu, 0x80000000, 0, words);
    if (snapshot.error) return snapshot;
    if (words[0] < 0x80000007) {
        snapshot.status = PstateStatus::CapabilityUnavailable;
        return snapshot;
    }
    snapshot.error = reader.cpuid(cpu, 0x80000007, 0, words);
    if (snapshot.error) return snapshot;
    if (!(words[3] & (1u << 7))) {
        snapshot.status = PstateStatus::CapabilityNotAdvertised;
        return snapshot;
    }
    std::uint64_t limit = 0;
    snapshot.error = reader.readMsr(cpu, 0xc0010061, limit);
    if (snapshot.error) {
        snapshot.status = PstateStatus::LimitReadFailed;
        return snapshot;
    }
    snapshot.hasLimit = true;
    snapshot.limitRaw = limit;
    snapshot.minimum = unsigned(limit & 7);
    snapshot.maximum = unsigned((limit >> 4) & 7);
    if (snapshot.minimum > snapshot.maximum) {
        snapshot.status = PstateStatus::InconsistentLimit;
        snapshot.error = -ERANGE;
        return snapshot;
    }
    for (unsigned i = 0; i < snapshot.rows.size(); ++i) {
        auto &row = snapshot.rows[i];
        if (i > snapshot.maximum) {
            row.status = PstateRowStatus::AboveLimit;
            continue;
        }
        std::uint64_t raw = 0;
        row.error = reader.readMsr(cpu, 0xc0010064u + i, raw);
        if (row.error) {
            row.status = PstateRowStatus::ReadFailed;
            ++snapshot.failures;
        } else {
            row.status = PstateRowStatus::Read;
            row.raw = raw;
        }
    }
    snapshot.status = PstateStatus::Complete;
    return snapshot;
}

} // namespace core
} // namespace octool
