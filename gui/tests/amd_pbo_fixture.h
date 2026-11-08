// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_curve_fixture.h"
#include <cstring>

// BIOS, MP1 and unrecognized commands fail. Manual OC mode/frequency are
// separately opted in; existing PBO tests still reject implicit OC changes.
struct AmdPboFixture : AmdCurveFixture {
    std::uint32_t scalarRaw = 0x3f800000;
    std::uint32_t capsRaw = 4, fmaxMHz = 0;
    bool allowFmaxReadback = false, clampFmax = false;
    bool graniteFmax = false, hypervisor = false;
    bool allowGraniteScalar = false, clampScalar = false;
    bool allowGraniteLimits = false, allowGraniteMode = false;
    bool allowGraniteCurve = false;
    bool allowGraniteShaper = false, allowGraniteShaperWrite = false;
    unsigned shaperReads = 0, shaperWrites = 0;
    std::uint32_t shaperArgument = 0;
    std::array<std::uint32_t, 5> shaperWords{{0,1,2,3,4}};
    std::vector<unsigned> shaperReadIndices;
    std::uint32_t scalarClampedRaw = 0x40200000;
    void useGraniteScalar() { useGraniteFmax(); allowGraniteScalar = true; }
    void useGraniteLimits() { useGraniteFmax(); allowGraniteLimits = true; }
    void useGraniteMode() { useGraniteFmax(); allowGraniteMode = allowManualOc = true; }
    void useGraniteCurve() { useGraniteFmax(); allowGraniteCurve = allowAllCoreCurve = true; }
    void useGraniteShaper() { useGraniteFmax(); allowGraniteShaper = true; }
    void useGraniteShaperWrite() { useGraniteShaper(); allowGraniteShaperWrite = true; }
    void useGraniteFmax() {
        graniteFmax = allowFmaxReadback = true;
        model = 0x44; pciId = 0x14d81022; firmwareVersion = 0x00625400;
    }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        auto out = AmdCurveFixture::cpuid(cpu,leaf,subleaf);
        if (!out.error && leaf == 1 && hypervisor) out.words[2] |= 0x80000000u;
        return out;
    }
    std::uint32_t fmaxClampedMHz = 6000;
    bool allowManualOc = false, overrideModeCaps = false;
    bool allowAllCoreCurve = false;
    bool allowStatusQueries = false;
    bool allowPowerAdjustment = false;
    std::uint32_t firmwareVersion = 0x00732e00, powerAdjustmentRaw = 0;
    std::uint32_t ln2Raw = 0, performanceRaw = 0;
    std::uint32_t allCurveArgument = UINT32_MAX;
    std::uint32_t modeCapsAfter = 0, ocAllMHz = 0, ocCoreArgument = 0;
    std::array<std::uint32_t, 4> limitArguments{{0,0,0,0}};
    std::array<std::uint32_t, 6> scalarArgs{{11,22,33,44,55,66}};
    std::vector<std::array<std::uint32_t, 6>> commandArgs;
    octool::core::Reply execute(const octool::core::Request &r) override {
        using namespace octool::core;
        requests.push_back(r); Reply out;
        const unsigned responseAddress = graniteFmax ? 0x3b10570 : 0x3b10970;
        const unsigned commandAddress = graniteFmax ? 0x3b10524 : 0x3b10924;
        if (failing()) { out.error = -EACCES; out.value = UINT64_MAX; return out; }
        if (r.space != Space::Pci || r.width != 4 || r.bus || r.device || r.function) out.error = -EINVAL;
        else if (!r.write && r.address == 0) out.value = pciId;
        else if (r.write && r.address == 0xb8) {
            if (r.value != responseAddress && r.value != commandAddress &&
                (r.value < 0x3b10a40 || r.value > 0x3b10a54 || (r.value & 3))) out.error = -EINVAL;
            else index = std::uint32_t(r.value);
        } else if (r.address != 0xbc) out.error = -EINVAL;
        else if (r.write) {
            if (index == responseAddress && !r.value) response = 0;
            else if (index >= 0x3b10a40 && index <= 0x3b10a54) scalarArgs[(index-0x3b10a40)/4] = std::uint32_t(r.value);
            else if (index == commandAddress && (!graniteFmax || r.value == 2 || r.value == 0x6e || r.value == 0x6f || r.value == 0x70 ||
                     (allowGraniteScalar && (r.value == 0x5b || r.value == 0x6d)) ||
                     (allowGraniteLimits && r.value >= 0x56 && r.value <= 0x59) ||
                     (allowGraniteMode && (r.value == 0x5d || r.value == 0x5e)) ||
                     (allowGraniteCurve && r.value == 7) || (allowGraniteShaper && r.value == 0x84) ||
                     (allowGraniteShaperWrite && r.value == 0xa6)) &&
                     (r.value == 0x6d || r.value == 0x5b || r.value == 0x6f || r.value == 0x70 || (r.value >= 0x56 && r.value <= 0x59) ||
                     (allowManualOc && r.value >= 0x5d && r.value <= 0x60) || (allowAllCoreCurve && r.value == 7) ||
                     (allowStatusQueries && (r.value == 0xa6 || r.value == 0x5c)) ||
                     (allowPowerAdjustment && (r.value == 2 || r.value == 0xa7 || r.value == 0xa8)) ||
                     (allowFmaxReadback && (r.value == 2 || r.value == 0x6e)) || (allowGraniteShaper && r.value == 0x84) ||
                     (allowGraniteShaperWrite && r.value == 0xa6))) {
                const bool shaperSetting = graniteFmax && allowGraniteShaperWrite && r.value == 0xa6;
                const bool query = r.value == 0x6d || r.value == 0x6e || r.value == 0x6f ||
                    (r.value == 0xa6 && !shaperSetting) || r.value == 0x5c || r.value == 2 || r.value == 0xa8 || r.value == 0x84;
                for (unsigned i = query && r.value != 0x5c ? 0 : 1; i < 6; ++i) if (scalarArgs[i]) out.error = -EINVAL;
                if (r.value == 0x5c && scalarArgs[0] > 127) out.error = -EINVAL;
                if (r.value == 0x5b && (scalarArgs[0] < 100 || scalarArgs[0] > 1000 || scalarArgs[0] % 100)) out.error = -EINVAL;
                if (r.value == 0x70 && (!scalarArgs[0] || scalarArgs[0] > 0xfffff)) out.error = -EINVAL;
                if (r.value >= 0x56 && r.value <= 0x58 && (!scalarArgs[0] || scalarArgs[0] > 2000000 || scalarArgs[0] % 1000)) out.error = -EINVAL;
                if (r.value == 0x59 && (!scalarArgs[0] || scalarArgs[0] > 100)) out.error = -EINVAL;
                if ((r.value == 0x5d || r.value == 0x5e) && scalarArgs[0]) out.error = -EINVAL;
                if (r.value == 0x5f && (!scalarArgs[0] || scalarArgs[0] > 0xfffff)) out.error = -EINVAL;
                if (r.value == 0x60 && (!(scalarArgs[0] & 0xfffff) || (scalarArgs[0] & 0x0f800000u))) out.error = -EINVAL;
                if (r.value == 7 && !(scalarArgs[0] <= (graniteFmax ? 30u : 50u) || (scalarArgs[0] >= 0xffce && scalarArgs[0] <= 0xffff))) out.error = -EINVAL;
                if (r.value == 0xa7 && !(scalarArgs[0] <= 5 || scalarArgs[0] >= 0xfffffff6u)) out.error = -EINVAL;
                if (shaperSetting && (scalarArgs[0] & 0x7fu) >= 5) out.error = -EINVAL;
                if (!out.error) {
                    ++commands; polls = 0; messages.push_back(std::uint32_t(r.value)); commandArgs.push_back(scalarArgs);
                    response = query ? completion : writeCompletion;
                    if (!query) {
                        ++settings;
                        if (shaperSetting) {
                            ++shaperWrites; shaperArgument = scalarArgs[0];
                            if (!(capsRaw & 4u)) response = 0xff;
                        }
                        if (response == 1 && !discardChange) {
                            if (shaperSetting) {
                                shaperWords[scalarArgs[0] & 0x7fu] = scalarArgs[0];
                                // A6 rewrites every saved offset after signed8
                                // clamping, including the four unselected tiers.
                                for (unsigned tier=0;tier<shaperWords.size();++tier) {
                                    const auto word=shaperWords[tier];std::uint32_t normalized=tier;
                                    for (unsigned point=0;point<3;++point) {
                                        const unsigned byte=(word>>(8+8*point))&255u;
                                        int count=byte<128?int(byte):int(byte)-256;
                                        if(count < -50)count=-50;
                                        if(count > 30)count=30;
                                        normalized|=(std::uint32_t(count)&255u)<<(8+8*point);
                                    }
                                    shaperWords[tier]=normalized;
                                }
                            } else if (r.value == 0xa7) powerAdjustmentRaw = scalarArgs[0];
                            else if (r.value == 7) allCurveArgument = scalarArgs[0];
                            else if (r.value == 0x70) fmaxMHz = clampFmax ? fmaxClampedMHz : scalarArgs[0];
                            else if (r.value == 0x5d || r.value == 0x5e) capsRaw = overrideModeCaps ? modeCapsAfter : r.value == 0x5d ? capsRaw | 0x10000u : capsRaw & ~0x10000u;
                            else if (r.value == 0x5f) ocAllMHz = scalarArgs[0];
                            else if (r.value == 0x60) ocCoreArgument = scalarArgs[0];
                            else if (r.value >= 0x56 && r.value <= 0x59) limitArguments[std::size_t(r.value-0x56)] = scalarArgs[0];
                            else { const float value = float(scalarArgs[0]/100); std::memcpy(&scalarRaw, &value, 4);
                                if (clampScalar) scalarRaw = scalarClampedRaw; }
                        }
                    } else if (completion == 1) {
                        if (r.value == 0x84) {
                            for (unsigned i=0;i<shaperWords.size();++i) scalarArgs[i]=shaperWords[i];
                        } else scalarArgs[0] = r.value == 2 ? firmwareVersion : r.value == 0x6e ? fmaxMHz : r.value == 0xa8 ? powerAdjustmentRaw : r.value == 0x6d ?
                            (graniteFmax && (capsRaw & 0x10000u) ? 0u : scalarRaw) : r.value == 0x6f ? capsRaw : r.value == 0xa6 ? ln2Raw : performanceRaw;
                    }
                }
            } else out.error = -EINVAL;
        } else if (index == responseAddress) {
            ++polls; out.value = (!commands && busyBefore) || (commands && polls <= busyPolls) ? 0 : response;
        } else if (allowGraniteShaper && !messages.empty() && messages.back()==0x84 &&
                   index>=0x3b10a40 && index<=0x3b10a50) {
            const unsigned slot=(index-0x3b10a40)/4;
            ++argumentReads; ++shaperReads; shaperReadIndices.push_back(slot); out.value=scalarArgs[slot];
        } else if (index == 0x3b10a40) { ++argumentReads; out.value = scalarArgs[0]; }
        else out.error = -EINVAL;
        if (afterRequest) afterRequest();
        return out;
    }
};
