// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_curve_fixture.h"
#include "amd_pm_fixture.h"

struct AmdPmNativeFixture : AmdCurveFixture {
    std::uint32_t version = 0x730204, size = 2844, versionAfter = 0, sizeAfter = 0;
    std::uint64_t base = 0x123456000ull, baseAfter = 0;
    unsigned physicalBits = 48, versionQueries = 0, addressQueries = 0, memoryReads = 0;
    bool hypervisor = false;
    std::array<std::uint32_t, 6> args{};
    std::array<std::uint32_t, 5> completions{{1,1,1,1,1}};
    std::vector<std::array<std::uint32_t, 6>> packets;
    std::vector<unsigned> pciCalls, memoryCalls;
    std::vector<std::uint8_t> bytes = pm_fixture::bytes();
    octool::core::Reply execute(const octool::core::Request &r) override {
        using namespace octool::core;
        requests.push_back(r); Reply out;
        (r.space == Space::Memory ? memoryCalls : pciCalls).push_back(calls);
        if (failing()) { out.error = -EACCES; out.value = UINT64_MAX; return out; }
        if (r.space == Space::Memory) {
            if (r.write || r.width != 4 || r.address < base || r.address - base > bytes.size() - 4 || (r.address & 3)) out.error = -EINVAL;
            else {
                ++memoryReads;
                for (unsigned j = 0; j < 4; ++j) out.value |= std::uint64_t(bytes.at(std::size_t(r.address-base)+j)) << (8*j);
            }
        } else if (r.space != Space::Pci || r.width != 4 || r.bus || r.device || r.function) out.error = -EINVAL;
        else if (!r.write && r.address == 0) out.value = pciId;
        else if (r.write && r.address == 0xb8) {
            if (r.value != 0x3b10970 && r.value != 0x3b10924 && !(r.value >= 0x3b10a40 && r.value <= 0x3b10a54 && !(r.value & 3))) out.error = -EINVAL;
            else index = std::uint32_t(r.value);
        } else if (r.address != 0xbc) out.error = -EINVAL;
        else if (r.write && index == 0x3b10970 && !r.value) response = 0;
        else if (r.write && index >= 0x3b10a40 && index <= 0x3b10a54) args[(index - 0x3b10a40)/4] = std::uint32_t(r.value);
        else if (r.write && index == 0x3b10924 && r.value >= 3 && r.value <= 5 && commands < 5) {
            packets.push_back(args); messages.push_back(std::uint32_t(r.value));
            response = completions[commands++]; polls = 0;
            for (unsigned i = 0; i < 6; ++i) if (args[i] != (r.value == 4 && i < 2 ? 1u : 0u)) out.error = -EINVAL;
            if (r.value == 5) {
                ++versionQueries;
                args[0] = versionQueries > 1 && versionAfter ? versionAfter : version;
                args[1] = versionQueries > 1 && sizeAfter ? sizeAfter : size;
            } else if (r.value == 4) {
                ++addressQueries;
                const auto b = addressQueries > 1 && baseAfter ? baseAfter : base;
                args[0] = std::uint32_t(b); args[1] = std::uint32_t(b >> 32);
            }
        } else if (!r.write && index == 0x3b10970) { ++polls; out.value = !commands && busyBefore ? 0 : response; }
        else if (!r.write && index >= 0x3b10a40 && index <= 0x3b10a44) out.value = args[(index - 0x3b10a40)/4];
        else out.error = -EINVAL;
        if (afterRequest) afterRequest();
        return out;
    }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        if (leaf == 0x80000000 || leaf == 0x80000008) {
            octool::core::CpuIdReply out;
            if (cpu != expectedCpu) ++wrongCpu;
            if (failing()) { out.error = -EACCES; return out; }
            out.words[0] = leaf == 0x80000000 ? 0x80000008 : physicalBits;
            if (afterRequest) afterRequest();
            return out;
        }
        auto out = AmdCurveFixture::cpuid(cpu, leaf, subleaf);
        if (leaf == 1 && hypervisor) out.words[2] |= 0x80000000u;
        return out;
    }
};
