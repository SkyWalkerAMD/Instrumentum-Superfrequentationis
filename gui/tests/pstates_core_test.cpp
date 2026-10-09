// SPDX-License-Identifier: GPL-2.0-only
// Standalone core regression: no Qt, Linux HAL, device, or CPUID instruction.
#ifdef NDEBUG
#error "Core regression assertions must stay enabled"
#endif
#include "../core/amd_pstates.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
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
        if (scenario == 2) reader.signature = 0x00a00f11;
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

int main()
{
    decodeVectors();
    capabilityGuards();
    cpuErrorsStopBeforeMsr();
    successfulSnapshotAndLimit();
    maximumRange();
    rowFailureIsNotAValue();
    limitFailureStopsSampling();
    std::cout << "amd_pstates core: 7 scenario groups passed\n";
}
