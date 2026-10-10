// SPDX-License-Identifier: GPL-2.0-only
#include "amd_umc.h"
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
}
int main() {
    originalVectors(); offlineBoundaries(); refreshSelections(); identityGates(); explicitRead();
    failedReadsStop(); cancellationStopsBeforeDataRead();
    std::cout << "7 memory scenario groups passed\n";
}
