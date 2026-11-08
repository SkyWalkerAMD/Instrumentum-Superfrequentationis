// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_rsmu.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <set>
using namespace octool::core;
namespace {
const std::array<std::uint32_t,5> packed{{0xffce8000,0x1e010001,0x7f321f02,0x1dfe8103,0xcd207e04}};
const std::array<std::array<int,3>,5> decoded{{
    {{-128,-50,-1}},{{0,1,30}},{{31,50,127}},{{-127,-2,29}},{{126,32,-51}}
}};
const std::array<std::uint32_t,5> writable{{0x1effce00,0x02010001,0x1e0a0002,0x00fece03,0xff05fb04}};
const std::array<int,3> requested{{-50,0,30}};
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
struct WriteTest : Test {
    WriteTest() { fake->useGraniteShaperWrite();fake->shaperWords=writable; }
    AmdCurveShaperUpdate set(unsigned tier=2,const std::array<int,3> &counts=requested,
        const std::array<std::uint32_t,5> &expected=writable,unsigned caps=4,
        const std::atomic<bool> *cancel=nullptr,int timeout=10000) {
        AmdCurveShaperUpdate out;
        const int e=service.transaction([&](HardwareSession &s) {
            out=setAmdCurveShaper(s,target,caps,expected,tier,counts);return out.error;
        },timeout,cancel);
        if(e) {out.error=e;out.verified=false;}
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
void writeEncodingAndInputGuards() {
    std::uint32_t argument=UINT32_MAX;
    assert(!encodeAmdCurveShaper(2,requested,argument) && argument==0x1e00ce02u);
    // Pure coverage of all supported integers in every tier/column; the fixed
    // literal above independently anchors order, signed encoding and bit 7.
    for(unsigned tier=0;tier<5;++tier)for(unsigned point=0;point<3;++point)
        for(int count=-50;count<=30;++count) {
            auto values=requested;values[point]=count;
            assert(!encodeAmdCurveShaper(tier,values,argument));
            auto raw=writable;raw[tier]=argument;
            std::array<std::array<int,3>,5> counts{};
            assert(decodeAmdCurveShaper(raw,counts) && counts[tier]==values);
            assert((argument&255u)==tier);
        }
    for(unsigned tier:{5u,0x80u,UINT32_MAX}) {
        WriteTest t;assert(t.set(tier).error==-ERANGE && !t.fake->calls);
        argument=UINT32_MAX;assert(encodeAmdCurveShaper(tier,requested,argument)==-ERANGE && !argument);
    }
    for(unsigned point=0;point<3;++point)
        for(int count:{std::numeric_limits<int>::min(),-51,31,std::numeric_limits<int>::max()}) {
            WriteTest t;auto values=requested;values[point]=count;
            assert(t.set(2,values).error==-ERANGE && !t.fake->calls);
        }
    // Invalid existing values anywhere, even unselected tiers, must be rejected
    // before the firmware can normalize them as a side effect of one A6.
    for(unsigned tier=0;tier<5;++tier)for(unsigned point=0;point<3;++point)
        for(unsigned byte:{0x80u,0xcdu,0x1fu,0x7fu}) {
            auto raw=writable;const unsigned shift=8+8*point;
            raw[tier]=(raw[tier]&~(255u<<shift))|(byte<<shift);
            WriteTest t;assert(t.set(2,requested,raw).error==-ERANGE && !t.fake->calls);
        }
    for(unsigned tier=0;tier<5;++tier) {
        auto raw=writable;raw[tier]|=0x80;
        WriteTest t;assert(t.set(2,requested,raw).error==-EINVAL && !t.fake->calls);
    }
    for(unsigned caps:{0u,1u,2u,0x10004u,UINT32_MAX}) {
        WriteTest t;assert(t.set(2,requested,writable,caps).error==-ENOTSUP && !t.fake->calls);
    }
}
void writeIdentityAndVersionGuards() {
    for(unsigned mode=0;mode<13;++mode) {
        WriteTest t;
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
        const auto out=t.set();assert(out.error && !out.writeAttempted && !out.verified);
        assert(!t.fake->commands && !t.fake->settings);
        if(mode<=6)assert(!t.fake->calls);
    }
    for(unsigned version:{0u,0x00625300u,0x00625401u,0x00732e00u}) {
        WriteTest t;t.fake->firmwareVersion=version;const auto out=t.set();
        assert(out.error==-ENOTSUP && out.before.firmwareVersion==version && !out.writeAttempted);
        assert(t.fake->messages==std::vector<std::uint32_t>{2} && !t.fake->settings);
    }
}
void writeProtocolAndNoop() {
    const std::array<int,3> values{{-50,30,0}};
    for(unsigned tier=0;tier<5;++tier) {
        WriteTest t;const auto out=t.set(tier,values);
        auto expected=writable;expected[tier]=0x001ece00u|tier;
        assert(!out.error && out.phase==AmdPboPhase::Complete && out.verified && !out.unchanged);
        assert(out.accepted && out.writeAttempted && out.response==1);
        assert(out.tier==tier && out.requestedCounts==values && out.argument==expected[tier]);
        assert(out.before.valid && out.before.raw==writable && out.after.valid && out.after.raw==expected);
        assert(out.after.counts[tier]==values && out.requestedRaw==expected && t.fake->shaperWords==expected);
        assert(t.fake->shaperWrites==1 && t.fake->settings==1 && t.fake->shaperArgument==expected[tier]);
        assert(t.fake->shaperReads==15 && t.fake->argumentReads==27 && !t.fake->wrongCpu);
        assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x84,0x6f,2,2,0x6f,0x84,0x6f,2,0xa6,2,0x6f,0x84,0x6f,2}));
        for(unsigned message=0;message<t.fake->commandArgs.size();++message)
            for(unsigned arg=0;arg<6;++arg)
                assert(t.fake->commandArgs[message][arg]==(message==10 && !arg?expected[tier]:0u));
    }
    WriteTest unknown;unknown.fake->capsRaw=0x80000004u;
    assert(unknown.set(2,requested,writable,0x80000004u).verified);
    WriteTest noop;const auto out=noop.set(2,{{0,10,30}});
    assert(!out.error && out.verified && out.unchanged && !out.writeAttempted && !out.accepted);
    assert(out.after.raw==writable && out.before.raw==writable && out.requestedRaw==writable);
    assert((noop.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x84,0x6f,2}));
    assert(!noop.fake->settings && !noop.fake->shaperWrites && noop.fake->shaperReads==5);
}
void writeStaleTablesAndCaps() {
    for(unsigned tier=0;tier<5;++tier) {
        WriteTest t;t.fake->shaperWords[tier]^=0x100;
        const auto out=t.set();assert(out.error==-EAGAIN && out.before.valid);
        assert(!out.writeAttempted && !out.verified && !t.fake->settings && t.fake->commands==5);
    }
    WriteTest caps;caps.fake->capsRaw=0x80000004u;
    assert(caps.set().error==-EAGAIN && !caps.fake->settings && caps.fake->commands==5);
    // Change an unselected word after the first complete snapshot. The second
    // snapshot must compare all five words, not only the chosen tier.
    for(bool changeCaps:{false,true}) {
        WriteTest t;bool changed=false;
        t.fake->afterRequest=[&] {
            if(!changed && t.fake->argumentReads==9) {
                changed=true;
                if(changeCaps)t.fake->capsRaw=0x80000004u;
                else t.fake->shaperWords[4]^=0x100;
            }
        };
        const auto out=t.set();assert(out.error==-EAGAIN && out.before.valid && changed);
        assert(t.fake->commands==10 && !t.fake->settings && !out.writeAttempted && !out.verified);
    }
}
void writeStateDrift() {
    for(bool afterWrite:{false,true})for(unsigned mode=0;mode<7;++mode) {
        WriteTest t;bool changed=false;
        t.fake->afterRequest=[&] {
            if(changed || !(afterWrite?t.fake->shaperWrites==1:t.fake->shaperReads==6))return;
            changed=true;
            if(mode==0)t.fake->capsRaw=0x80000004u;
            if(mode==1)t.fake->capsRaw=0x10004;
            if(mode==2)t.fake->capsRaw=0;
            if(mode==3)t.fake->firmwareVersion=0x00625401;
            if(mode==4)t.fake->model=8;
            if(mode==5)t.fake->hypervisor=true;
            if(mode==6)t.fake->pciId=0x153a1022;
        };
        const auto out=t.set();assert(out.error && !out.verified && changed);
        assert(out.writeAttempted==afterWrite && out.accepted==afterWrite);
        assert(t.fake->settings==(afterWrite?1u:0u) && t.fake->shaperWrites==(afterWrite?1u:0u));
        if(mode<3) {
            assert(out.error==-EAGAIN);
            if(afterWrite)assert(out.after.valid); // Saved table readable, permission/state drift still fails apply.
        }
    }
}
void writeReadbackDifferences() {
    for(unsigned tier=0;tier<5;++tier) {
        WriteTest t;bool changed=false;
        t.fake->afterRequest=[&] {
            if(!changed && t.fake->shaperWrites) {changed=true;t.fake->shaperWords[tier]^=0x100;}
        };
        const auto out=t.set();assert(out.error==-EIO && out.after.valid && out.accepted && !out.verified);
        assert(out.after.raw[tier]!=(tier==2?0x1e00ce02u:writable[tier]));
        assert(t.fake->settings==1 && t.fake->shaperWrites==1);
    }
    WriteTest discard;discard.fake->discardChange=true;const auto unchanged=discard.set();
    assert(unchanged.error==-EIO && unchanged.accepted && !unchanged.verified && unchanged.after.raw==writable);
    for(bool badTag:{false,true}) {
        WriteTest t;bool changed=false;
        t.fake->afterRequest=[&] {
            if(!changed && t.fake->shaperWrites) {
                changed=true;t.fake->shaperWords[4]=badTag?0x80u:0x00001f04u;
            }
        };
        const auto out=t.set();assert(out.accepted && !out.verified && t.fake->shaperWrites==1);
        if(badTag) {assert(out.error==-EPROTO);unpublished(out.after);}
        else assert(out.error==-EIO && out.after.valid && out.after.counts[4][0]==31);
    }
}
void writeRejectionAndLostAck() {
    for(unsigned status:{0u,2u,0xffu}) {
        WriteTest t;t.fake->writeCompletion=status;const auto out=t.set();
        assert(out.error==(status?-EIO:-ETIMEDOUT) && out.writeAttempted && !out.accepted && !out.verified);
        assert(out.phase==AmdPboPhase::Write && out.response==status && !out.after.valid);
        assert(t.fake->settings==1 && t.fake->shaperWrites==1 && t.fake->commands==11);
        assert(t.fake->shaperWords==writable);
    }
    // The saved table changes, then the reply is lost. There is no retry,
    // automatic rollback, or false inference that a failed ACK means no write.
    WriteTest lost;bool changed=false;
    lost.fake->afterRequest=[&] {
        if(!changed && lost.fake->shaperWrites) {changed=true;lost.fake->response=0;}
    };
    const auto out=lost.set();assert(out.error==-ETIMEDOUT && out.writeAttempted && !out.accepted && !out.verified);
    assert(out.phase==AmdPboPhase::Write && !out.after.valid && !out.response);
    auto expected=writable;expected[2]=0x1e00ce02;
    assert(lost.fake->shaperWords==expected && lost.fake->settings==1 && lost.fake->commands==11);
}
void writeHalFaultBoundaries() {
    WriteTest baseline;std::set<unsigned> stops{0,1,2,3};
    std::array<bool,2> snapshots{{false,false}};std::size_t seen=0;
    unsigned commandData=0,ackData=0;
    baseline.fake->afterRequest=[&] {
        for(unsigned i=0;i<2;++i)if(!snapshots[i] && baseline.fake->argumentReads==9*(i+1)) {
            snapshots[i]=true;stops.insert(baseline.fake->calls);
        }
        if(seen==baseline.fake->requests.size())return;
        seen=baseline.fake->requests.size();const auto &r=baseline.fake->requests.back();
        if(r.write && r.address==0xbc && baseline.fake->index==0x3b10524 && r.value==0xa6)
            commandData=baseline.fake->calls;
        if(!ackData && !r.write && r.address==0xbc && baseline.fake->index==0x3b10570 && baseline.fake->commands==11) {
            ackData=baseline.fake->calls;stops.insert(ackData);
        }
        if(!r.write && r.address==0xbc && baseline.fake->commands==14 &&
           baseline.fake->index>=0x3b10a40 && baseline.fake->index<=0x3b10a50)
            stops.insert(baseline.fake->calls-1); // Every post-write returned word can fail independently.
    };
    assert(baseline.set().verified && commandData && ackData);
    stops.insert(commandData-2);stops.insert(commandData-1);stops.insert(baseline.fake->calls-1);
    for(unsigned at:stops) {
        WriteTest t;t.fake->failAt=int(at);const auto out=t.set();
        assert(out.error==-EACCES && !out.verified && t.fake->calls==at+1);
        assert(out.writeAttempted==(at>=commandData-2) && out.accepted==(at>=ackData));
        assert(t.fake->settings==(at>=commandData?1u:0u));
    }
    // Exhaust every HAL position in the isolated A6 transport. Combined with
    // the reader's per-word faults above, this covers the write's full I/O
    // shape without repeatedly sleeping through unrelated query prefixes.
    WriteTest transport;detail::AmdRsmuReply successful;
    assert(transport.service.transaction([&](HardwareSession &s) {
        successful=detail::exchangeGraniteShaperWrite(s,0xa6,0x1e00ce02);return successful.error;
    })==0 && successful.valid && transport.fake->calls==20);
    for(unsigned at=0;at<transport.fake->calls;++at) {
        WriteTest t;t.fake->failAt=int(at);detail::AmdRsmuReply reply;
        assert(t.service.transaction([&](HardwareSession &s) {
            reply=detail::exchangeGraniteShaperWrite(s,0xa6,0x1e00ce02);return reply.error;
        })==-EACCES);
        assert(!reply.valid && !reply.response && reply.messageAttempted==(at>=16));
        assert(t.fake->calls==at+1 && t.fake->settings==(at>=18?1u:0u));
    }
}
void writeCancellation() {
    for(unsigned when=0;when<7;++when) {
        WriteTest t;std::atomic<bool> cancelled{when==0};
        t.fake->afterRequest=[&] {
            if((when==1 && t.fake->shaperReads==5) || (when==2 && t.fake->argumentReads==9) ||
               (when==3 && t.fake->argumentReads==18) || (when==4 && t.fake->shaperWrites==1) ||
               (when==5 && t.fake->shaperReads==11) || (when==6 && t.fake->argumentReads==27))
                cancelled.store(true);
        };
        const auto out=t.set(2,requested,writable,4,&cancelled);
        assert(out.error==-ECANCELED && !out.verified && !out.after.valid);
        assert(out.writeAttempted==(when>=4) && out.accepted==(when>=5));
        assert(t.fake->settings==(when>=4?1u:0u) && t.fake->shaperWrites==(when>=4?1u:0u));
        if(!when)assert(!t.fake->calls);
    }
    WriteTest timeout;timeout.fake->busyBefore=true;
    assert(timeout.set(2,requested,writable,4,nullptr,1).error==-ETIMEDOUT && !timeout.fake->settings);
}
void closedWriteTransport() {
    WriteTest t;
    assert(t.service.transaction([&](HardwareSession &s) {
        for(unsigned command=0;command<256;++command) {
            if(command!=0xa6)assert(detail::exchangeGraniteShaperWrite(s,command,0x1e00ce02).error==-EINVAL);
            assert(!allowedSmuMessage(SmuProfile::GraniteRidge,command));
        }
        for(unsigned tier:{5u,0x7fu,0x80u,0x84u,0xffu})
            assert(detail::exchangeGraniteShaperWrite(s,0xa6,0x1e00ce00u|tier).error==-EINVAL);
        for(unsigned point=0;point<3;++point)for(unsigned byte=31;byte<206;++byte) {
            const unsigned shift=8+8*point;
            const auto argument=(0x1e00ce02u&~(255u<<shift))|(byte<<shift);
            assert(detail::exchangeGraniteShaperWrite(s,0xa6,argument).error==-EINVAL);
        }
        assert(detail::exchangeGraniteShaper(s,0xa6).error==-EINVAL);
        assert(detail::exchangeGranitePbo(s,0xa6).error==-EINVAL);
        return 0;
    })==0 && !t.fake->calls && !t.fake->settings);
}
}
int main() {
    fixedAndAllSignedBytes();badTierNeverPublishes();identities();exactFirmware();savedCountsWithoutPboGate();
    stateChanges();rejectionAndTimeout();partialWordFailures();cancellationAndStaleResult();closedTransport();
    writeEncodingAndInputGuards();writeIdentityAndVersionGuards();writeProtocolAndNoop();writeStaleTablesAndCaps();
    writeStateDrift();writeReadbackDifferences();writeRejectionAndLostAck();writeHalFaultBoundaries();
    writeCancellation();closedWriteTransport();
    std::cout << "20 Granite Curve Shaper scenario groups passed\n";
}
