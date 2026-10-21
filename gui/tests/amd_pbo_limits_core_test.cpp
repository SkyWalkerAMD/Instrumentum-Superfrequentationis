// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_rsmu.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <iostream>
using namespace octool::core;
namespace {
const AmdPboLimit kinds[] = {AmdPboLimit::Ppt,AmdPboLimit::Tdc,AmdPboLimit::Edc,AmdPboLimit::Temperature};
struct Test {
    AmdPboFixture *fake = new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() { target.cpu = 130; target.profile = SmuProfile::Shimada; fake->capsRaw = 6; }
    AmdPboLimitUpdate set(AmdPboLimit kind = AmdPboLimit::Ppt, unsigned value = 350,
                         std::uint32_t caps = 6, const std::atomic<bool> *cancel = nullptr, int timeout = 10000) {
        AmdPboLimitUpdate out;
        const auto e = service.transaction([&](HardwareSession &s) { out = setShimadaPboLimit(s,target,caps,kind,value); return out.error; }, timeout, cancel);
        if (e) out.error = e;
        return out;
    }
};
void encoding() {
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned max = i == 3 ? 100 : 2000;
        assert(amdPboLimitMaximum(kinds[i]) == max);
        for (unsigned v = 1; v <= max; ++v) {
            std::uint32_t command = 0, argument = 0;
            assert(!encodeAmdPboLimit(kinds[i],v,command,argument));
            assert(command == 0x56+i && argument == (i == 3 ? v : v*1000));
        }
        for (unsigned bad : {0u,max+1,4294968u,UINT32_MAX}) {
            Test t; const auto r = t.set(kinds[i],bad);
            assert(r.error == -ERANGE && !r.argument && !r.command && !t.fake->calls);
        }
    }
    Test invalid; assert(invalid.set(AmdPboLimit(99)).error == -EINVAL && !invalid.fake->calls);
}
void eligibility() {
    for (unsigned mode = 0; mode < 8; ++mode) {
        Test t;
        if (mode == 0) t.target.profile = SmuProfile::Phoenix;
        if (mode == 1) t.target.bus = 1;
        if (mode == 2) t.target.device = 1;
        if (mode == 3) t.target.function = 1;
        if (mode == 4) t.fake->amd = false;
        if (mode == 5) t.fake->model = 0x18;
        if (mode == 6) t.fake->family = 0x19;
        if (mode == 7) t.fake->pciId ^= 0x10000;
        assert(t.set().error && !t.fake->commands && !t.fake->settings);
    }
    for (unsigned caps : {0u,1u,3u,4u,5u,7u,UINT32_MAX}) {
        Test t; assert(t.set(AmdPboLimit::Ppt,350,caps).error == -ENOTSUP && !t.fake->calls);
    }
}
void oneLimitAcknowledgementOnly() {
    const unsigned values[] = {350,280,500,90};
    const unsigned arguments[] = {350000,280000,500000,90};
    for (unsigned i = 0; i < 4; ++i) for (bool drop : {false,true}) {
        Test t; t.fake->discardChange = drop;
        const auto r = t.set(kinds[i],values[i]);
        assert(!r.error && r.accepted && r.writeAttempted && !r.verified && r.phase == AmdPboPhase::Complete);
        assert(r.limit == kinds[i] && r.requested == values[i] && r.command == 0x56+i && r.argument == arguments[i]);
        assert(r.before.valid && r.before.raw == 6 && r.response == 1);
        assert(t.fake->settings == 1 && t.fake->argumentReads == 1 && !t.fake->wrongCpu);
        assert((t.fake->messages == std::vector<std::uint32_t>{0x6f,0x56+i}));
        for (unsigned j = 0; j < 4; ++j) assert(t.fake->limitArguments[j] == ((!drop && j == i) ? arguments[i] : 0));
        assert(!t.fake->commandArgs[0][0] && t.fake->commandArgs[1][0] == arguments[i]);
        for (const auto &args : t.fake->commandArgs) for (unsigned n = 1; n < 6; ++n) assert(!args[n]);
    }
}
void capabilityChanges() {
    for (std::uint32_t caps : {0u,2u,4u,7u,0x80000006u}) {
        Test t; t.fake->capsRaw = caps; const auto r = t.set();
        assert(r.error == -EAGAIN && r.before.raw == caps && !r.writeAttempted && !t.fake->settings);
    }
    for (std::uint32_t caps : {2u,6u,0x80000002u}) {
        Test t; t.fake->capsRaw = caps;
        assert(t.set(AmdPboLimit::Edc,100,caps).accepted);
    }
    Test identity; identity.fake->afterRequest = [&] { if (identity.fake->argumentReads) identity.fake->model = 0x18; };
    assert(identity.set().error == -ENOTSUP && !identity.fake->settings);
}
void rejections() {
    for (const auto kind : kinds) for (std::uint32_t status : {0u,2u,0xfcu,0xfdu,0xfeu,0xffu,UINT32_MAX}) {
        Test t; t.fake->writeCompletion = status; const auto r = t.set(kind,80);
        assert(r.error == (status ? -EIO : -ETIMEDOUT) && r.writeAttempted && !r.accepted && !r.verified);
        assert(t.fake->settings == 1 && t.fake->commands == 2);
    }
    Test query; query.fake->completion = 0xfe;
    assert(query.set().error == -EIO && !query.fake->settings && query.fake->commands == 1);
    Test busy; busy.fake->busyBefore = true;
    assert(busy.set().error == -ETIMEDOUT && !busy.fake->commands);
}
void faultsAndCancellation() {
    Test base; assert(base.set().accepted);
    for (unsigned n = 0; n < base.fake->calls; ++n) {
        Test t; t.fake->failAt = int(n); const auto r = t.set();
        assert(r.error == -EACCES && !r.verified && t.fake->calls == n+1 && t.fake->settings <= 1);
    }
    for (unsigned n = 0; n <= base.fake->calls; ++n) {
        Test t; std::atomic<bool> cancel{n == 0};
        t.fake->afterRequest = [&] { if (t.fake->calls == n) cancel.store(true); };
        const auto r = t.set(AmdPboLimit::Tdc,200,6,&cancel);
        assert(r.error == -ECANCELED && !r.verified && t.fake->calls == n && t.fake->settings <= 1);
    }
    Test deadline; deadline.fake->busyBefore = true;
    assert(deadline.set(AmdPboLimit::Ppt,350,6,nullptr,1).error == -ETIMEDOUT && !deadline.fake->settings);
}
void transportAllowlist() {
    for (std::uint32_t command : {0x53u,0x54u,0x55u,0x5du,0x61u,0x31u,0x3eu}) {
        Test t;
        assert(t.service.transaction([&](HardwareSession &s) { return detail::exchangeShimadaRsmu(s,command,1000,false,true).error; },10000) == -EINVAL);
        assert(!t.fake->calls);
    }
    Test t;
    assert(t.service.transaction([&](HardwareSession &s) { return detail::exchangeShimadaRsmu(s,0x56,1000,false,false).error; },10000) == -EINVAL);
    assert(!t.fake->calls);
}
}
int main() {
    encoding(); eligibility(); oneLimitAcknowledgementOnly(); capabilityChanges(); rejections(); faultsAndCancellation(); transportAllowlist();
    std::cout << "7 AMD PBO limit scenario groups passed\n";
}
