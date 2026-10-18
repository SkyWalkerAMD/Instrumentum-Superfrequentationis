// SPDX-License-Identifier: GPL-2.0-only
// Standalone core regression: no Qt, Linux HAL, device, or CPUID instruction.
#ifdef NDEBUG
#error "Core regression assertions must stay enabled"
#endif
#include "../core/amd_pstates.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>
#include <vector>

using namespace octool::core;

struct Reader final : PstateReader {
    unsigned expectedCpu = 37;
    bool amd = true, advertised = true;
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
        case 1: words[0] = signature; break;
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
    std::cout << "amd_pstates core: 12 scenario groups passed\n";
}
