// SPDX-License-Identifier: GPL-2.0-only
// Standalone core regression: no Qt, Linux HAL, device, or CPUID instruction.
#ifdef NDEBUG
#error "Core regression assertions must stay enabled"
#endif
#include "../core/amd_pstates.h"
#include "pstates_electrical_fixture.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace octool::core;

struct Reader final : PstateReader {
    unsigned expectedCpu = 37;
    bool amd = true, advertised = true, hypervisor = false;
    std::uint32_t rootMaximum = 1, signature = 0x00b00f81;
    std::uint32_t extendedMaximum = 0x80000007, failLeaf = UINT32_MAX;
    std::uint32_t failRegister = 0;
    std::uint64_t limit = 0x20;
    std::vector<std::uint32_t> leaves, msrs;

    int cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf,
              std::uint32_t words[4]) override {
        assert(cpu == expectedCpu && subleaf == 0);
        leaves.push_back(leaf);
        std::fill(words, words + 4, UINT32_MAX);
        if (leaf == failLeaf) return -EACCES; // Deliberately poisoned output.
        std::fill(words, words + 4, 0);
        switch (leaf) {
        case 0: {
            words[0] = rootMaximum;
            const char *vendor = amd ? "AuthenticAMD" : "GenuineIntel";
            std::memcpy(&words[1], vendor, 4);
            std::memcpy(&words[3], vendor + 4, 4);
            std::memcpy(&words[2], vendor + 8, 4);
            break;
        }
        case 1: words[0] = signature; words[2] = hypervisor ? UINT32_C(1) << 31 : 0; break;
        case 0x80000000: words[0] = extendedMaximum; break;
        case 0x80000007: words[3] = advertised ? 0x80 : 0; break;
        default: assert(false);
        }
        return 0;
    }
    int readMsr(unsigned cpu, std::uint32_t index, std::uint64_t &value) override {
        assert(cpu == expectedCpu);
        msrs.push_back(index);
        value = UINT64_MAX;
        if (index == failRegister) return -EIO; // Failed output must not escape.
        if (index == 0xc0010061) value = limit;
        else {
            assert(index >= 0xc0010064 && index <= 0xc001006b);
            value = index == 0xc0010064 ? 0x80000001000003e8ULL : 0x00000000ffc001ccULL;
        }
        return 0;
    }
};

static void allUnread(const PstateSnapshot &snapshot)
{
    for (const auto &row : snapshot.rows) {
        assert(row.status == PstateRowStatus::NotRead && row.raw == 0);
    }
}

static void decodeVectors()
{
    const std::uint64_t raw[] = {0x80000001000003e8ULL, 0x80000000000001ccULL,
        0x800000000000012cULL, 0x8000000000000010ULL, 0x8000000000000fffULL};
    const unsigned mhz[] = {5000, 2300, 1500, 80, 20475};
    for (unsigned i = 0; i < 5; ++i) {
        const auto value = decodeFamily1aPstate(raw[i]);
        assert(value.enabled && value.validFrequency && value.frequencyMHz == mhz[i]);
    }
    assert(!decodeFamily1aPstate(0x800000000000000fULL).validFrequency);
    assert(!decodeFamily1aPstate(0x00000001000003e8ULL).enabled);
    assert(!decodeFamily1aPstate(0x00000001000003e8ULL).validFrequency);
}

static void capabilityGuards()
{
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        Reader reader;
        if (scenario == 0) reader.amd = false;
        if (scenario == 1) reader.rootMaximum = 0;
        if (scenario == 2) reader.signature = 0x00900f11;
        if (scenario == 3) reader.extendedMaximum = 0x80000006;
        if (scenario == 4) reader.advertised = false;
        assert(reader.leaves.empty() && reader.msrs.empty());
        const auto snapshot = readAmdPstates(reader, reader.expectedCpu);
        const PstateStatus expected[] = {PstateStatus::NotAmd, PstateStatus::NotAmd,
            PstateStatus::UnsupportedFamily, PstateStatus::CapabilityUnavailable,
            PstateStatus::CapabilityNotAdvertised};
        assert(snapshot.status == expected[scenario] && snapshot.error == 0);
        assert(reader.msrs.empty());
        allUnread(snapshot);
    }
}

