// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_rsmu.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <iostream>
using namespace octool::core;
namespace {
const AmdPboLimit kinds[] = {
    AmdPboLimit::Ppt, AmdPboLimit::Tdc, AmdPboLimit::Edc, AmdPboLimit::Temperature
};
struct Test {
    AmdPboFixture *fake = new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() {
        fake->useGraniteLimits(); fake->useGraniteMode(); fake->capsRaw=3;
        target.cpu=130; target.profile=SmuProfile::GraniteRidge;
    }
    AmdPboCapabilities read(const std::atomic<bool> *cancel=nullptr, int timeout=10000) {
        AmdPboCapabilities out;
        const int e=service.transaction([&](HardwareSession &s) {
            out=readAmdPboCapabilities(s,target); return out.error;
        },timeout,cancel);
        if(e) {out.error=e;out.valid=false;} return out;
    }
    AmdPboLimitUpdate limit(AmdPboLimit kind=AmdPboLimit::Ppt, unsigned value=100,
        unsigned expected=3, const std::atomic<bool> *cancel=nullptr, int timeout=10000) {
        AmdPboLimitUpdate out;
        const int e=service.transaction([&](HardwareSession &s) {
            out=setAmdPboLimit(s,target,expected,kind,value); return out.error;
        },timeout,cancel);
        if(e) {out.error=e;out.verified=false;} return out;
    }
    AmdOcModeUpdate mode(bool enabled=true, unsigned expected=3,
        const std::atomic<bool> *cancel=nullptr, int timeout=10000) {
        AmdOcModeUpdate out;
        const int e=service.transaction([&](HardwareSession &s) {
            out=setAmdOcMode(s,target,expected,enabled); return out.error;
        },timeout,cancel);
        if(e) {out.error=e;out.verified=false;} return out;
    }
};
void messages(const Test &t, std::initializer_list<std::uint32_t> expected) {
    assert(t.fake->messages==std::vector<std::uint32_t>(expected));
    assert(t.fake->commands==expected.size() && !t.fake->wrongCpu);
}
void arguments(const Test &t, unsigned setting=0, unsigned argument=0) {
    assert(t.fake->commandArgs.size()==t.fake->messages.size());
    for(std::size_t i=0;i<t.fake->messages.size();++i) {
        const auto &args=t.fake->commandArgs[i];
        assert(args[0]==(t.fake->messages[i]==setting?argument:0));
        for(unsigned n=1;n<6;++n) assert(!args[n]);
    }
}
void identitiesAndVersions() {
    // Exercise the common identity gate through all three public operations.
    for(unsigned scenario=0;scenario<10;++scenario) {
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
        if(scenario==9)t.target.profile=SmuProfile::Shimada;
        const int error=scenario%3==0?t.read().error:scenario%3==1?t.limit().error:t.mode().error;
        assert(error && !t.fake->commands && !t.fake->settings);
    }
    const unsigned versions[]={0,0x00625401,0x00732e00};
    for(unsigned i=0;i<3;++i) {
        Test t;t.fake->firmwareVersion=versions[i];
        const int error=i==0?t.read().error:i==1?t.limit().error:t.mode().error;
        assert(error==-ENOTSUP && !t.fake->settings); messages(t,{2}); arguments(t);
    }
}
void capabilityReads() {
    for(unsigned raw:{0u,3u,0x10001u,0x80000003u,UINT32_MAX}) {
        Test t;t.fake->capsRaw=raw;const auto out=t.read();
        assert(!out.error && out.valid && out.raw==raw && out.response==1 && out.messageAttempted);
        assert(out.firmwareValid && out.firmwareVersion==0x00625400 && !t.fake->settings);
        messages(t,{2,0x6f,2});arguments(t);
    }
}
void limitInputGuards() {
    for(unsigned i=0;i<4;++i) {
        const unsigned maximum=i==3?100:2000;
        assert(amdPboLimitMaximum(kinds[i])==maximum);
        for(unsigned value:{1u,maximum}) {
            unsigned command=0,argument=0;
            assert(!encodeAmdPboLimit(kinds[i],value,command,argument));
            assert(command==0x56+i && argument==(i==3?value:value*1000));
        }
        for(unsigned value:{0u,maximum+1,UINT32_MAX}) {
            Test t;const auto out=t.limit(kinds[i],value);
            assert(out.error==-ERANGE && out.phase==AmdPboPhase::Validate && !t.fake->calls);
        }
    }
    Test invalid;assert(invalid.limit(AmdPboLimit(99)).error==-EINVAL && !invalid.fake->calls);
    for(unsigned caps:{0u,1u,4u,0x10002u,0x10003u,UINT32_MAX}) {
        Test t;const auto out=t.limit(AmdPboLimit::Ppt,100,caps);
        assert(out.error==-ENOTSUP && !out.writeAttempted && !t.fake->calls);
    }
}
void limitsAcknowledgedOnly() {
    for(unsigned i=0;i<4;++i) for(bool maximum:{false,true}) {
        Test t;const unsigned value=maximum?(i==3?100:2000):1;
        const unsigned argument=i==3?value:value*1000;
        // Even a discarded maximum request is acknowledged, never verified.
        t.fake->discardChange=maximum;const auto out=t.limit(kinds[i],value);
        assert(!out.error && out.accepted && out.writeAttempted && !out.verified);
        assert(out.phase==AmdPboPhase::Complete && out.limit==kinds[i] && out.requested==value);
        assert(out.before.valid && out.before.raw==3 && out.before.firmwareValid);
        assert(out.command==0x56+i && out.argument==argument && out.response==1);
        assert(t.fake->settings==1 && t.fake->argumentReads==6 && t.fake->capsRaw==3);
        assert(t.fake->scalarRaw==0x3f800000 && !t.fake->ocAllMHz && !t.fake->ocCoreArgument);
        messages(t,{2,0x6f,2,2,0x6f,2,0x56+i});arguments(t,0x56+i,argument);
        for(unsigned n=0;n<4;++n)
            assert(t.fake->limitArguments[n]==(!maximum && n==i?argument:0));
    }
    Test unknown;unknown.fake->capsRaw=0x80000003;
    const auto out=unknown.limit(AmdPboLimit::Edc,120,0x80000003);
    assert(!out.error && out.accepted && !out.verified && out.before.raw==0x80000003);
}
void changesBeforeWrite() {
    Test stale;stale.fake->capsRaw=0x80000003;const auto old=stale.limit();
    assert(old.error==-EAGAIN && old.before.raw==0x80000003 && !old.writeAttempted && !stale.fake->settings);
    messages(stale,{2,0x6f,2});
    for(unsigned stage=0;stage<6;++stage) {
        Test t;bool changed=false;
        const unsigned boundary=stage==0?2:stage==2?5:stage==3?6:3;
        t.fake->afterRequest=[&] {
            if(changed || t.fake->argumentReads!=boundary)return;
            changed=true;
            if(stage==0 || stage==4)t.fake->firmwareVersion=0x00625401;
            if(stage==1)t.fake->capsRaw=0x80000003;
            if(stage==2)t.fake->pciId=0x153a1022;
            if(stage==3)t.fake->hypervisor=true;
            if(stage==5)t.fake->capsRaw=0x10003;
        };
        int error=0;bool attempted=false;
        if(stage&1) {const auto out=t.mode();error=out.error;attempted=out.writeAttempted;assert(!out.verified);}
        else {const auto out=t.limit();error=out.error;attempted=out.writeAttempted;assert(!out.verified);}
        const int expected=stage==2?-ENODEV:(stage==3 || stage==4)?-ENOTSUP:-EAGAIN;
        assert(changed && error==expected && !attempted && !t.fake->settings);
        assert(t.fake->commands<7);
    }
}
void modeTransitions() {
    for(bool enabled:{false,true}) {
        Test t;const unsigned before=enabled?3:0x10003;t.fake->capsRaw=before;
        const auto out=t.mode(enabled,before);
        assert(!out.error && out.verified && out.accepted && out.writeAttempted && !out.unchanged);
        assert(out.phase==AmdPboPhase::Complete && out.requestedEnabled==enabled);
        assert(out.before.raw==before && out.after.valid && out.after.firmwareValid);
        assert(out.after.raw==(enabled?0x10003u:3u) && t.fake->settings==1);
        messages(t,{2,0x6f,2,2,0x6f,2,enabled?0x5du:0x5eu,2,0x6f,2});arguments(t);
    }
    Test changed;changed.fake->overrideModeCaps=true;changed.fake->modeCapsAfter=0x80010000;
    const auto out=changed.mode();
    assert(out.verified && out.before.raw==3 && out.after.raw==0x80010000 && out.accepted);
    assert(!amdManualOcAvailable(out.after.raw) && amdManualOcEnabled(out.after.raw));
}
void modeNoopAndPermission() {
    for(bool enabled:{false,true}) {
        Test t;t.fake->capsRaw=enabled?0x10000:0;const auto out=t.mode(enabled,t.fake->capsRaw);
        assert(!out.error && out.verified && out.unchanged && !out.writeAttempted && !out.accepted);
        assert(out.phase==AmdPboPhase::Complete && out.before.raw==out.after.raw && !t.fake->settings);
        messages(t,{2,0x6f,2});arguments(t);
    }
    Test unavailable;unavailable.fake->capsRaw=2;const auto denied=unavailable.mode(true,2);
    assert(denied.error==-ENOTSUP && !denied.writeAttempted && !unavailable.fake->settings);
    messages(unavailable,{2,0x6f,2});
    Test disable;disable.fake->capsRaw=0x10000;const auto out=disable.mode(false,0x10000);
    assert(out.verified && out.accepted && !out.unchanged && !out.after.raw && disable.fake->settings==1);
    messages(disable,{2,0x6f,2,2,0x6f,2,0x5e,2,0x6f,2});arguments(disable);
}
void modeReadbackAndIdentity() {
    Test drop;drop.fake->discardChange=true;const auto ignored=drop.mode();
    assert(ignored.error==-EIO && ignored.accepted && !ignored.verified && ignored.after.valid);
    assert(ignored.after.raw==3 && ignored.phase==AmdPboPhase::Verify && drop.fake->settings==1);
    messages(drop,{2,0x6f,2,2,0x6f,2,0x5d,2,0x6f,2});
    for(unsigned stage=0;stage<3;++stage) {
        Test t;bool changed=false;
        t.fake->afterRequest=[&] {
            if(changed || !t.fake->settings || (stage==2 && t.fake->argumentReads!=8))return;
            changed=true;
            if(stage==1)t.fake->model=8;else t.fake->firmwareVersion=0x00625401;
        };
        const auto out=t.mode();
        assert(changed && out.error==(stage==2?-EAGAIN:-ENOTSUP));
        assert(out.accepted && out.writeAttempted && !out.verified && !out.after.valid);
        assert(out.phase==AmdPboPhase::Verify && t.fake->settings==1);
        assert(t.fake->commands==(stage==0?8u:stage==1?7u:10u));
    }
}
void rejectionsAndTimeouts() {
    for(bool mode:{false,true}) for(unsigned response:{0u,2u}) {
        Test t;t.fake->writeCompletion=response;
        int error=0;bool attempted=false,accepted=false,verified=false;
        if(mode) {const auto out=t.mode();error=out.error;attempted=out.writeAttempted;accepted=out.accepted;verified=out.verified;}
        else {const auto out=t.limit();error=out.error;attempted=out.writeAttempted;accepted=out.accepted;verified=out.verified;}
        assert(error==(response?-EIO:-ETIMEDOUT) && attempted && !accepted && !verified);
        assert(t.fake->settings==1 && t.fake->commands==7);
    }
    Test query;query.fake->completion=0xfe;const auto bad=query.read();
    assert(bad.error==-EIO && !bad.valid && !bad.raw && !query.fake->settings);messages(query,{2});
    Test deadline;deadline.fake->busyBefore=true;
    assert(deadline.limit(AmdPboLimit::Ppt,100,3,nullptr,1).error==-ETIMEDOUT);
    assert(!deadline.fake->commands && !deadline.fake->settings);
}
void faultsAndCancellation() {
    // Locate transaction boundaries once. Do not replay every PCI/CPUID I/O:
    // that multiplies real transport delays without adding distinct coverage.
    Test baseline;unsigned beforeCommandData=0,afterCommandData=0;
    baseline.fake->afterRequest=[&] {
        if(!beforeCommandData && baseline.fake->argumentReads==6 && !baseline.fake->settings && baseline.fake->index==0x3b10524)
            beforeCommandData=baseline.fake->calls;
        if(!afterCommandData && baseline.fake->settings)afterCommandData=baseline.fake->calls;
    };
    assert(baseline.limit().accepted && beforeCommandData && afterCommandData==beforeCommandData+1);
    for(unsigned at:{0u,beforeCommandData,afterCommandData}) {
        Test t;t.fake->failAt=int(at);const auto out=t.limit();
        assert(out.error==-EACCES && !out.accepted && !out.verified && t.fake->calls==at+1);
        assert(t.fake->settings==(at==afterCommandData?1u:0u));
        assert(out.writeAttempted==(at!=0));
    }
    // A failure after the acknowledged write retains accepted=true, but the
    // mode result cannot claim verification from an incomplete fresh query.
    Test verify;bool armed=false;
    verify.fake->afterRequest=[&] {
        if(!armed && verify.fake->settings && verify.fake->argumentReads==7) {
            armed=true;verify.fake->failAt=int(verify.fake->calls);
        }
    };
    const auto failure=verify.mode();
    assert(armed && failure.error==-EACCES && failure.accepted && !failure.verified && !failure.after.valid);
    assert(verify.fake->settings==1 && verify.fake->commands==8);
    for(unsigned at:{0u,beforeCommandData,afterCommandData}) {
        Test t;std::atomic<bool> cancelled{at==0};
        t.fake->afterRequest=[&] {if(t.fake->calls==at)cancelled.store(true);};
        const auto out=t.limit(AmdPboLimit::Ppt,100,3,&cancelled);
        assert(out.error==-ECANCELED && !out.accepted && !out.verified && t.fake->calls==at);
        assert(t.fake->settings==(at==afterCommandData?1u:0u));
        assert(out.writeAttempted==(at!=0));
    }
    for(unsigned boundary:{6u,9u}) {
        Test t;std::atomic<bool> cancelled{false};
        t.fake->afterRequest=[&] {if(t.fake->argumentReads==boundary)cancelled.store(true);};
        const auto out=t.mode(true,3,&cancelled);
        assert(out.error==-ECANCELED && !out.verified && t.fake->settings==(boundary==9?1u:0u));
        assert(out.accepted==(boundary==9) && out.writeAttempted==(boundary==9));
    }
}
void closedTransportsAndOldApis() {
    for(unsigned command=0;command<256;++command) {
        assert(!allowedSmuMessage(SmuProfile::GraniteRidge,command));
        if(command!=2 && command!=0x6f && (command<0x56 || command>0x59)) {
            Test t;assert(t.service.transaction([&](HardwareSession &s) {
                return detail::exchangeGraniteLimits(s,command,1000).error;
            })==-EINVAL && !t.fake->calls);
        }
        if(command!=2 && command!=0x6f && command!=0x5d && command!=0x5e) {
            Test t;assert(t.service.transaction([&](HardwareSession &s) {
                return detail::exchangeGraniteMode(s,command).error;
            })==-EINVAL && !t.fake->calls);
        }
    }
    for(unsigned command:{2u,0x6fu,0x5du,0x5eu}) for(unsigned argument:{1u,UINT32_MAX}) {
        Test t;assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeGraniteMode(s,command,argument).error;
        })==-EINVAL && !t.fake->calls);
    }
    for(unsigned command:{2u,0x6fu}) {
        Test t;assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeGraniteLimits(s,command,1).error;
        })==-EINVAL && !t.fake->calls);
    }
    for(unsigned command:{0x56u,0x57u,0x58u,0x59u}) {
        const unsigned maximum=command==0x59?100:2000000;
        for(unsigned argument:{0u,maximum+1,UINT32_MAX}) {
            Test t;assert(t.service.transaction([&](HardwareSession &s) {
                return detail::exchangeGraniteLimits(s,command,argument).error;
            })==-EINVAL && !t.fake->calls);
        }
        if(command!=0x59) {
            Test t;assert(t.service.transaction([&](HardwareSession &s) {
                return detail::exchangeGraniteLimits(s,command,1001).error;
            })==-EINVAL && !t.fake->calls);
        }
    }
    Test old;
    assert(old.service.transaction([&](HardwareSession &s) {
        assert(readShimadaPboCapabilities(s,old.target).error==-ENOTSUP);
        assert(setShimadaPboLimit(s,old.target,3,AmdPboLimit::Ppt,100).error==-ENOTSUP);
        assert(setShimadaOcMode(s,old.target,3,true).error==-ENOTSUP);
        assert(readShimadaPboFmax(s,old.target).error==-ENOTSUP);
        assert(readShimadaPboScalar(s,old.target).error==-ENOTSUP);
        return 0;
    })==0 && !old.fake->calls);
}
}
int main() {
    identitiesAndVersions();capabilityReads();limitInputGuards();limitsAcknowledgedOnly();
    changesBeforeWrite();modeTransitions();modeNoopAndPermission();modeReadbackAndIdentity();
    rejectionsAndTimeouts();faultsAndCancellation();closedTransportsAndOldApis();
    std::cout << "11 Granite controls scenario groups passed\n";
}
