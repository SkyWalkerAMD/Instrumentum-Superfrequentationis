// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_curve_fixture.h"
#include "amd_pm_fixture.h"

struct AmdPmNativeFixture : AmdCurveFixture {
    std::uint32_t version = 0x730204, size = 2844, versionAfter = 0, sizeAfter = 0;
    std::uint64_t base = 0x123456000ull, baseAfter = 0;
    unsigned physicalBits = 48, versionQueries = 0, addressQueries = 0, memoryReads = 0;
    unsigned pmCommands = 0, smuQueries = 0, presenceQueries = 0;
    std::uint32_t smuVersion = 0x732e00, smuVersionAfter = 0;
    unsigned presentMask = 0x8001, presentMaskAfter = UINT32_MAX;
    int presenceFailAt = -1;
    std::uint32_t presenceFailure = 0xfd;
    bool hypervisor = false, addressArgsOnes = true;
    std::uint32_t commandRegister = 0x3b10924, responseRegister = 0x3b10970;
    std::array<std::uint32_t, 6> args{};
    std::array<std::uint32_t, 5> completions{{1,1,1,1,1}};
    std::vector<std::array<std::uint32_t, 6>> packets;
    std::vector<unsigned> pciCalls, memoryCalls, presenceCalls;
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
            if (r.value != responseRegister && r.value != commandRegister && !(r.value >= 0x3b10a40 && r.value <= 0x3b10a54 && !(r.value & 3))) out.error = -EINVAL;
            else index = std::uint32_t(r.value);
        } else if (r.address != 0xbc) out.error = -EINVAL;
        else if (r.write && index == responseRegister && !r.value) response = 0;
        else if (r.write && index >= 0x3b10a40 && index <= 0x3b10a54) args[(index - 0x3b10a40)/4] = std::uint32_t(r.value);
        else if (r.write && index == commandRegister && r.value >= 3 && r.value <= 5 && pmCommands < 5) {
            packets.push_back(args); messages.push_back(std::uint32_t(r.value));
            response = completions[pmCommands++]; ++commands; polls = 0;
            for (unsigned i = 0; i < 6; ++i) if (args[i] != (addressArgsOnes && r.value == 4 && i < 2 ? 1u : 0u)) out.error = -EINVAL;
            if (r.value == 5) {
                ++versionQueries;
                args[0] = versionQueries > 1 && versionAfter ? versionAfter : version;
                args[1] = versionQueries > 1 && sizeAfter ? sizeAfter : size;
            } else if (r.value == 4) {
                ++addressQueries;
                const auto b = addressQueries > 1 && baseAfter ? baseAfter : base;
                args[0] = std::uint32_t(b); args[1] = std::uint32_t(b >> 32);
            }
        } else if (r.write && index == commandRegister && r.value == 2) {
            packets.push_back(args); messages.push_back(2); ++commands; ++smuQueries; polls = 0; response = 1;
            for (auto arg : args) if (arg) out.error = -EINVAL;
            args[0] = smuQueries > 1 && smuVersionAfter ? smuVersionAfter : smuVersion;
        } else if (r.write && index == commandRegister && r.value == 0xa5) {
            packets.push_back(args); messages.push_back(0xa5); presenceCalls.push_back(calls-1); ++commands; polls = 0;
            const unsigned target = args[0];
            if (target > 15 || target != presenceQueries % 16) out.error = -EINVAL;
            for (unsigned i = 1; i < 6; ++i) if (args[i]) out.error = -EINVAL;
            const auto mask = presenceQueries >= 16 && presentMaskAfter != UINT32_MAX ? presentMaskAfter : presentMask;
            response = int(presenceQueries) == presenceFailAt ? presenceFailure : mask & (1u << target) ? 1 : 0xfe;
            ++presenceQueries; args[0] = 0x01234567; // Clock payload is not rail voltage or a mask.
        } else if (!r.write && index == responseRegister) { ++polls; out.value = !commands && busyBefore ? 0 : response; }
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
