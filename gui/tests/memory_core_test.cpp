// SPDX-License-Identifier: GPL-2.0-only
#include "amd_umc.h"
#include "amd_topology.h"
#include "amd_topology_fixture.h"
#include <cassert>
#include <cerrno>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <atomic>

using namespace octool::core;
namespace {
#include "amd_umc_vectors.inc"
void originalVectors() {
    assert(amdUmcFields().size()==212 && amdUmcOffsets(0).size()==56);
    for(unsigned scenario=0;scenario<5;++scenario) {
        std::vector<UmcRegister> registers;
        for(auto offset:amdUmcOffsets(0)) {
            UmcRegister r; r.offset=offset;
            if(scenario==1) r.value=UINT32_MAX;
            if(scenario==2) r.value=0x12345678;
            if(scenario==3) r.value=offset*0x01010101u;
            if(scenario==4) {
                if(offset==0x1204) r.value=0xfff;
                if(offset==0x1208) r.value=0xffff;
                if(offset==0x120c) r.value=0x20000;
                if(offset==0x1210) r.value=(63u<<12)|(63u<<20);
            }
            registers.push_back(r);
        }
        const auto decoded=decodeAmdUmc(registers,0);
        assert(!decoded.error && decoded.values.size()==212);
        for(unsigned i=0;i<212;++i) {
            assert(decoded.values[i].id==i && !decoded.values[i].error);
            assert(decoded.values[i].encoded==originalValues[scenario][i]);
        }
    }
    // The original string search returned the first Tcl for BOTH display groups.
    // The rebuilt view retains both physical fields with stable IDs.
    assert(std::string(amdUmcFields()[32].name)=="Tcl" && amdUmcFields()[32].group==0);
    assert(std::string(amdUmcFields()[203].name)=="Tcl" && amdUmcFields()[203].group==11);
    assert(originalValues[4][32]==0 && originalValues[4][203]==63);
    const unsigned counts[]={7,12,14,15,18,20,17,22,10,27,33,17};
    for(unsigned g=0;g<12;++g) {
        unsigned count=0; for(const auto &f:amdUmcFields()) if(f.group==g) ++count;
        assert(count==counts[g]);
    }
}
void offlineBoundaries() {
    UmcRegister r; r.offset=0x204; r.value=34;
    const auto partial=decodeAmdUmc({r},0);
    assert(!partial.error && partial.values[32].encoded==34);
    assert(partial.values[203].error==-ENODATA && partial.values[203].encoded==0);
    assert(decodeAmdUmc({r,r},0).error==-EINVAL);
    r.offset=0x205; assert(decodeAmdUmc({r},0).error==-EINVAL);
    assert(decodeAmdUmc({},16).error==-EINVAL && amdUmcOffsets(16).empty());
    assert(decodeAmdUmc(std::vector<UmcRegister>(57),0).error==-EINVAL);
}
void refreshSelections() {
    for(unsigned slot=0;slot<16;++slot) {
        UmcRegister r; r.offset=0x260+(slot/4)*0x100+(slot%4)*4; r.value=(123u<<16)|456u;
        const auto d=decodeAmdUmc({r},slot);
        assert(!d.error && !d.values[99].error && !d.values[100].error);
        assert(d.values[99].encoded==123 && d.values[100].encoded==456);
        assert(d.values[100].offset==r.offset);
        assert(d.values[141].error==-ENODATA);
    }
}
struct Fake : HardwareBackend {
    bool amd=true;
    std::uint32_t signature=0x00b00f20, pciId=0x153a1022, index=0, clock=1200;
    unsigned cpuidCalls=0, failAt=UINT32_MAX;
    std::vector<Request> calls;
    std::atomic<bool> *cancel=nullptr;
    Reply execute(const Request &r) override {
        calls.push_back(r); Reply out;
        assert(r.space==Space::Pci && r.width==4 && r.bus==2 && r.device==3 && r.function==1);
        if(calls.size()==failAt) { out.error=-EACCES; out.value=UINT32_MAX; return out; }
        if(r.address==0) { assert(!r.write); out.value=pciId; return out; }
        if(r.address==0xe0) { assert(r.write); index=std::uint32_t(r.value); if(cancel) cancel->store(true); return out; }
        assert(r.address==0xe4 && !r.write);
        out.value=(index&0xfffff)==0x50200 ? clock : 0x12345678;
        return out;
    }
    CpuIdReply cpuid(unsigned cpu,std::uint32_t leaf,std::uint32_t subleaf) override {
        ++cpuidCalls; assert(cpu==7 && !subleaf); CpuIdReply r;
        if(!leaf) { r.words[0]=1; r.words[1]=amd?0x68747541:0x756e6547; r.words[3]=amd?0x69746e65:0x49656e69; r.words[2]=amd?0x444d4163:0x6c65746e; }
        if(leaf==1) r.words[0]=signature;
        return r;
    }
    Backend backend(Space) const override { return Backend::Module; }
};
struct Device {
    Fake *fake; HardwareService service; UmcTarget target;
    Device():fake(new Fake),service(std::unique_ptr<HardwareBackend>(fake)) {
        target.cpu=7; target.bus=2; target.device=3; target.function=1;
    }
    UmcSnapshot read() {
        UmcSnapshot result;
        service.transaction([&](HardwareSession &s) { result=readAmdUmc(s,target); return result.error; });
        return result;
    }
};
void identityGates() {
    Device d; d.target.bank=23;
    assert(d.read().error==-EINVAL && !d.fake->cpuidCalls && d.fake->calls.empty());
    d.target.bank=0; d.target.refreshSlot=16;
    assert(d.read().error==-EINVAL && d.fake->calls.empty());
    d.target.refreshSlot=0; d.fake->amd=false;
    assert(d.read().error==-ENOTSUP && d.fake->calls.empty());
    d.fake->amd=true; d.fake->signature=0x00a00f20;
    assert(d.read().error==-ENOTSUP && d.fake->calls.empty());
    d.fake->signature=0x00b00f20; d.fake->pciId=0x14801022;
    assert(d.read().error==-ENODEV && d.fake->calls.size()==1 && !d.fake->calls[0].write);
}
void explicitRead() {
    for(auto identity:{0x153a1022u,0x14d81022u}) {
        Device d; d.fake->pciId=identity; d.target.bank=22; d.target.refreshSlot=15;
        const auto r=d.read(); assert(!r.error && r.registers.size()==56 && r.decoded.values.size()==212);
        std::set<std::uint64_t> indices;
        for(const auto &request:d.fake->calls) if(request.write) indices.insert(request.value);
        assert(indices.size()==56 && d.fake->calls.size()==113);
        assert(indices.count(0x1650200) && indices.count(0x165056c) && indices.count(0x16505cc));
        assert(!indices.count(0x1650260));
    }
}
void failedReadsStop() {
    for(unsigned failAt=1;failAt<=113;++failAt) {
        Device d; d.fake->failAt=failAt;
        const auto r=d.read(); assert(r.error==-EACCES && r.decoded.values.empty());
        assert(d.fake->calls.size()==failAt);
    }
    for(auto value:{0u,65001u,65535u,UINT32_MAX}) {
        Device d; d.fake->clock=value; const auto r=d.read();
        assert(r.error==-ENODATA && r.decoded.values.empty() && d.fake->calls.size()==3);
    }
}
void cancellationStopsBeforeDataRead() {
    Device d; std::atomic<bool> cancelled{false}; d.fake->cancel=&cancelled;
    UmcSnapshot out;
    const auto error=d.service.transaction([&](HardwareSession &s) {
        out=readAmdUmc(s,d.target); return out.error;
    },10000,&cancelled);
    assert(error==-ECANCELED && d.fake->calls.size()==2 && out.decoded.values.empty());
}
struct TopologyFake : HardwareBackend {
    bool amd=true;
    unsigned calls=0,failAt=UINT32_MAX;
    std::uint32_t maximum=0x80000026;
    std::vector<std::array<std::uint32_t,4>> levels;
    TopologyFake() {
        // Sparse ID: socket 1, CCD 10, core 7, thread 1. Counts intentionally
        // exclude fused cores; shifts (not counts) determine identity.
        levels={{{1,2,0x100,0x1af}},{{4,12,0x201,0x1af}},
                {{4,12,0x302,0x1af}},{{8,144,0x403,0x1af}}};
    }
    Reply execute(const Request &) override { assert(false); return {}; }
    CpuIdReply cpuid(unsigned cpu,std::uint32_t leaf,std::uint32_t subleaf) override {
        assert(cpu==4095); CpuIdReply r;
        if(++calls==failAt) { r.error=-EACCES; return r; }
        if(!leaf) { r.words[0]=1; r.words[1]=amd?0x68747541:0x756e6547; r.words[3]=amd?0x69746e65:0x49656e69; r.words[2]=amd?0x444d4163:0x6c65746e; }
        else if(leaf==1) r.words[0]=0x00b00f20;
        else if(leaf==0x80000000) r.words[0]=maximum;
        else {
            assert(leaf==0x80000026);
            if(subleaf<levels.size()) for(unsigned i=0;i<4;++i) r.words[i]=levels[subleaf][i];
        }
        return r;
    }
    Backend backend(Space) const override { return Backend::Unavailable; }
};
AmdTopology topology(TopologyFake &fake) {
    // Borrowed test adapter; the owning service keeps its normal validation.
    struct Adapter : HardwareBackend {
        TopologyFake &f; explicit Adapter(TopologyFake &value):f(value) {}
        Reply execute(const Request &r) override { return f.execute(r); }
        CpuIdReply cpuid(unsigned c,std::uint32_t l,std::uint32_t sub) override { return f.cpuid(c,l,sub); }
        Backend backend(Space s) const override { return f.backend(s); }
    };
    HardwareService service{std::unique_ptr<HardwareBackend>(new Adapter(fake))};
    AmdTopology out; service.transaction([&](HardwareSession &s) { out=readAmdTopology(s,4095); return out.error; });
    return out;
}
void topologyUsesShiftsAndSparseIds() {
    TopologyFake fake; auto t=topology(fake);
    assert(!t.error && t.apicId==0x1af && t.socketId==1 && t.ccdInSocket==10 && t.coreInCcd==7 && t.threadInCore==1);
    assert(t.levels.size()==4 && t.levels[1].logicalCount==12 && t.levels[1].globalId==26);
    fake.levels={{{0,1,0x100,0}},{{3,8,0x201,0}},{{3,8,0x302,0}},{{3,8,0x403,0}}};
    t=topology(fake); assert(!t.error && !t.socketId && !t.ccdInSocket && !t.threadInCore);
}
void topologyRejectsMalformedAndUnavailableLeaves() {
    for(unsigned failure=1;failure<=8;++failure) {
        TopologyFake fake; fake.failAt=failure; const auto t=topology(fake);
        assert(t.error==-EACCES && fake.calls==failure);
    }
    for(unsigned variant=0;variant<9;++variant) {
        TopologyFake fake;
        switch(variant) {
        case 0: fake.amd=false; break;
        case 1: fake.maximum=0x80000025; break;
        case 2: fake.levels.clear(); break;
        case 3: fake.levels[1][0]=0; break;
        case 4: fake.levels[2][2]=0x202; break;
        case 5: fake.levels[3][2]=0x503; break;
        case 6: fake.levels[2][3]=0x1ae; break;
        case 7: fake.levels[1][1]=32; break;
        case 8: fake.levels[3][2]=0x402; break;
        }
        const auto t=topology(fake); assert(t.error && !t.socketId && !t.ccdInSocket && !t.coreInCcd);
    }
}
 AmdTopology l3Topology(amd_topology_fixture::Device &fake,bool include=true) {
    struct Adapter : HardwareBackend {
        amd_topology_fixture::Device &f;
        explicit Adapter(amd_topology_fixture::Device &value):f(value) {}
        Reply execute(const Request &r) override { return f.execute(r); }
        CpuIdReply cpuid(unsigned c,std::uint32_t l,std::uint32_t sub) override { return f.cpuid(c,l,sub); }
        Backend backend(Space s) const override { return f.backend(s); }
    };
    HardwareService service{std::unique_ptr<HardwareBackend>(new Adapter(fake))}; AmdTopology out;
    service.transaction([&](HardwareSession &s) { out=readAmdTopology(s,fake.expectedCpu,include); return out.error; });
    return out;
}
void topologyDistinguishesL3CapacityWithoutPmMapping() {
    for(unsigned ccd=0;ccd<2;++ccd) for(unsigned core=0;core<8;++core) for(unsigned thread=0;thread<2;++thread) {
        amd_topology_fixture::Device d; d.apic=0x80+(ccd<<4)+(core<<1)+thread;
        d.caches[2][1]=63u|((ccd?15u:47u)<<22);
        const auto t=l3Topology(d); assert(!t.error && t.l3Valid && !d.registerRequests);
        assert(t.socketId==4 && t.ccdInSocket==ccd && t.coreInCcd==core && t.threadInCore==thread);
        assert(t.ccdLevelType==2 && t.levels.size()==3);
        assert(t.l3.bytes==(ccd?32:96)*UINT64_C(1048576) && t.l3.cacheId==(d.apic>>4));
        assert(t.l3.sharingCapacity==16 && t.l3.sharingShift==4 && t.l3.subleaf==2);
        const auto json=amdTopologyJson(t);
        assert(json.find("\"pm_slot_mapping_verified\":false")!=std::string::npos);
        assert(json.find("\"ccd_level_type\":2")!=std::string::npos);
    }
    amd_topology_fixture::Device d; assert(!l3Topology(d,false).l3Valid && !d.l3Reads);
    // Missing-CCD fallback is model-specific, not a guess for every AMD CPU.
    for(unsigned mode=0;mode<4;++mode) {
        amd_topology_fixture::Device bad;
        if(mode==0) bad.signature=0x00b00f80; // Shimada still requires type 3
        if(mode==1) bad.signature=0x00a40f40; // family 19h, model 44h
        if(mode==2) bad.levels={{{1,2,0x100}},{{5,32,0x401}}};
        if(mode==3) bad.levels={{{1,2,0x100}},{{4,16,0x201}},{{4,16,0x302}},{{5,32,0x403}}};
        const auto t=l3Topology(bad); assert(t.error==-ENOTSUP && !t.l3Valid && !bad.registerRequests);
    }
    // Disabled SMT and sparse core IDs keep the APIC bit partition intact.
    amd_topology_fixture::Device single;
    single.apic=5; single.levels={{{0,1,0x100}},{{3,6,0x201}},{{3,6,0x402}}};
    single.caches[2][0]=3u|(3u<<5)|(7u<<14);
    const auto t=l3Topology(single);
    assert(!t.error && t.ccdLevelType==2 && t.ccdInSocket==0 && t.coreInCcd==5 && !t.threadInCore);
}
void topologyCacheFailuresDiscardGeometry() {
    amd_topology_fixture::Device good; assert(!l3Topology(good).error);
    for(unsigned fault=0;fault<good.calls;++fault) {
        amd_topology_fixture::Device d; d.failAt=fault; const auto t=l3Topology(d);
        assert(t.error==-EACCES && !t.l3Valid && !t.l3.bytes && d.calls==fault+1);
        assert(amdTopologyJson(t).find("\"l3\":null")!=std::string::npos);
    }
    for(unsigned mode=0;mode<8;++mode) {
        amd_topology_fixture::Device d;
        if(mode==0) d.extension=false;
        if(mode==1) d.caches.pop_back();
        if(mode==2) d.caches.push_back(d.caches.back());
        if(mode==3) d.caches[2][0]=3u|(3u<<5)|(31u<<14);
        if(mode==4) { d.caches[2][1]=UINT32_MAX; d.caches[2][2]=UINT32_MAX; }
        if(mode==5) d.caches[0][0]=4u|(1u<<5);
        if(mode==6) d.changeApic=true;
        if(mode==7) d.changeCache=true;
        const auto t=l3Topology(d); assert(t.error && !t.l3Valid && !t.l3.bytes);
    }
}
void topologyCacheEnumerationBoundedAndSparse() {
    amd_topology_fixture::Device d;
    // assign may release the vector's storage before copying the fill value.
    const auto l1=d.caches.front(); d.caches.assign(8,l1);
    const auto bounded=l3Topology(d);
    if(bounded.error!=-ENOTSUP || d.calls>=20)
        std::cerr << "Unterminated cache enumeration: error=" << bounded.error << " calls=" << d.calls << '\n';
    assert(bounded.error==-ENOTSUP && d.calls<20);
    amd_topology_fixture::Device sparse; sparse.caches[2][0]=3u|(3u<<5)|(11u<<14);
    const auto t=l3Topology(sparse); assert(!t.error && t.l3.sharingCapacity==12 && t.l3.sharingShift==4 && t.l3.cacheId==1);
}
}
AmdTopologyCatalog catalogTopology(amd_topology_fixture::CatalogDevice &fake,const std::vector<unsigned> &cpus,bool include=true) {
    struct Adapter : HardwareBackend {
        amd_topology_fixture::CatalogDevice &f;
        explicit Adapter(amd_topology_fixture::CatalogDevice &v):f(v) {}
        Reply execute(const Request &r) override { return f.execute(r); }
        CpuIdReply cpuid(unsigned c,std::uint32_t l,std::uint32_t sub) override { return f.cpuid(c,l,sub); }
        Backend backend(Space s) const override { return f.backend(s); }
    };
    HardwareService service{std::unique_ptr<HardwareBackend>(new Adapter(fake))}; AmdTopologyCatalog out;
    service.transaction([&](HardwareSession &s) { out=readAmdTopologyCatalog(s,cpus,include); return out.error; });
    return out;
}
void topologyCatalogPreservesOsIdsAndCacheGroups() {
    const std::vector<unsigned> cpus={4095,0,130,7};
    amd_topology_fixture::CatalogDevice d(cpus);
    const std::uint32_t apics[]={0x91,0x80,0x90,0x81};
    for(unsigned i=0;i<cpus.size();++i) {
        d.devices[cpus[i]].apic=apics[i];
        d.devices[cpus[i]].caches[2][1]=63u|(((apics[i]&16)?15u:47u)<<22);
    }
    const auto t=catalogTopology(d,cpus); assert(!t.error && t.rechecked && t.cpus.size()==4 && !d.registerRequests);
    assert(t.requestedCpus==cpus);
    for(unsigned i=0;i<4;++i) {
        const auto &c=t.cpus[i]; assert(c.cpu==cpus[i] && c.apicId==apics[i]);
        assert(c.socketId==4 && c.ccdInSocket==((apics[i]>>4)&1) && c.coreInCcd==0 && c.threadInCore==(apics[i]&1));
        assert(c.l3.bytes==((apics[i]&16)?33554432u:100663296u));
    }
    const auto json=amdTopologyCatalogJson(t);
    assert(json.find("\"requested_cpus\":[4095,0,130,7]")!=std::string::npos);
    assert(json.find("\"pm_slot_mapping_verified\":false")!=std::string::npos);
    assert(json.find("\"complete\":true")!=std::string::npos);
    amd_topology_fixture::CatalogDevice noCache(cpus); const auto minimal=catalogTopology(noCache,cpus,false);
    assert(!minimal.error && minimal.rechecked);
    for(const auto &entry:noCache.devices) assert(!entry.second.l3Reads);
}
void topologyCatalogRejectsIncompleteAndChangingSets() {
    const std::vector<unsigned> cpus={7,130};
    amd_topology_fixture::CatalogDevice good(cpus); assert(!catalogTopology(good,cpus).error);
    for(unsigned fault=0;fault<good.calls;++fault) {
        amd_topology_fixture::CatalogDevice d(cpus); d.failAt=fault;
        const auto t=catalogTopology(d,cpus); assert(t.error==-EACCES && t.cpus.empty() && !t.rechecked && t.hasFailedCpu);
        assert(amdTopologyCatalogJson(t).find("\"cpus\":[]")!=std::string::npos);
    }
    for(unsigned mode=0;mode<5;++mode) {
        amd_topology_fixture::CatalogDevice d(cpus);
        if(mode==0) d.devices[130].apic=d.devices[7].apic;
        if(mode==1) d.devices[130].caches[2][1]=63u|(15u<<22);
        if(mode==2) d.devices[130].signature^=1;
        if(mode==3) d.devices[130].levels[2][0]=6;
        if(mode==4) d.changeCpu=7;
        const auto t=catalogTopology(d,cpus); assert(t.error && t.cpus.empty() && !t.rechecked);
        if(mode==4) assert(t.error==-EAGAIN && d.changed);
    }
    for(const auto &invalid:std::vector<std::vector<unsigned>>{{},{7,7},{1048576},std::vector<unsigned>(4097,0)}) {
        amd_topology_fixture::CatalogDevice d(cpus); const auto t=catalogTopology(d,invalid);
        assert(t.error==-EINVAL && t.cpus.empty() && !d.calls);
    }
}
int main() {
    originalVectors(); offlineBoundaries(); refreshSelections(); identityGates(); explicitRead();
    failedReadsStop(); cancellationStopsBeforeDataRead();
    topologyUsesShiftsAndSparseIds(); topologyRejectsMalformedAndUnavailableLeaves();
    topologyDistinguishesL3CapacityWithoutPmMapping(); topologyCacheFailuresDiscardGeometry(); topologyCacheEnumerationBoundedAndSparse();
    topologyCatalogPreservesOsIdsAndCacheGroups(); topologyCatalogRejectsIncompleteAndChangingSets();
    std::cout << "14 memory and topology scenario groups passed\n";
}
