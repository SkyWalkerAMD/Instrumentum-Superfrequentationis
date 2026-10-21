// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_curve_fixture.h"
#include <cstring>

// The fixture accepts only the independently checked scalar commands. BIOS,
// MP1, OC-enable and other writes fail even if they would look similar.
struct AmdPboFixture : AmdCurveFixture {
    std::uint32_t scalarRaw = 0x3f800000;
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
            else if (index == 0x3b10924 && (r.value == 0x6d || r.value == 0x5b)) {
                for (unsigned i = r.value == 0x6d ? 0 : 1; i < 6; ++i) if (scalarArgs[i]) out.error = -EINVAL;
                if (r.value == 0x5b && (scalarArgs[0] < 100 || scalarArgs[0] > 1000 || scalarArgs[0] % 100)) out.error = -EINVAL;
                if (!out.error) {
                    ++commands; polls = 0; messages.push_back(std::uint32_t(r.value)); commandArgs.push_back(scalarArgs);
                    response = r.value == 0x6d ? completion : writeCompletion;
                    if (r.value == 0x5b) {
                        ++settings;
                        if (writeCompletion == 1 && !discardChange) {
                            const float value = float(scalarArgs[0]/100); std::memcpy(&scalarRaw, &value, 4);
                        }
                    } else if (completion == 1) scalarArgs[0] = scalarRaw;
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
