// SPDX-License-Identifier: GPL-2.0-only
#include "amd_topology.h"
#include <cerrno>

namespace octool { namespace core {
namespace {
std::uint32_t mask(unsigned bits) { return std::uint32_t((UINT64_C(1)<<bits)-1); }
}
AmdTopology readAmdTopology(HardwareSession &s,unsigned cpu) {
    AmdTopology out; out.cpu=cpu; out.identity=identifyCpu(s,cpu);
    if((out.error=out.identity.error)) return out;
    if(!out.identity.amd) { out.error=-ENOTSUP; return out; }
    auto r=s.cpuid(cpu,0x80000000);
    if((out.error=r.error)) return out;
    if(r.words[0]<0x80000026) { out.error=-ENOTSUP; return out; }
    unsigned previousType=0, previousShift=0, previousCount=0;
    unsigned coreShift=0,ccdShift=0,socketShift=0;
    bool haveCore=false,haveCcd=false,haveSocket=false,terminated=false;
    for(unsigned subleaf=0;subleaf<8;++subleaf) {
        r=s.cpuid(cpu,0x80000026,subleaf);
        if((out.error=r.error)) return out;
        const unsigned count=r.words[1]&0xffff;
        if(!count) { terminated=true; break; }
        const unsigned type=(r.words[2]>>8)&255,shift=r.words[0]&31;
        if((r.words[2]&255)!=subleaf || type<=previousType || type>4 || shift<previousShift ||
            count<previousCount || std::uint64_t(count)>(UINT64_C(1)<<shift) ||
            (!out.levels.empty() && r.words[3]!=out.apicId)) {
            out.error=-EINVAL; return out;
        }
        out.apicId=r.words[3];
        AmdTopologyLevel level; level.type=type; level.shift=shift; level.logicalCount=count;
        level.globalId=out.apicId>>shift;
        for(unsigned i=0;i<4;++i) level.words[i]=r.words[i];
        out.levels.push_back(level);
        if(type==1) { haveCore=true; coreShift=shift; }
        if(type==3) { haveCcd=true; ccdShift=shift; }
        if(type==4) { haveSocket=true; socketShift=shift; }
        previousType=type; previousShift=shift; previousCount=count;
    }
    if(!terminated || !haveCore || !haveCcd || !haveSocket) { out.error=-ENOTSUP; return out; }
    out.socketId=out.apicId>>socketShift;
    out.ccdInSocket=(out.apicId>>ccdShift)&mask(socketShift-ccdShift);
    out.coreInCcd=(out.apicId>>coreShift)&mask(ccdShift-coreShift);
    out.threadInCore=out.apicId&mask(coreShift);
    return out;
}
} }
