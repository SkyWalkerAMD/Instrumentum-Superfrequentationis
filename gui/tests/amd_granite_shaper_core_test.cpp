// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_rsmu.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <iostream>
using namespace octool::core;
namespace {
const std::array<std::uint32_t,5> packed{{0xffce8000,0x1e010001,0x7f321f02,0x1dfe8103,0xcd207e04}};
const std::array<std::array<int,3>,5> decoded{{
    {{-128,-50,-1}},{{0,1,30}},{{31,50,127}},{{-127,-2,29}},{{126,32,-51}}
}};
struct Test {
    AmdPboFixture *fake=new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() {
        target.cpu=130;target.profile=SmuProfile::GraniteRidge;
        fake->useGraniteShaper();fake->shaperWords=packed;
    }
    AmdCurveShaper read(const std::atomic<bool> *cancel=nullptr,int timeout=10000) {
        AmdCurveShaper out;
        const int e=service.transaction([&](HardwareSession &s) {
            out=readAmdCurveShaper(s,target);return out.error;
        },timeout,cancel);
        if(e) {out.error=e;out.valid=out.rawValid=false;}
        return out;
    }
};
void unpublished(const AmdCurveShaper &out) {
    assert(out.error && !out.valid && !out.rawValid);
    for(auto word:out.raw)assert(!word);
    for(const auto &row:out.counts)for(int count:row)assert(!count);
}
void fixedAndAllSignedBytes() {
    std::array<std::array<int,3>,5> counts{};
    assert(decodeAmdCurveShaper(packed,counts) && counts==decoded);
    // Each of the 15 positions independently receives every raw signed byte;
    // fixed, unrelated neighbouring counts expose byte/column transposition.
    for(unsigned tier=0;tier<5;++tier)for(unsigned point=0;point<3;++point)
        for(unsigned byte=0;byte<256;++byte) {
            auto raw=packed;auto expected=decoded;
            const unsigned shift=8+8*point;
            raw[tier]=(raw[tier]&~(0xffu<<shift))|(byte<<shift);
            expected[tier][point]=byte<128?int(byte):int(byte)-256;
            assert(decodeAmdCurveShaper(raw,counts) && counts==expected);
        }
}
void badTierNeverPublishes() {
    for(unsigned tier=0;tier<5;++tier)for(unsigned tag:{5u,0x80u,0xffu}) {
        auto raw=packed;raw[tier]=(raw[tier]&~0xffu)|tag;
        auto counts=decoded;assert(!decodeAmdCurveShaper(raw,counts));
        for(const auto &row:counts)for(int count:row)assert(!count);
    }
    for(unsigned mode=0;mode<3;++mode) {
        Test t;
        if(mode==0)t.fake->shaperWords[0]|=0x80;
        if(mode==1)t.fake->shaperWords[4]&=~0xffu;
        if(mode==2)std::swap(t.fake->shaperWords[1],t.fake->shaperWords[2]);
        const auto out=t.read();unpublished(out);
        assert(out.error==-EPROTO && out.wordsRead==5 && out.response==1);
        assert(out.capabilitiesAfter.valid && t.fake->commands==5 && !t.fake->settings);
    }
}
void identities() {
    for(unsigned mode=0;mode<13;++mode) {
        Test t;
        if(mode==0)t.target.profile=SmuProfile::None;
        if(mode==1)t.target.profile=SmuProfile::Shimada;
        if(mode==2)t.target.profile=SmuProfile::Phoenix;
        if(mode==3)t.target.profile=SmuProfile::Gpt;
        if(mode==4)t.target.bus=1;
        if(mode==5)t.target.device=1;
        if(mode==6)t.target.function=1;
        if(mode==7)t.fake->amd=false;
        if(mode==8)t.fake->family=0x19;
        if(mode==9)t.fake->model=8;
        if(mode==10)t.fake->pciId=0x153a1022;
        if(mode==11)t.fake->hypervisor=true;
        if(mode==12)t.fake->pciId=UINT32_MAX;
        const auto out=t.read();unpublished(out);
        assert(!out.messageAttempted && !out.wordsRead && !t.fake->commands && !t.fake->settings);
        if(mode<=6)assert(!t.fake->calls);
    }
}
void exactFirmware() {
    for(unsigned version:{0u,0x00625300u,0x00625401u,0x00732e00u,UINT32_MAX}) {
        Test t;t.fake->firmwareVersion=version;const auto out=t.read();unpublished(out);
        assert(out.error==-ENOTSUP && out.firmwareValid && out.firmwareVersion==version);
        assert(out.firmwareResponse==1 && !out.messageAttempted && !out.wordsRead);
        assert(t.fake->messages==std::vector<std::uint32_t>{2} && !t.fake->settings);
    }
}
void savedCountsWithoutPboGate() {
    for(unsigned caps:{0u,4u,0x10000u,0x80000005u,UINT32_MAX}) {
        Test t;t.fake->capsRaw=caps;const auto out=t.read();
        assert(!out.error && out.valid && out.rawValid && out.raw==packed && out.counts==decoded);
        assert(out.firmwareValid && out.firmwareVersion==0x00625400 && out.firmwareResponse==1);
        assert(out.capabilities.valid && out.capabilities.raw==caps);
        assert(out.capabilitiesAfter.valid && out.capabilitiesAfter.raw==caps);
        assert(out.wordsRead==5 && out.response==1 && out.messageAttempted);
        assert(t.fake->shaperReads==5 && t.fake->argumentReads==9 && !t.fake->wrongCpu && !t.fake->settings);
        assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x84,0x6f,2}));
        assert((t.fake->shaperReadIndices==std::vector<unsigned>{0,1,2,3,4}));
        for(const auto &args:t.fake->commandArgs)for(auto argument:args)assert(!argument);
        assert(t.fake->scalarArgs[5]==0); // Initialized, never returned as a sixth row.
        for(const auto &request:t.fake->requests)
            assert(request.space==Space::Pci && request.width==4 && !request.bus && !request.device && !request.function);
    }
}
void stateChanges() {
    for(unsigned mode=0;mode<7;++mode) {
        Test t;
        t.fake->afterRequest=[&] {
            if(mode==0 && t.fake->argumentReads==2)t.fake->model=8;
            if(mode==1 && t.fake->shaperReads==1)t.fake->capsRaw=0x80000004u;
            if(mode==2 && t.fake->shaperReads==5)t.fake->capsRaw=0x10004;
            if(mode==3 && t.fake->shaperReads==1)t.fake->firmwareVersion=0x00625401;
            if(mode==4 && t.fake->argumentReads==8)t.fake->hypervisor=true;
            if(mode==5 && t.fake->argumentReads==9)t.fake->pciId=0x153a1022;
            if(mode==6 && t.fake->shaperReads==5)t.fake->amd=false;
        };
        const auto out=t.read();unpublished(out);assert(!t.fake->settings);
        if(mode==0)assert(!out.messageAttempted && !out.wordsRead && t.fake->commands==2);
        else assert(out.messageAttempted && out.wordsRead==5);
        if(mode==1 || mode==2 || mode==3)assert(out.error==-EAGAIN);
    }
}
void rejectionAndTimeout() {
    for(unsigned status:{0u,2u,0xfcu,0xffu,UINT32_MAX}) {
        Test t;t.fake->afterRequest=[&] {if(t.fake->argumentReads==2)t.fake->completion=status;};
        const auto out=t.read();unpublished(out);
        assert(out.error==(status?-EIO:-ETIMEDOUT) && out.response==status);
        assert(out.messageAttempted && !out.wordsRead && !t.fake->shaperReads);
        assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x84}) && !t.fake->settings);
    }
    Test busy;busy.fake->busyBefore=true;const auto out=busy.read(nullptr,1);unpublished(out);
    assert(out.error==-ETIMEDOUT && !out.messageAttempted && !busy.fake->commands);
}
void partialWordFailures() {
    Test baseline;std::array<unsigned,5> afterWord{};unsigned beforeCommand=0;std::size_t seenRequests=0;
    baseline.fake->afterRequest=[&] {
        if(baseline.fake->requests.size()==seenRequests)return;
        seenRequests=baseline.fake->requests.size();
        const auto &request=baseline.fake->requests.back();
        if(baseline.fake->commands==2 && request.write && request.address==0xb8 && request.value==0x3b10524)
            beforeCommand=baseline.fake->calls;
        if(baseline.fake->commands==3 && !request.write && request.address==0xbc &&
           baseline.fake->index>=0x3b10a40 && baseline.fake->index<=0x3b10a50)
            afterWord[(baseline.fake->index-0x3b10a40)/4]=baseline.fake->calls;
    };
    assert(baseline.read().valid && beforeCommand);
    for(unsigned word=0;word<5;++word) {
        assert(afterWord[word]);Test t;t.fake->failAt=int(afterWord[word]-1);
        const auto out=t.read();unpublished(out);
        assert(out.error==-EACCES && out.response==1 && out.messageAttempted);
        assert(out.wordsRead==word && t.fake->shaperReads==word && t.fake->calls==afterWord[word]);
        assert(t.fake->commands==3 && !t.fake->settings);
    }
    for(unsigned at:{0u,beforeCommand,baseline.fake->calls-1}) {
        Test t;t.fake->failAt=int(at);const auto out=t.read();unpublished(out);
        assert(out.error==-EACCES && t.fake->calls==at+1 && !t.fake->settings);
        assert(out.wordsRead==(at==baseline.fake->calls-1?5u:0u));
    }
}
void cancellationAndStaleResult() {
    for(unsigned when:{0u,1u,3u,5u,8u,9u}) {
        Test t;std::atomic<bool> cancelled{when==0};
        t.fake->afterRequest=[&] {
            if((when<=5 && t.fake->shaperReads==when) ||
               (when>5 && t.fake->argumentReads==when))cancelled.store(true);
        };
        const auto out=t.read(&cancelled);unpublished(out);
        assert(out.error==-ECANCELED && !t.fake->settings);
        if(!when)assert(!t.fake->calls && !out.messageAttempted);
        else assert(out.messageAttempted && out.wordsRead==(when<=5?when:5u));
    }
    Test stale;auto out=stale.read();assert(out.valid && out.counts==decoded);
    stale.fake->failAt=int(stale.fake->calls);out=stale.read();unpublished(out);
    assert(out.error==-EACCES && !out.wordsRead && !out.messageAttempted && !stale.fake->settings);
}
void closedTransport() {
    Test t;
    assert(t.service.transaction([&](HardwareSession &s) {
        for(unsigned command=0;command<256;++command) {
            assert(!allowedSmuMessage(SmuProfile::GraniteRidge,command));
            if(command!=0x84)assert(detail::exchangeGraniteShaper(s,command).error==-EINVAL);
        }
        for(unsigned argument:{1u,0x80000000u,UINT32_MAX})
            assert(detail::exchangeGraniteShaper(s,0x84,argument).error==-EINVAL);
        for(unsigned command:{0x84u,0xa6u}) {
            assert(detail::exchangeGranitePbo(s,command).error==-EINVAL);
            assert(detail::exchangeGraniteFmax(s,command).error==-EINVAL);
            assert(detail::exchangeGraniteScalar(s,command).error==-EINVAL);
            assert(detail::exchangeGraniteLimits(s,command).error==-EINVAL);
            assert(detail::exchangeGraniteMode(s,command).error==-EINVAL);
            assert(detail::exchangeGraniteCurve(s,command).error==-EINVAL);
        }
        return 0;
    })==0 && !t.fake->calls && !t.fake->settings);
}
}
int main() {
    fixedAndAllSignedBytes();badTierNeverPublishes();identities();exactFirmware();savedCountsWithoutPboGate();
    stateChanges();rejectionAndTimeout();partialWordFailures();cancellationAndStaleResult();closedTransport();
    std::cout << "10 Granite Curve Shaper scenario groups passed\n";
}