static void cpuErrorsStopBeforeMsr()
{
    for (auto leaf : {0u, 1u, 0x80000000u, 0x80000007u}) {
        Reader reader;
        reader.failLeaf = leaf;
        const auto snapshot = readAmdPstates(reader, reader.expectedCpu);
        assert(snapshot.status == PstateStatus::CpuReadFailed && snapshot.error == -EACCES);
        assert(reader.leaves.back() == leaf && reader.msrs.empty());
        allUnread(snapshot);
    }
}

static void successfulSnapshotAndLimit()
{
    Reader reader;
    const auto snapshot = readAmdPstates(reader, reader.expectedCpu);
    assert(snapshot.status == PstateStatus::Complete && snapshot.error == 0);
    assert(snapshot.cpu == reader.expectedCpu && snapshot.hasIdentity);
    assert(snapshot.family == 0x1a && snapshot.model == 8 && snapshot.stepping == 1);
    assert(snapshot.maximum == 2 && snapshot.failures == 0);
    assert(reader.leaves == std::vector<std::uint32_t>({0, 1, 0x80000000, 0x80000007}));
    assert(reader.msrs == std::vector<std::uint32_t>({0xc0010061, 0xc0010064, 0xc0010065, 0xc0010066}));
    assert(snapshot.rows[0].raw == 0x80000001000003e8ULL);
    assert(snapshot.rows[1].status == PstateRowStatus::Read && snapshot.rows[1].raw == 0x00000000ffc001ccULL);
    for (unsigned i = 3; i < 8; ++i) {
        assert(snapshot.rows[i].status == PstateRowStatus::AboveLimit && snapshot.rows[i].raw == 0);
    }
}

static void maximumRange()
{
    Reader reader;
    reader.limit = UINT64_MAX;
    const auto snapshot = readAmdPstates(reader, reader.expectedCpu);
    assert(snapshot.maximum == 7 && reader.msrs.size() == 9);
    assert(reader.msrs.back() == 0xc001006b);
    for (const auto &row : snapshot.rows) assert(row.status == PstateRowStatus::Read);
}

static void rowFailureIsNotAValue()
{
    Reader reader;
    reader.failRegister = 0xc0010065;
    auto snapshot = readAmdPstates(reader, reader.expectedCpu);
    assert(snapshot.status == PstateStatus::Complete && snapshot.failures == 1);
    assert(snapshot.rows[1].status == PstateRowStatus::ReadFailed);
    assert(snapshot.rows[1].error == -EIO && snapshot.rows[1].raw == 0);
    assert(snapshot.rows[2].status == PstateRowStatus::Read);
    reader.failRegister = 0xc0010064;
    snapshot = readAmdPstates(reader, reader.expectedCpu);
    assert(snapshot.rows[0].raw == 0 && snapshot.rows[0].status == PstateRowStatus::ReadFailed);
    assert(snapshot.rows[1].status == PstateRowStatus::Read);
}

static void limitFailureStopsSampling()
{
    Reader reader;
    reader.failRegister = 0xc0010061;
    const auto snapshot = readAmdPstates(reader, reader.expectedCpu);
    assert(snapshot.status == PstateStatus::LimitReadFailed && snapshot.error == -EIO);
    assert(reader.msrs == std::vector<std::uint32_t>({0xc0010061}));
    allUnread(snapshot);
}

static void legacyGoldenVectors()
{
    const unsigned fid[] = {160, 171, 165, 255, 16};
    const unsigned dfs[] = {8, 12, 9, 44, 8};
    const double mhz[] = {4000, 2850, 3666.6666666666665, 1159.090909090909, 400};
    for (unsigned family : {0x17u, 0x19u}) for (unsigned i = 0; i < 5; ++i) {
        const auto raw = UINT64_C(0xfedcba99abcdc000) | (dfs[i] << 8) | fid[i];
        const auto d = decodeAmdPstate(family, raw);
        assert(d.enabled && d.hasDivider && d.validFrequency && d.rawBit32);
        assert(d.frequencyId == fid[i] && d.dividerId == dfs[i]);
        assert(std::abs(d.frequencyMHz - mhz[i]) < 1e-9);
        assert(d.vidBits == 55 && d.iddValueBits == 175 && d.iddDivBits == 2);
        assert(d.frequencyStatus == PstateFrequencyStatus::Valid);
    }
}

