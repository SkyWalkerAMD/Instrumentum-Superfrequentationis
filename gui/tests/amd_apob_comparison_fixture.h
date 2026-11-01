// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_apob_fixture.h"
#include "amd_topology_fixture.h"
#include "amd_9950x3d_topology_fixture.h"
namespace amd_apob_comparison_fixture {
// Synthetic APOB input used by the original-byte enumeration replay: stored
// group IDs [4,0], stored core IDs [15..8]. They deliberately differ from the
// constructor's positions. No runtime APOB capture is implied.
inline std::vector<std::uint8_t> record() {
    std::vector<std::uint8_t> b(456,255);
    for(unsigned i=0;i<48;++i) b[i]=0;
    amd_apob_fixture::word(b,0,3); amd_apob_fixture::word(b,4,3); amd_apob_fixture::word(b,12,456);
    for(unsigned g=0;g<2;++g) {
        const unsigned at=48+51*g; b[at]=g?0:4; b[at+1]=0;
        for(unsigned c=0;c<8;++c) { b[at+2+3*c]=15-c; b[at+3+3*c]=1; b[at+4+3*c]=1; }
    }
    return b;
}
inline void configure(amd_topology_fixture::CatalogDevice &d,const std::vector<unsigned> &cpus) {
    assert(cpus.size()<=32);
    for(unsigned i=0;i<cpus.size();++i) {
        auto &cpu=d.devices.at(cpus[i]);
        amd_topology_fixture::published9950x3d(cpu,31-i);
        cpu.maximum=0xb; cpu.extendedSignature=cpu.signature;
    }
}
}
