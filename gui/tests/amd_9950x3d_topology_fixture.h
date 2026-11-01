// SPDX-License-Identifier: GPL-2.0-only
// Generated/checked by analysis/tools/firmware-granite-published-topology.py.
// Public register facts: InstLatx64 commit 895c35aba50508c64cd38d8892fe98b0954142d7.
// X670E-E / BIOS 2904. See docs/amd-granite-published-topology.md.
// Capture CPU indices are NOT the user's OS CPU IDs. Leaf terminators and
// backend identity scaffolding are synthetic; no live platform is certified.
#pragma once
#include "amd_topology_fixture.h"
namespace amd_topology_fixture {
inline void published9950x3d(Device &d,unsigned captureCpu) {
    assert(captureCpu<32);
    d.signature=0x00b40f40; d.apic=captureCpu;
    d.levels={{{0x1u,0x2u,0x100u}},{{0x4u,0x10u,0x201u}},{{0x4u,0x10u,0x302u}},{{0x5u,0x20u,0x403u}}};
    d.caches={{{0x4121u,0x2c0003fu,0x3fu,0x0u}},{{0x4122u,0x1c0003fu,0x3fu,0x0u}},{{0x4143u,0x3c0003fu,0x3ffu,0x2u}},{{0x3c163u,0x3c0003fu,0x17fffu,0x1u}}};
    if(captureCpu>=16) d.caches[3][2]=0x7fff;
}
}