static void exhaustiveLegacyFrequencyEncodings()
{
    // Independent value table from OSRR pp130-131, not the decoder predicate.
    const unsigned accepted[] = {8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,
                                 28,30,32,34,36,38,40,42,44};
    for (unsigned family : {0x17u, 0x19u}) for (unsigned enabled = 0; enabled < 2; ++enabled)
        for (unsigned fid = 0; fid < 256; ++fid) for (unsigned dfs = 0; dfs < 64; ++dfs) {
            const std::uint64_t raw = (std::uint64_t(enabled) << 63) | (dfs << 8) | fid;
            const auto d = decodeAmdPstate(family, raw);
            const bool divisor = std::find(std::begin(accepted), std::end(accepted), dfs) != std::end(accepted);
            const bool valid = enabled && fid >= 16 && divisor;
            assert(d.validFrequency == valid && d.frequencyId == fid && d.dividerId == dfs);
            if (valid) assert(std::abs(d.frequencyMHz * dfs - 200.0 * fid) < 1e-7);
            else {
                assert(d.frequencyMHz == 0);
                const auto status = !enabled ? PstateFrequencyStatus::Disabled : fid < 16 ? PstateFrequencyStatus::ReservedFid :
                    dfs == 0 ? PstateFrequencyStatus::DividerOff : PstateFrequencyStatus::UnverifiedDivider;
                assert(d.frequencyStatus == status);
            }
        }
}

static void familyAndRawFieldIsolation()
{
    for (unsigned family = 0; family < 256; ++family) {
        Reader reader; reader.signature = ((family >= 15 ? family - 15 : 0) << 20) | ((family >= 15 ? 15 : family) << 8);
        const auto p = readAmdPstates(reader, reader.expectedCpu);
        const bool supported = family == 0x17 || family == 0x19 || family == 0x1a;
        assert((p.status == PstateStatus::Complete) == supported);
        assert(reader.msrs.empty() != supported);
        if (!supported) {
            const auto d = decodeAmdPstate(family, UINT64_MAX);
            assert(!d.validFrequency && d.frequencyStatus == PstateFrequencyStatus::UnsupportedFamily);
        }
    }
    for (unsigned family : {0x17u,0x19u,0x1au}) for (unsigned bit = 14; bit < 63; ++bit) {
        const auto base = UINT64_C(0x80000000000008a0);
        const auto a = decodeAmdPstate(family, base), b = decodeAmdPstate(family, base | (UINT64_C(1) << bit));
        assert(a.frequencyMHz == b.frequencyMHz && a.frequencyId == b.frequencyId && a.dividerId == b.dividerId);
        if (bit == 32) assert(b.rawBit32 && b.vidBits == (family == 0x1a ? 256u : 0u));
    }
}

static void allLimitPairsAndFamilies()
{
    for (unsigned family : {0x17u,0x19u,0x1au}) for (unsigned minimum = 0; minimum < 8; ++minimum)
        for (unsigned maximum = 0; maximum < 8; ++maximum) {
            Reader reader; reader.signature = ((family-15) << 20) | 0x000a0f91;
            reader.limit = UINT64_C(0xabcdef1234567808) | (maximum << 4) | minimum;
            const auto p = readAmdPstates(reader, reader.expectedCpu);
            assert(p.family == family && p.model == 0xa9 && p.stepping == 1);
            assert(p.hasLimit && p.limitRaw == reader.limit && p.minimum == minimum && p.maximum == maximum);
            if (minimum > maximum) {
                assert(p.status == PstateStatus::InconsistentLimit && p.error == -ERANGE);
                assert(reader.msrs.size() == 1); allUnread(p);
            } else {
                assert(p.status == PstateStatus::Complete && !p.error);
                assert(reader.msrs.size() == maximum + 2);
                for (unsigned i = 0; i < 8; ++i)
                    assert(p.rows[i].status == (i <= maximum ? PstateRowStatus::Read : PstateRowStatus::AboveLimit));
            }
        }
}

