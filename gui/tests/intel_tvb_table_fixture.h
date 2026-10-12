// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_tvb_fixture.h"
#include <array>

struct IntelTvbTableFixture : IntelTvbFixture {
    std::array<std::uint32_t,8> entries{{0x02644601,0x03694602,0x046a4703,0x056b4804,0x066c4905,0x076d4a06,0x086e4b07,0x096f4c08}};
    std::array<unsigned,8> statuses{{0,0,0,0,0,0,0,0}};
    unsigned busyEntry = 0;
    std::vector<unsigned> selectors;
    unsigned tableSettingsAttempts() const {
        unsigned count = 0;
        for (const auto &r : requests) if (r.write && ((r.value >> 32) & 255) == 0x25) ++count;
        return count;
    }
    octool::core::Reply execute(const octool::core::Request &r) override {
        const unsigned command = unsigned((r.value >> 32) & 255);
        if (r.space != octool::core::Space::Msr || r.address != 0x150 || !r.write || (command != 0x24 && command != 0x25))
            return IntelTvbFixture::execute(r);
        requests.push_back(r); octool::core::Reply out;
        if (failing(r.cpu)) { out.error = -EACCES; return out; }
        const unsigned selected = unsigned((r.value >> 40) & 255);
        if (selected < 1 || selected > 8 || r.width != 8 ||
            (r.value >> 32) != (UINT64_C(0x80000000) | command | (std::uint64_t(selected) << 8)) ||
            (command == 0x24 && std::uint32_t(r.value))) {
            out.error = -EINVAL; return out;
        }
        commands.push_back(command);
        const unsigned response = command == failCommand ? status : command == 0x24 ? statuses[selected-1] : 0;
        if (command == 0x24) selectors.push_back(selected);
        else if (!response) {
            ++mutations;
            if (!discardChange) entries[selected-1] = std::uint32_t(r.value);
        }
        mailbox = (std::uint64_t(response) << 32) | entries[selected-1];
        if (command == 0x24 && busyEntry == selected) mailbox |= UINT64_C(1) << 63;
        if (afterRequest) afterRequest();
        return out;
    }
    void clearTrace() { IntelTvbFixture::clearTrace(); selectors.clear(); }
};
