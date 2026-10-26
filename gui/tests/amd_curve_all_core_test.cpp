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
    Test() { target.cpu = 130; target.profile = SmuProfile::Shimada; fake->capsRaw = 6; fake->allowAllCoreCurve = true; }
    AmdCurveAllUpdate set(int margin = -25, std::uint32_t caps = 6,
                         const std::atomic<bool> *cancel = nullptr, int timeout = 10000) {
        AmdCurveAllUpdate out;
        const int e = service.transaction([&](HardwareSession &s) { out = setShimadaAllCoreCurve(s,target,caps,margin); return out.error; },timeout,cancel);
        if (e) out.error = e;
        return out;
    }
};
void encoding() {
    for (int n = -50; n <= 50; ++n) {
        std::uint32_t arg = UINT32_MAX;
        assert(!encodeShimadaAllCoreCurve(n,arg));
        assert(arg == unsigned(n < 0 ? 65536+n : n));
    }
    for (int n : {INT_MIN,-65536,-51,51,65536,INT_MAX}) {
        Test t; const auto r = t.set(n);
        assert(r.error == -ERANGE && !r.argument && !t.fake->calls);
    }
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
    for (unsigned caps : {0u,1u,2u,3u,0x10005u,0x10007u,UINT32_MAX}) {
        Test t; assert(t.set(-25,caps).error == -ENOTSUP && !t.fake->calls);
    }
}
void acknowledgementOnly() {
    for (int n : {-50,-25,0,50}) for (bool drop : {false,true}) {
        Test t; t.fake->discardChange = drop;
        const auto r = t.set(n);
        assert(!r.error && r.accepted && r.writeAttempted && !r.verified && r.phase == AmdPboPhase::Complete);
        assert(r.requestedMargin == n && r.argument == unsigned(n < 0 ? 65536+n : n));
        assert(r.before.valid && r.before.raw == 6 && r.response == 1);
        assert(t.fake->settings == 1 && t.fake->argumentReads == 1 && !t.fake->wrongCpu);
        assert((t.fake->messages == std::vector<std::uint32_t>{0x6f,7}));
        assert(t.fake->allCurveArgument == (drop ? UINT32_MAX : r.argument));
        assert(!t.fake->commandArgs[0][0] && t.fake->commandArgs[1][0] == r.argument);
        for (const auto &args : t.fake->commandArgs) for (unsigned i = 1; i < 6; ++i) assert(!args[i]);
        assert(t.fake->scalarRaw == 0x3f800000 && t.fake->capsRaw == 6 && !t.fake->fmaxMHz && !t.fake->ocAllMHz);
    }
}
void staleIdentityAndCapabilities() {
    for (std::uint32_t caps : {0u,2u,4u,7u,0x80000006u}) {
        Test t; t.fake->capsRaw = caps; const auto r = t.set();
        assert(r.error == -EAGAIN && r.before.raw == caps && !r.writeAttempted && !t.fake->settings);
    }
    for (std::uint32_t caps : {4u,5u,6u,7u,0x80000004u}) {
        Test t; t.fake->capsRaw = caps; assert(t.set(-25,caps).accepted);
    }
    Test changed; changed.fake->afterRequest = [&] { if (changed.fake->argumentReads) changed.fake->model = 0x18; };
    assert(changed.set().error == -ENOTSUP && !changed.fake->settings);
}
void firmwareRejections() {
    for (std::uint32_t status : {0u,2u,0xfcu,0xfdu,0xfeu,0xffu,UINT32_MAX}) {
        Test t; t.fake->writeCompletion = status; const auto r = t.set();
        assert(r.error == (status ? -EIO : -ETIMEDOUT) && r.writeAttempted && !r.accepted && !r.verified);
        assert(t.fake->settings == 1 && t.fake->commands == 2);
    }
    Test query; query.fake->completion = 0xfe;
    assert(query.set().error == -EIO && !query.fake->settings);
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
        const auto r = t.set(-25,6,&cancel);
        assert(r.error == -ECANCELED && !r.verified && t.fake->calls == n && t.fake->settings <= 1);
    }
    Test deadline; deadline.fake->busyBefore = true;
    assert(deadline.set(-25,6,nullptr,1).error == -ETIMEDOUT && !deadline.fake->settings);
}
void transportBounds() {
    for (std::uint32_t command : {0x61u,0x92u,0x36u,0xa8u}) {
        Test t;
        assert(t.service.transaction([&](HardwareSession &s) { return detail::exchangeShimadaRsmu(s,command,0,false,true).error; },10000) == -EINVAL);
        assert(!t.fake->calls);
    }
    Test t;
    assert(t.service.transaction([&](HardwareSession &s) { return detail::exchangeShimadaRsmu(s,7,0,false,false).error; },10000) == -EINVAL);
    assert(!t.fake->calls);
}
}
int main() {
    encoding(); eligibility(); acknowledgementOnly(); staleIdentityAndCapabilities(); firmwareRejections(); faultsAndCancellation(); transportBounds();
    std::cout << "7 AMD all-core curve scenario groups passed\n";
}