static void legacyReadFailuresAndCapabilities()
{
    for (auto signature : {0x00800f11u,0x00a20f10u}) {
        for (auto leaf : {0u,1u,0x80000000u,0x80000007u}) {
            Reader r; r.signature = signature; r.failLeaf = leaf;
            const auto p = readAmdPstates(r, r.expectedCpu);
            assert(p.error == -EACCES && r.msrs.empty()); allUnread(p);
        }
        for (unsigned mode = 0; mode < 11; ++mode) {
            Reader r; r.signature = signature; r.limit = 0x70;
            if (mode == 0) r.advertised = false;
            else if (mode == 1) r.extendedMaximum = 0x80000006;
            else r.failRegister = mode == 2 ? 0xc0010061u : 0xc0010064u + mode - 3;
            const auto p = readAmdPstates(r, r.expectedCpu);
            if (mode < 2) { assert(r.msrs.empty()); allUnread(p); }
            else if (mode == 2) { assert(!p.hasLimit && p.limitRaw == 0 && r.msrs.size() == 1); allUnread(p); }
            else {
                assert(p.failures == 1 && p.rows[mode-3].raw == 0 && p.rows[mode-3].error == -EIO);
                assert(p.status == PstateStatus::Complete && r.msrs.size() == 9);
            }
        }
    }
}

static void electricalGoldenDescriptors()
{
    const double voltages[2][6] = {{1550,1543.75,1050,6.25,0,0}, {0,250,1525,2800,1520,1925}};
    const double currents[] = {255,17.5,1.75,0,0,2.55};
    for (unsigned svi3 = 0; svi3 < 2; ++svi3) for (unsigned row = 0; row < 8; ++row) {
        const auto e = decodeAmdPstateElectrical(0x17, svi3 ? 0xa0 : 0x18, false, pstateElectricalFixture(svi3 != 0, row));
        assert(e.encoding == (svi3 ? PstateElectricalEncoding::Svi3 : PstateElectricalEncoding::Svi2));
        if (row < 6) {
            assert(e.hasVid && e.hasVoltage && e.voltageMv == voltages[svi3][row]);
            assert(e.voltageStatus == (voltages[svi3][row] == 0 ? PstateElectricalStatus::VoltageOff : PstateElectricalStatus::Valid));
            assert(e.hasCurrent == (row != 4));
            if (e.hasCurrent) assert(e.expectedCoreCurrentA == currents[row]);
            else assert(e.currentStatus == PstateElectricalStatus::ReservedDivisor);
        } else {
            assert(!e.hasVid && !e.hasVoltage && !e.hasCurrent);
            assert(e.voltageStatus == (row == 6 ? PstateElectricalStatus::Disabled : PstateElectricalStatus::ReservedBits));
            assert(e.currentStatus == e.voltageStatus);
        }
    }
}

static void electricalExactModelGates()
{
    unsigned supported = 0;
    for (unsigned family = 0; family < 256; ++family) for (unsigned model = 0; model < 256; ++model) {
        const bool known = (family == 0x17 && (model == 0x11 || model == 0x18 || model == 0x20 || model == 0xa0)) ||
            (family == 0x19 && model == 0x50);
        const auto e = decodeAmdPstateElectrical(family, model, false, pstateElectricalFixture(false, 1));
        assert(e.hasVoltage == known && e.hasCurrent == known && e.hasVid == known);
        if (known) ++supported;
        else assert(e.encoding == PstateElectricalEncoding::Unavailable && e.voltageStatus == PstateElectricalStatus::UnsupportedModel);
    }
    assert(supported == 5);
    assert(amdPstateElectricalEncoding(0x117, 0x18) == PstateElectricalEncoding::Unavailable);
    assert(amdPstateElectricalEncoding(0x17, 0x118) == PstateElectricalEncoding::Unavailable);
}

static void electricalVoltageBoundaries()
{
    // All 512 encodings catch a lost ninth VID bit and SVI2 off-code underflow.
    for (unsigned svi3 = 0; svi3 < 2; ++svi3) for (unsigned vid = 0; vid < 512; ++vid) {
        const auto raw = UINT64_C(0x80000000000008a0) | (std::uint64_t(vid & 255) << 14) | (std::uint64_t(vid >> 8) << 32);
        const auto e = decodeAmdPstateElectrical(0x17, svi3 ? 0xa0 : 0x11, false, raw);
        if (!svi3 && vid >= 256) {
            assert(!e.hasVoltage && !e.hasCurrent && e.voltageStatus == PstateElectricalStatus::ReservedBits); continue;
        }
        assert(e.hasVoltage && e.vid == vid);
        if (svi3) assert(e.voltageMv == (vid ? 245 + 5 * vid : 0));
        else assert(e.voltageMv == (vid < 248 ? 1550 - 6.25 * vid : 0));
        const auto rawFields = decodeAmdPstate(0x17, raw);
        assert(rawFields.vidBits == (vid & 255) && rawFields.rawBit32 == (vid >= 256));
        // Voltage/current validity is independent of an unverified DFS code.
        const auto badFrequency = decodeAmdPstateElectrical(0x17, svi3 ? 0xa0 : 0x11, false, raw & ~UINT64_C(0x3fff));
        assert(badFrequency.voltageMv == e.voltageMv && badFrequency.hasVoltage);
    }
}

