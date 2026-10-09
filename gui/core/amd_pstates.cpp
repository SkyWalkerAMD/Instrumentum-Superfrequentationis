// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pstates.h"

namespace octool {
namespace core {

// Existing verified frequency rule; sources and limits: docs/amd-pstates.md.
// Family-wide frequency only, without model-specific VID/current assumptions.
PstateValue decodeFamily1aPstate(std::uint64_t raw)
{
    PstateValue value;
    value.enabled = (raw >> 63) != 0;
    const unsigned fid = unsigned(raw & 0xfff);
    value.frequencyId = fid;
    // Bit extraction only. The UI labels the exact positions; these are not
    // a model-independent mV/A conversion or a measured current limit.
    value.vidBits = unsigned((raw >> 14) & 255) | unsigned((raw >> 24) & 256);
    value.iddValueBits = unsigned((raw >> 22) & 255);
    value.iddDivBits = unsigned((raw >> 30) & 3);
    value.validFrequency = value.enabled && fid >= 0x10;
    if (value.validFrequency) value.frequencyMHz = fid * 5;
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
    const unsigned baseFamily = (words[0] >> 8) & 0xf;
    snapshot.family = baseFamily + (baseFamily == 0xf ? (words[0] >> 20) & 0xff : 0);
    snapshot.model = ((words[0] >> 4) & 0xf) |
        ((baseFamily == 6 || baseFamily == 0xf) ? (words[0] >> 12) & 0xf0 : 0);
    snapshot.stepping = words[0] & 0xf;
    if (snapshot.family != 0x1a) {
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
    snapshot.maximum = unsigned((limit >> 4) & 7);
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
