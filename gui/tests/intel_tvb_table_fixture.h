// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_tvb_fixture.h"
#include <array>

struct IntelTvbTableFixture : IntelTvbFixture {
    std::array<std::uint32_t,8> entries{{0x02644601,0x03694602,0x046a4703,0x056b4804,0x066c4905,0x076d4a06,0x086e4b07,0x096f4c08}};
    std::array<unsigned,8> statuses{{0,0,0,0,0,0,0,0}};
    unsigned busyEntry = 0;
    std::vector<unsigned> selectors;
    octool::core::Reply execute(const octool::core::Request &r) override {
        if (r.space != octool::core::Space::Msr || r.address != 0x150 || !r.write || ((r.value >> 32) & 255) != 0x24)
            return IntelTvbFixture::execute(r);
        requests.push_back(r); octool::core::Reply out;
        if (failing(r.cpu)) { out.error = -EACCES; return out; }
        const unsigned selected = unsigned((r.value >> 40) & 255);
        if (selected < 1 || selected > 8 || r.width != 8 ||
            r.value != (UINT64_C(0x8000002400000000) | (std::uint64_t(selected) << 40))) {
            out.error = -EINVAL; return out;
        }
        commands.push_back(0x24); selectors.push_back(selected);
        mailbox = (std::uint64_t(statuses[selected-1]) << 32) | entries[selected-1];
        if (busyEntry == selected) mailbox |= UINT64_C(1) << 63;
        if (afterRequest) afterRequest();
        return out;
    }
    void clearTrace() { IntelTvbFixture::clearTrace(); selectors.clear(); }
};
