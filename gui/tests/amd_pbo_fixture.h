// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_curve_fixture.h"
#include <cstring>

// BIOS, MP1 and unrecognized commands fail. Manual OC mode/frequency are
// separately opted in; existing PBO tests still reject implicit OC changes.
struct AmdPboFixture : AmdCurveFixture {
    std::uint32_t scalarRaw = 0x3f800000;
    std::uint32_t capsRaw = 4, fmaxMHz = 0;
    bool allowManualOc = false, overrideModeCaps = false;
    bool allowAllCoreCurve = false;
    bool allowStatusQueries = false;
    std::uint32_t ln2Raw = 0, performanceRaw = 0;
    std::uint32_t allCurveArgument = UINT32_MAX;
    std::uint32_t modeCapsAfter = 0, ocAllMHz = 0, ocCoreArgument = 0;
    std::array<std::uint32_t, 4> limitArguments{{0,0,0,0}};
    std::array<std::uint32_t, 6> scalarArgs{{11,22,33,44,55,66}};
    std::vector<std::array<std::uint32_t, 6>> commandArgs;
    octool::core::Reply execute(const octool::core::Request &r) override {
        using namespace octool::core;
        requests.push_back(r); Reply out;
        if (failing()) { out.error = -EACCES; out.value = UINT64_MAX; return out; }
        if (r.space != Space::Pci || r.width != 4 || r.bus || r.device || r.function) out.error = -EINVAL;
        else if (!r.write && r.address == 0) out.value = pciId;
        else if (r.write && r.address == 0xb8) {
            if (r.value != 0x3b10970 && r.value != 0x3b10924 &&
                (r.value < 0x3b10a40 || r.value > 0x3b10a54 || (r.value & 3))) out.error = -EINVAL;
            else index = std::uint32_t(r.value);
        } else if (r.address != 0xbc) out.error = -EINVAL;
        else if (r.write) {
            if (index == 0x3b10970 && !r.value) response = 0;
            else if (index >= 0x3b10a40 && index <= 0x3b10a54) scalarArgs[(index-0x3b10a40)/4] = std::uint32_t(r.value);
            else if (index == 0x3b10924 && (r.value == 0x6d || r.value == 0x5b || r.value == 0x6f || r.value == 0x70 || (r.value >= 0x56 && r.value <= 0x59) ||
                     (allowManualOc && r.value >= 0x5d && r.value <= 0x60) || (allowAllCoreCurve && r.value == 7) ||
                     (allowStatusQueries && (r.value == 0xa6 || r.value == 0x5c)))) {
                const bool query = r.value == 0x6d || r.value == 0x6f || r.value == 0xa6 || r.value == 0x5c;
                for (unsigned i = query && r.value != 0x5c ? 0 : 1; i < 6; ++i) if (scalarArgs[i]) out.error = -EINVAL;
                if (r.value == 0x5c && scalarArgs[0] > 127) out.error = -EINVAL;
                if (r.value == 0x5b && (scalarArgs[0] < 100 || scalarArgs[0] > 1000 || scalarArgs[0] % 100)) out.error = -EINVAL;
                if (r.value == 0x70 && (!scalarArgs[0] || scalarArgs[0] > 0xfffff)) out.error = -EINVAL;
                if (r.value >= 0x56 && r.value <= 0x58 && (!scalarArgs[0] || scalarArgs[0] > 2000000 || scalarArgs[0] % 1000)) out.error = -EINVAL;
                if (r.value == 0x59 && (!scalarArgs[0] || scalarArgs[0] > 100)) out.error = -EINVAL;
                if ((r.value == 0x5d || r.value == 0x5e) && scalarArgs[0]) out.error = -EINVAL;
                if (r.value == 0x5f && (!scalarArgs[0] || scalarArgs[0] > 0xfffff)) out.error = -EINVAL;
                if (r.value == 0x60 && (!(scalarArgs[0] & 0xfffff) || (scalarArgs[0] & 0x0f800000u))) out.error = -EINVAL;
                if (r.value == 7 && !(scalarArgs[0] <= 50 || (scalarArgs[0] >= 0xffce && scalarArgs[0] <= 0xffff))) out.error = -EINVAL;
                if (!out.error) {
                    ++commands; polls = 0; messages.push_back(std::uint32_t(r.value)); commandArgs.push_back(scalarArgs);
                    response = query ? completion : writeCompletion;
                    if (!query) {
                        ++settings;
                        if (writeCompletion == 1 && !discardChange) {
                            if (r.value == 7) allCurveArgument = scalarArgs[0];
                            else if (r.value == 0x70) fmaxMHz = scalarArgs[0];
                            else if (r.value == 0x5d || r.value == 0x5e) capsRaw = overrideModeCaps ? modeCapsAfter : r.value == 0x5d ? capsRaw | 1u : capsRaw & ~1u;
                            else if (r.value == 0x5f) ocAllMHz = scalarArgs[0];
                            else if (r.value == 0x60) ocCoreArgument = scalarArgs[0];
                            else if (r.value >= 0x56 && r.value <= 0x59) limitArguments[std::size_t(r.value-0x56)] = scalarArgs[0];
                            else { const float value = float(scalarArgs[0]/100); std::memcpy(&scalarRaw, &value, 4); }
                        }
                    } else if (completion == 1) scalarArgs[0] = r.value == 0x6d ? scalarRaw : r.value == 0x6f ? capsRaw : r.value == 0xa6 ? ln2Raw : performanceRaw;
                }
            } else out.error = -EINVAL;
        } else if (index == 0x3b10970) {
            ++polls; out.value = (!commands && busyBefore) || (commands && polls <= busyPolls) ? 0 : response;
        } else if (index == 0x3b10a40) { ++argumentReads; out.value = scalarArgs[0]; }
        else out.error = -EINVAL;
        if (afterRequest) afterRequest();
        return out;
    }
};
