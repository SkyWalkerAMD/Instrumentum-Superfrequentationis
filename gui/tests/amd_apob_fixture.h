// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <vector>
namespace amd_apob_fixture {
inline void word(std::vector<std::uint8_t> &b,unsigned at,std::uint32_t value) {
    for(unsigned i=0;i<4;++i) b[at+i]=std::uint8_t(value>>(8*i));
}
// Synthetic record; not a hardware capture. Literal offsets correspond to the
// pinned getter disassembly, independently of the product decoder's traversal.
inline std::vector<std::uint8_t> record() {
    std::vector<std::uint8_t> b(456,255);
    for(unsigned i=0;i<48;++i) b[i]=0;
    word(b,0,3); word(b,4,3); word(b,8,0x102); word(b,12,456);
    b[0x30]=8; b[0x31]=1;
    b[0x32]=6; b[0x33]=1; b[0x34]=1;
    b[0x35]=2; b[0x36]=0; b[0x37]=1;
    b[0x63]=4; b[0x64]=0;
    b[0x65]=7; b[0x66]=2; b[0x67]=0;
    return b;
}
// Complete synthetic container: header extension, unknown 48-byte record,
// topology at byte 80, optional second instance, unknown trailing record.
inline std::vector<std::uint8_t> container(bool duplicate=false) {
    std::vector<std::uint8_t> b(duplicate?1040:584,0);
    word(b,0,0x424f5041); word(b,4,5); word(b,8,unsigned(b.size())); word(b,12,32);
    word(b,32,1); word(b,36,7); word(b,44,48);
    const auto single=record();
    for(unsigned i=0;i<456;++i) { b[80+i]=single[i]; if(duplicate) b[536+i]=single[i]; }
    const unsigned tail=duplicate?992:536;
    word(b,tail,7); word(b,tail+4,1); word(b,tail+12,48);
    return b;
}
}
