// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>

// Synthetic definitions exercise descriptor boundaries; no hardware capture.
inline std::uint64_t pstateElectricalFixture(bool svi3, unsigned row)
{
    static const unsigned svi2Vid[] = {0,1,80,247,248,255,80,80};
    static const unsigned svi3Vid[] = {0,1,256,511,255,336,336,336};
    static const unsigned idd[] = {255,175,175,0,8,255,1,1};
    static const unsigned div[] = {0,1,2,2,3,2,0,0};
    const unsigned vid = svi3 ? svi3Vid[row] : svi2Vid[row];
    return (row == 6 ? 0 : UINT64_C(1) << 63) | (row == 7 ? UINT64_C(1) << 33 : 0) |
        (std::uint64_t(vid >> 8) << 32) | (std::uint64_t(div[row]) << 30) |
        (std::uint64_t(idd[row]) << 22) | (std::uint64_t(vid & 255) << 14) | 0x08a0;
}
