// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_rsmu.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <climits>
#include <iostream>
using namespace octool::core;
namespace {
struct Test {
    AmdPboFixture *fake = new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    explicit Test(bool granite=true) {
        target.cpu=130;target.profile=granite?SmuProfile::GraniteRidge:SmuProfile::Shimada;
        fake->capsRaw=4;
        if(granite)fake->useGraniteCurve();else fake->allowAllCoreCurve=true;
    }
    AmdCurveAllUpdate set(int margin=-25, unsigned caps=4,
        const std::atomic<bool> *cancel=nullptr,int timeout=10000) {
        AmdCurveAllUpdate out;
        const int error=service.transaction([&](HardwareSession &s) {
            out=setAmdAllCoreCurve(s,target,caps,margin);return out.error;
        },timeout,cancel);
        if(error) {out.error=error;out.verified=false;} return out;
    }
};
void encodingAndGuards() {
    for(int counts=-50;counts<=30;++counts) {
        std::uint32_t argument=UINT32_MAX;
        assert(!encodeGraniteAllCoreCurve(counts,argument));
        assert(argument==std::uint32_t(counts<0?65536+counts:counts) && !(argument>>16));
    }
    for(int counts:{INT_MIN,-65536,-51,31,50,51,65536,INT_MAX}) {
        Test t;std::uint32_t argument=UINT32_MAX;
        assert(encodeGraniteAllCoreCurve(counts,argument)==-ERANGE && !argument);
        const auto out=t.set(counts);
        assert(out.error==-ERANGE && out.phase==AmdPboPhase::Validate && !out.argument && !t.fake->calls);
    }
    for(unsigned caps:{0u,1u,2u,3u,0x10004u,0x10007u,UINT32_MAX}) {
        Test t;const auto out=t.set(-25,caps);
        assert(out.error==-ENOTSUP && !out.writeAttempted && !t.fake->calls);
    }
}
void identitiesAndVersions() {
    for(unsigned scenario=0;scenario<9;++scenario) {
        Test t;
        if(scenario==0)t.target.profile=SmuProfile::Phoenix;
        if(scenario==1)t.target.bus=1;
        if(scenario==2)t.target.device=1;
        if(scenario==3)t.target.function=1;
        if(scenario==4)t.fake->amd=false;
        if(scenario==5)t.fake->family=0x19;
        if(scenario==6)t.fake->model=8;
        if(scenario==7)t.fake->pciId=0x153a1022;
        if(scenario==8)t.fake->hypervisor=true;
        const auto out=t.set();
        assert(out.error && !out.writeAttempted && !t.fake->settings && !t.fake->commands);
    }
    for(unsigned version:{0u,0x00625300u,0x00625401u,0x00732e00u}) {
        Test t;t.fake->firmwareVersion=version;const auto out=t.set();
        assert(out.error==-ENOTSUP && out.before.firmwareValid && out.before.firmwareVersion==version);
        assert(!out.writeAttempted && !t.fake->settings);
        assert(t.fake->messages==std::vector<std::uint32_t>{2});
    }
}
void acknowledgedCounts() {
    for(int counts:{-50,-1,0,1,30}) for(bool discard:{false,true}) {
        Test t;t.fake->discardChange=discard;const auto out=t.set(counts);
        assert(!out.error && out.accepted && out.writeAttempted && !out.verified);
        assert(out.phase==AmdPboPhase::Complete && out.requestedMargin==counts);
        assert(out.argument==std::uint32_t(counts<0?65536+counts:counts) && out.response==1);
        assert(out.before.valid && out.before.raw==4 && out.before.firmwareValid && out.before.firmwareVersion==0x00625400);
        assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,2,2,0x6f,2,7}));
        assert(t.fake->commands==7 && t.fake->settings==1 && t.fake->argumentReads==6 && !t.fake->wrongCpu);
        assert(t.fake->allCurveArgument==(discard?UINT32_MAX:out.argument));
        for(unsigned i=0;i<7;++i) {
            assert(t.fake->commandArgs[i][0]==(i==6?out.argument:0));
            for(unsigned n=1;n<6;++n)assert(!t.fake->commandArgs[i][n]);
        }
        assert(t.fake->capsRaw==4 && t.fake->scalarRaw==0x3f800000 && !t.fake->fmaxMHz);
        assert(!t.fake->ocAllMHz && !t.fake->ocCoreArgument);
        for(auto limit:t.fake->limitArguments)assert(!limit);
    }
    Test unknown;unknown.fake->capsRaw=0x80000004;
    const auto out=unknown.set(-1,0x80000004);
    assert(out.accepted && !out.verified && out.before.raw==0x80000004 && unknown.fake->settings==1);
}
void completeCapabilityAndIdentityComparison() {
    Test stale;stale.fake->capsRaw=0x80000004;const auto old=stale.set();
    assert(old.error==-EAGAIN && old.before.raw==0x80000004 && !old.writeAttempted && !stale.fake->settings);
    assert((stale.fake->messages==std::vector<std::uint32_t>{2,0x6f,2}));
    for(unsigned stage=0;stage<6;++stage) {
        Test t;bool changed=false;
        const unsigned boundary=stage==0?2:stage==2?5:stage==3?6:3;
        t.fake->afterRequest=[&] {
            if(changed || t.fake->argumentReads!=boundary)return;
            changed=true;
            if(stage==0 || stage==4)t.fake->firmwareVersion=0x00625401;
            if(stage==1)t.fake->capsRaw=0x80000004;
            if(stage==2)t.fake->model=8;
            if(stage==3)t.fake->pciId=0x153a1022;
            if(stage==5)t.fake->capsRaw=0x10004;
        };
        const auto out=t.set();
        const int expected=stage==3?-ENODEV:(stage==2 || stage==4)?-ENOTSUP:-EAGAIN;
        assert(changed && out.error==expected && out.phase==AmdPboPhase::Compare);
        assert(!out.writeAttempted && !out.accepted && !out.verified && !t.fake->settings && t.fake->commands<7);
    }
}
void rejectionAndTimeout() {
    for(unsigned response:{0u,2u,0xfeu}) {
        Test t;t.fake->writeCompletion=response;const auto out=t.set();
        assert(out.error==(response?-EIO:-ETIMEDOUT));
        assert(out.writeAttempted && !out.accepted && !out.verified && out.phase==AmdPboPhase::Write);
        assert(t.fake->settings==1 && t.fake->commands==7 && t.fake->allCurveArgument==UINT32_MAX);
    }
    Test query;query.fake->completion=0xfe;const auto out=query.set();
    assert(out.error==-EIO && !out.writeAttempted && !query.fake->settings && query.fake->commands==1);
    Test busy;busy.fake->busyBefore=true;
    assert(busy.set(-25,4,nullptr,1).error==-ETIMEDOUT && !busy.fake->commands && !busy.fake->settings);
}
void partialTransportAndCancellation() {
    Test baseline;unsigned beforeData=0,afterData=0;
    baseline.fake->afterRequest=[&] {
        if(!beforeData && baseline.fake->argumentReads==6 && !baseline.fake->settings && baseline.fake->index==0x3b10524)
            beforeData=baseline.fake->calls;
        if(!afterData && baseline.fake->settings)afterData=baseline.fake->calls;
    };
    assert(baseline.set().accepted && beforeData && afterData==beforeData+1);
    // Distinguish failed command delivery from loss of the acknowledgement.
    // None of these paths retries, rolls back or reports a verified value.
    for(unsigned at:{0u,beforeData,afterData}) {
        Test t;t.fake->failAt=int(at);const auto out=t.set();
        assert(out.error==-EACCES && !out.accepted && !out.verified && t.fake->calls==at+1);
        assert(out.writeAttempted==(at!=0) && t.fake->settings==(at==afterData?1u:0u));
        assert(t.fake->commands==(at==0?0u:at==beforeData?6u:7u));
    }
    for(unsigned at:{0u,beforeData,afterData}) {
        Test t;std::atomic<bool> cancelled{at==0};
        t.fake->afterRequest=[&] {if(t.fake->calls==at)cancelled.store(true);};
        const auto out=t.set(-25,4,&cancelled);
        assert(out.error==-ECANCELED && !out.accepted && !out.verified && t.fake->calls==at);
        assert(out.writeAttempted==(at!=0) && t.fake->settings==(at==afterData?1u:0u));
    }
    Test acknowledged;std::atomic<bool> cancelled{false};
    acknowledged.fake->afterRequest=[&] {
        if(!acknowledged.fake->settings || acknowledged.fake->index!=0x3b10570 || acknowledged.fake->requests.empty())return;
        const auto &request=acknowledged.fake->requests.back();
        if(!request.write && request.address==0xbc)cancelled.store(true);
    };
    const auto out=acknowledged.set(-25,4,&cancelled);
    assert(out.error==-ECANCELED && out.accepted && out.writeAttempted && !out.verified);
    assert(out.response==1 && acknowledged.fake->settings==1 && acknowledged.fake->commands==7);
}
void closedTransport() {
    for(unsigned command=0;command<256;++command) {
        assert(!allowedSmuMessage(SmuProfile::GraniteRidge,command));
        if(command==2 || command==0x6f || command==7)continue;
        Test t;assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeGraniteCurve(s,command).error;
        })==-EINVAL && !t.fake->calls);
    }
    for(unsigned argument:{31u,50u,0xffcdu,0x10000u,0x1ffceu,0xa5a5ffceu,0xffffffceu,UINT32_MAX}) {
        Test t;assert(t.service.transaction([&](HardwareSession &s) {
            assert(detail::exchangeGranitePbo(s,7,argument).error==-EINVAL);
            return detail::exchangeGraniteCurve(s,7,argument).error;
        })==-EINVAL && !t.fake->calls);
    }
    for(unsigned command:{2u,0x6fu}) {
        Test t;assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeGraniteCurve(s,command,1).error;
        })==-EINVAL && !t.fake->calls);
    }
    Test separate;assert(separate.service.transaction([&](HardwareSession &s) {
        assert(detail::exchangeGraniteFmax(s,7).error==-EINVAL);
        assert(detail::exchangeGraniteScalar(s,7).error==-EINVAL);
        assert(detail::exchangeGraniteLimits(s,7).error==-EINVAL);
        assert(detail::exchangeGraniteMode(s,7).error==-EINVAL);
        return 0;
    })==0 && !separate.fake->calls);
}
void shimadaCompatibility() {
    Test old(false);const auto out=old.set(50);
    assert(!out.error && out.accepted && !out.verified && out.argument==50 && old.fake->settings==1);
    assert((old.fake->messages==std::vector<std::uint32_t>{0x6f,7}));
    Test excessive(false);assert(excessive.set(51).error==-ERANGE && !excessive.fake->calls);
    Test granite;assert(granite.service.transaction([&](HardwareSession &s) {
        return setShimadaAllCoreCurve(s,granite.target,4,-25).error;
    })==-ENOTSUP && !granite.fake->calls);
    std::uint32_t argument=0;
    assert(!encodeShimadaAllCoreCurve(50,argument) && argument==50);
    assert(encodeGraniteAllCoreCurve(31,argument)==-ERANGE && !argument);
}
}
int main() {
    encodingAndGuards();identitiesAndVersions();acknowledgedCounts();completeCapabilityAndIdentityComparison();
    rejectionAndTimeout();partialTransportAndCancellation();closedTransport();shimadaCompatibility();
    std::cout << "8 Granite all-core curve scenario groups passed\n";
}
