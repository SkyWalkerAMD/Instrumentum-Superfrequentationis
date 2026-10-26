// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_rsmu.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <iostream>
using namespace octool::core;
namespace {
struct Test {
    AmdPboFixture *fake = new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() { target.cpu = 130; target.profile = SmuProfile::Shimada; fake->allowStatusQueries = true; }
    AmdPboQueryResult read(AmdPboQuery query = AmdPboQuery::Ln2Mode, unsigned index = 0,
                           const std::atomic<bool> *cancel = nullptr, int timeout = 10000) {
        AmdPboQueryResult out;
        const int e = service.transaction([&](HardwareSession &s) { out = readShimadaPboQuery(s,target,query,index); return out.error; },timeout,cancel);
        if (e) out.error = e;
        return out;
    }
};
void formatAndOpaqueWords() {
    for (std::uint32_t raw : {0u,1u,2u,0x80000000u,UINT32_MAX}) {
        Test t; t.fake->ln2Raw = raw; const auto r = t.read();
        assert(r.rawValid && r.raw == raw && r.response == 1 && r.messageAttempted);
        assert(r.valid == (raw <= 1) && r.error == (raw <= 1 ? 0 : -ERANGE));
        assert(r.ln2Enabled == (raw == 1));
        assert(t.fake->settings == 0 && t.fake->messages == std::vector<std::uint32_t>{0xa6});
    }
    for (std::uint32_t raw : {0u,1u,0x7fc00000u,0x80000000u,0xdeadbeefu,UINT32_MAX}) {
        Test t; t.fake->performanceRaw = raw; const auto r = t.read(AmdPboQuery::CorePerformance,127);
        assert(!r.error && r.valid && r.rawValid && r.raw == raw && !r.ln2Enabled);
        assert(r.firmwareIndex == 127 && r.command == 0x5c && t.fake->settings == 0);
    }
}
void initializedArguments() {
    for (unsigned n : {0u,1u,7u,8u,63u,127u}) {
        Test t; assert(t.read(AmdPboQuery::CorePerformance,n).valid);
        assert(t.fake->commandArgs.size() == 1 && t.fake->commandArgs[0][0] == n);
        for (unsigned i = 1; i < 6; ++i) assert(!t.fake->commandArgs[0][i]);
        assert(t.fake->argumentReads == 1 && !t.fake->settings && !t.fake->wrongCpu);
        assert(t.fake->messages == std::vector<std::uint32_t>{0x5c});
    }
    Test t; assert(t.read().valid);
    for (auto value : t.fake->commandArgs[0]) assert(!value);
}
void invalidInputs() {
    for (unsigned n : {128u,UINT32_MAX}) {
        Test t; assert(t.read(AmdPboQuery::CorePerformance,n).error == -ERANGE && !t.fake->calls);
    }
    Test ln2; assert(ln2.read(AmdPboQuery::Ln2Mode,1).error == -ERANGE && !ln2.fake->calls);
    Test kind; assert(kind.read(static_cast<AmdPboQuery>(9)).error == -EINVAL && !kind.fake->calls);
}
void identities() {
    for (unsigned mode = 0; mode < 8; ++mode) {
        Test t;
        if (mode == 0) t.target.profile = SmuProfile::Phoenix;
        if (mode == 1) t.target.bus = 1;
        if (mode == 2) t.target.device = 1;
        if (mode == 3) t.target.function = 1;
        if (mode == 4) t.fake->amd = false;
        if (mode == 5) t.fake->family = 0x19;
        if (mode == 6) t.fake->model = 0x18;
        if (mode == 7) t.fake->pciId ^= 0x10000;
        const auto r = t.read(); assert(r.error && !r.valid && !r.rawValid && !t.fake->commands);
    }
    Test changed; changed.fake->afterRequest = [&] { if (changed.fake->argumentReads) changed.fake->model = 0x18; };
    const auto r = changed.read(); assert(r.error == -ENOTSUP && !r.rawValid && !r.valid && !r.raw);
}
void firmwareAndDeadlines() {
    for (auto query : {AmdPboQuery::Ln2Mode,AmdPboQuery::CorePerformance}) {
        for (std::uint32_t response : {0u,2u,0xfcu,0xfdu,0xfeu,0xffu,UINT32_MAX}) {
            Test t; t.fake->completion = response; const auto r = t.read(query);
            assert(r.error == (response ? -EIO : -ETIMEDOUT) && r.messageAttempted && !r.valid && !r.rawValid);
            assert(t.fake->commands == 1 && !t.fake->argumentReads && !t.fake->settings);
        }
    }
    Test busy; busy.fake->busyBefore = true;
    assert(busy.read().error == -ETIMEDOUT && !busy.fake->commands);
    Test deadline; deadline.fake->busyBefore = true;
    assert(deadline.read(AmdPboQuery::Ln2Mode,0,nullptr,1).error == -ETIMEDOUT && !deadline.fake->commands);
}
void ioAndCancellation() {
    for (auto query : {AmdPboQuery::Ln2Mode,AmdPboQuery::CorePerformance}) {
        Test base; assert(base.read(query).valid);
        for (unsigned n = 0; n < base.fake->calls; ++n) {
            Test t; t.fake->failAt = int(n); const auto r = t.read(query);
            assert(r.error == -EACCES && !r.valid && !r.rawValid && !r.raw && !t.fake->settings && t.fake->calls == n+1);
        }
        for (unsigned n = 0; n <= base.fake->calls; ++n) {
            Test t; std::atomic<bool> cancel{n == 0};
            t.fake->afterRequest = [&] { if (t.fake->calls == n) cancel.store(true); };
            const auto r = t.read(query,0,&cancel);
            assert(r.error == -ECANCELED && !r.valid && !r.rawValid && !r.raw && !t.fake->settings && t.fake->calls == n);
        }
    }
}
void closedTransport() {
    for (auto command : {0xa6u,0x5cu}) for (unsigned mode = 0; mode < 3; ++mode) {
        Test t;
        assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeShimadaRsmu(s,command,mode == 2 ? 128 : 0,mode != 0,mode != 1).error;
        },10000) == -EINVAL && !t.fake->calls);
    }
    Test t;
    assert(t.service.transaction([&](HardwareSession &s) { return detail::exchangeShimadaRsmu(s,0x61,0,true,true).error; },10000) == -EINVAL);
    assert(!t.fake->calls);
}
}
int main() {
    formatAndOpaqueWords(); initializedArguments(); invalidInputs(); identities(); firmwareAndDeadlines(); ioAndCancellation(); closedTransport();
    std::cout << "7 AMD PBO query scenario groups passed\n";
}