static void electricalCurrentBoundaries()
{
    for (unsigned value = 0; value < 256; ++value) for (unsigned div = 0; div < 4; ++div) {
        const auto raw = UINT64_C(0x80000000001408a0) | (std::uint64_t(value) << 22) | (std::uint64_t(div) << 30);
        const auto e = decodeAmdPstateElectrical(0x19, 0x50, false, raw);
        assert(e.hasVoltage && e.voltageMv == 1050 && e.hasCurrent == (div != 3));
        if (div == 0) assert(e.expectedCoreCurrentA == value);
        if (div == 1) assert(std::abs(e.expectedCoreCurrentA * 10 - value) < 1e-10);
        if (div == 2) assert(std::abs(e.expectedCoreCurrentA * 100 - value) < 1e-10);
        if (div == 3) assert(e.expectedCoreCurrentA == 0 && e.currentStatus == PstateElectricalStatus::ReservedDivisor);
    }
}

static void electricalReservedBitsAndDisabled()
{
    for (unsigned model : {0x18u,0xa0u}) for (unsigned bit = 32; bit < 63; ++bit) {
        const auto raw = pstateElectricalFixture(false, 2) | (UINT64_C(1) << bit);
        const auto e = decodeAmdPstateElectrical(0x17, model, false, raw);
        if (model == 0xa0 && bit == 32) { assert(e.hasVoltage && e.vid == 336 && e.voltageMv == 1925); continue; }
        assert(!e.hasVid && !e.hasVoltage && !e.hasCurrent && e.voltageStatus == PstateElectricalStatus::ReservedBits);
        const auto disabled = decodeAmdPstateElectrical(0x17, model, false, raw & ~(UINT64_C(1) << 63));
        assert(!disabled.hasVoltage && disabled.voltageStatus == PstateElectricalStatus::Disabled);
    }
    assert(std::string(pstateElectricalStatusName(PstateElectricalStatus::VoltageOff)) == "voltage_off");
    assert(std::string(pstateElectricalEncodingName(PstateElectricalEncoding::Svi3)) == "svi3");
}

static void electricalIdentityAndVirtualization()
{
    for (unsigned signature : {0x00810f10u,0x00810f80u,0x00820f00u,0x008a0f00u,0x00a50f00u})
        for (unsigned stepping = 0; stepping < 16; ++stepping) for (bool hypervisor : {false,true}) {
            Reader r; r.signature = signature | stepping; r.hypervisor = hypervisor;
            const auto p = readAmdPstates(r, r.expectedCpu);
            assert(p.hasIdentity && p.hypervisor == hypervisor && p.stepping == stepping);
            assert(p.status == PstateStatus::Complete && r.msrs.size() == 4 && r.leaves.size() == 4);
            const auto e = decodeAmdPstateElectrical(p.family, p.model, p.hypervisor, pstateElectricalFixture(false, 2));
            assert(e.hasVoltage == !hypervisor && e.hasCurrent == !hypervisor);
            if (hypervisor) assert(e.voltageStatus == PstateElectricalStatus::Virtualized && !e.hasVid);
        }
    Reader r; r.hypervisor = true; r.failLeaf = 1;
    const auto p = readAmdPstates(r, r.expectedCpu);
    assert(!p.hasIdentity && !p.hypervisor && r.msrs.empty());
}

int main()
{
    decodeVectors();
    capabilityGuards();
    cpuErrorsStopBeforeMsr();
    successfulSnapshotAndLimit();
    maximumRange();
    rowFailureIsNotAValue();
    limitFailureStopsSampling();
    legacyGoldenVectors(); exhaustiveLegacyFrequencyEncodings(); familyAndRawFieldIsolation();
    allLimitPairsAndFamilies(); legacyReadFailuresAndCapabilities();
    electricalGoldenDescriptors(); electricalExactModelGates(); electricalVoltageBoundaries();
    electricalCurrentBoundaries(); electricalReservedBitsAndDisabled(); electricalIdentityAndVirtualization();
    std::cout << "amd_pstates core: 18 scenario groups passed\n";
}
