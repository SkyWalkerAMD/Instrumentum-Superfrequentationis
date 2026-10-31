// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../core/amd_topology.h"
#include <cassert>
#include <cerrno>
#include <map>
namespace amd_topology_fixture {
struct Device : octool::core::HardwareBackend {
    unsigned expectedCpu=130,calls=0,failAt=UINT32_MAX,topologyReads=0,l3Reads=0,registerRequests=0;
    std::uint32_t apic=0x1f;
    std::uint32_t signature=0x00b40f40;
    bool extension=true,changeApic=false,changeCache=false;
    std::vector<std::array<std::uint32_t,4>> caches;
    std::vector<std::array<std::uint32_t,3>> levels;
    Device() {
        // Model 44h: type 3 is reserved (PPR 57896 p120). Complex == CCD.
        levels={{{1,2,0x100}},{{4,16,0x201}},{{5,32,0x402}}};
        caches={{{1u|(1u<<5),63u|(7u<<22),63u,0}},
                {{3u|(2u<<5)|(1u<<14),63u|(7u<<22),2047u,0}},
                {{3u|(3u<<5)|(15u<<14),63u|(47u<<22),32767u,0}}};
    }
    octool::core::Reply execute(const octool::core::Request &) override { ++registerRequests; assert(false); return {}; }
    octool::core::CpuIdReply cpuid(unsigned cpu,std::uint32_t leaf,std::uint32_t subleaf) override {
        using octool::core::CpuIdReply; CpuIdReply r;
        assert(cpu==expectedCpu); if(calls++==failAt) { r.error=-EACCES; return r; }
        if(!leaf) { r.words[0]=1; r.words[1]=0x68747541; r.words[2]=0x444d4163; r.words[3]=0x69746e65; }
        else if(leaf==1) r.words[0]=signature;
        else if(leaf==0x80000000) r.words[0]=0x80000026;
        else if(leaf==0x80000001) r.words[2]=extension?1u<<22:0;
        else if(leaf==0x80000026) {
            if(!subleaf) ++topologyReads;
            if(subleaf<levels.size()) {
                for(unsigned i=0;i<3;++i) r.words[i]=levels[subleaf][i];
                r.words[3]=apic+(changeApic && topologyReads>1 ? 0x20:0);
            }
        } else if(leaf==0x8000001d) {
            if(subleaf<caches.size()) for(unsigned i=0;i<4;++i) r.words[i]=caches[subleaf][i];
            if(subleaf==2 && ++l3Reads>1 && changeCache) r.words[1]^=1u<<22;
        } else assert(false);
        return r;
    }
    octool::core::Backend backend(octool::core::Space) const override { return octool::core::Backend::Unavailable; }
};
struct CatalogDevice : octool::core::HardwareBackend {
    std::map<unsigned,Device> devices;
    unsigned calls=0,failAt=UINT32_MAX,registerRequests=0,changeCpu=UINT32_MAX,changeAfterReads=2;
    bool changed=false;
    explicit CatalogDevice(const std::vector<unsigned> &cpus) {
        for(unsigned i=0;i<cpus.size();++i) {
            auto &d=devices[cpus[i]]; d.expectedCpu=cpus[i]; d.apic=0x80+i;
            d.caches[2][1]=63u|((((i>>4)&1)?15u:47u)<<22);
        }
    }
    octool::core::Reply execute(const octool::core::Request &) override { ++registerRequests; assert(false); return {}; }
    octool::core::CpuIdReply cpuid(unsigned cpu,std::uint32_t leaf,std::uint32_t subleaf) override {
        if(calls++==failAt || !devices.count(cpu)) { octool::core::CpuIdReply r; r.error=-EACCES; return r; }
        auto &d=devices.at(cpu);
        if(cpu==changeCpu && !changed && d.topologyReads>=changeAfterReads) { d.apic+=0x20; changed=true; }
        return d.cpuid(cpu,leaf,subleaf);
    }
    octool::core::Backend backend(octool::core::Space) const override { return octool::core::Backend::Unavailable; }
};
}
