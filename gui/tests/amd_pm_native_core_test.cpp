// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pm_native.h"
#include "amd_pm_native_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace octool::core;
namespace {
struct Test {
    AmdPmNativeFixture *fake = new AmdPmNativeFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() { target.cpu = 130; target.profile = SmuProfile::Shimada; }
    AmdPmNativeRead run(const AmdPmOptions &options = {}, const std::atomic<bool> *cancel = nullptr, int timeout = 10000, bool map = false) {
        AmdPmNativeRead r;
        const int e = service.transaction([&](HardwareSession &s) { r = readShimadaPmNative(s,target,options,map); return r.error; },timeout,cancel);
        if (e) r.error = e;
        return r;
    }
};
void layoutsAndPackets() {
    for (const auto &layout : amdPmLayouts()) {
        Test t; t.fake->version = layout.version; t.fake->size = unsigned(layout.bytes); t.fake->bytes = pm_fixture::bytes(layout.bytes);
        AmdPmOptions o; o.hasBclk = true; o.bclkMHz = 102.5;
        const auto r = t.run(o);
        assert(!r.error && r.phase == AmdPmNativePhase::Complete && r.metadataRechecked);
        assert(r.transferAttempted && r.transferAccepted && r.address == 0x123456000ull && r.bytesRead == layout.bytes);
        assert(r.snapshot.raw == t.fake->bytes && r.snapshot.fields.size() == 22 && r.snapshot.originalArrays.size() == 92);
        assert(t.fake->messages == std::vector<std::uint32_t>({5,4,3,5,4}) && !t.fake->wrongCpu);
        assert(t.fake->memoryReads == layout.bytes/4 && t.fake->packets.size() == 5);
        for (std::size_t i = 0; i < 5; ++i) for (unsigned j = 0; j < 6; ++j)
            assert(t.fake->packets[i][j] == ((i == 1 || i == 4) && j < 2 ? 1u : 0u));
        for (const auto &request : t.fake->requests) if (request.space == Space::Memory) assert(!request.write);
        const auto imported = parseAmdPmCapture(amdPmJson(r.snapshot)); assert(!imported.error && imported.raw == r.snapshot.raw);
        assert(amdPmNativeJson(r).find("\"origin_verified\":false") != std::string::npos);
    }
}
void validation() {
    for (unsigned mode = 0; mode < 11; ++mode) {
        Test t;
        if (mode == 0) t.target.profile = SmuProfile::Phoenix;
        if (mode == 1) t.target.bus = 1;
        if (mode == 2) t.target.device = 1;
        if (mode == 3) t.target.function = 1;
        if (mode == 4) t.fake->model = 0x18;
        if (mode == 5) t.fake->family = 0x19;
        if (mode == 6) t.fake->pciId ^= 0x10000;
        if (mode == 7) t.fake->amd = false;
        if (mode == 8) t.fake->hypervisor = true;
        if (mode == 9) t.fake->physicalBits = 31;
        if (mode == 10) t.fake->physicalBits = 53;
        const auto r = t.run(); assert(r.error && !r.transferAttempted && !t.fake->commands && !t.fake->memoryReads);
    }
    for (double bclk : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        Test t; AmdPmOptions o; o.hasBclk = true; o.bclkMHz = bclk;
        assert(t.run(o).error == -EINVAL && !t.fake->calls);
    }
}
void metadataAndBounds() {
    for (unsigned size : {0u,2840u,2848u,11636u,UINT32_MAX}) {
        Test t; t.fake->size = size; const auto r = t.run();
        assert(r.error == -EMSGSIZE && r.version == 0x730204 && !r.metadataValid && t.fake->commands == 1 && !t.fake->memoryReads);
    }
    Test unknown; unknown.fake->version = 0x730205; assert(unknown.run().error == -ENOTSUP && unknown.fake->commands == 1);
    for (std::uint64_t address : std::array<std::uint64_t,7>{{0,4,4092,4097,0xfffffffffffcull,0x1000000000000ull,UINT64_MAX}}) {
        Test t; t.fake->base = address; const auto r = t.run();
        assert(r.error == -ERANGE && !r.addressValid && !r.transferAttempted && !t.fake->memoryReads && t.fake->commands == 2);
    }
    Test low; low.fake->base = 4096; low.fake->physicalBits = 32; assert(!low.run().error);
    Test width; width.fake->base = 0xfffffff0u; width.fake->physicalBits = 32; assert(width.run().error == -ERANGE);
}
void accessFaults() {
    Test good; assert(!good.run().error);
    auto calls = good.fake->pciCalls;
    calls.push_back(good.fake->memoryCalls.front()); calls.push_back(good.fake->memoryCalls.at(350)); calls.push_back(good.fake->memoryCalls.back());
    // Include every CPUID call, identified by the gaps in backend execute calls.
    for (unsigned i = 0; i < good.fake->calls; ++i) {
        bool execute = false;
        for (auto n : good.fake->pciCalls) if (n == i) execute = true;
        for (auto n : good.fake->memoryCalls) if (n == i) execute = true;
        if (!execute) calls.push_back(i);
    }
    for (auto i : calls) {
        Test t; t.fake->failAt = int(i); const auto r = t.run();
        assert(r.error == -EACCES && r.snapshot.raw.empty() && r.snapshot.fields.empty() && !r.metadataRechecked);
        assert(t.fake->calls == i+1);
    }
}
void rejectedAndChanged() {
    for (unsigned n = 0; n < 5; ++n) {
        Test t; t.fake->completions[n] = 0xfd; const auto r = t.run();
        assert(r.error == -EIO && r.snapshot.raw.empty() && t.fake->commands == n+1);
        assert(r.transferAccepted == (n > 2) && r.transferAttempted == (n >= 2));
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        Test t;
        if (mode == 0) t.fake->versionAfter = 0x730404;
        if (mode == 1) t.fake->sizeAfter = 2848;
        if (mode == 2) t.fake->baseAfter = 0x223456000ull;
        if (mode == 3) t.fake->afterRequest = [&] { if (t.fake->memoryReads == 711) t.fake->physicalBits = 47; };
        const auto r = t.run(); assert(r.error == -EAGAIN && r.snapshot.raw.empty() && r.bytesRead == 2844 && r.transferAccepted);
    }
    Test busy; busy.fake->busyBefore = true; assert(busy.run({},nullptr,100).error == -ETIMEDOUT && !busy.fake->commands);
    Test waiting; waiting.fake->completions[2] = 0; const auto r = waiting.run();
    assert(r.error == -ETIMEDOUT && r.transferAttempted && !r.transferAccepted && !waiting.fake->memoryReads);
}
void cancellation() {
    Test baseline; assert(!baseline.run().error);
    const unsigned positions[] = {1,30,65,100,baseline.fake->memoryCalls.front()+1,
        baseline.fake->memoryCalls.at(350)+1,baseline.fake->memoryCalls.back()+1,baseline.fake->calls};
    for (auto pos : positions) {
        Test t; std::atomic<bool> stop{false}; t.fake->afterRequest = [&] { if (t.fake->calls >= pos) stop.store(true); };
        const auto r = t.run({},&stop); assert(r.error == -ECANCELED && r.snapshot.raw.empty() && !r.metadataRechecked);
    }
}
void partialDecode() {
    Test t; pm_fixture::put(t.fake->bytes,0x20c,0x7f800001); const auto r = t.run();
    assert(r.error == -EDOM && r.phase == AmdPmNativePhase::Decode && r.metadataRechecked && r.snapshot.raw == t.fake->bytes);
    assert(r.snapshot.fields[0].error == -EDOM && r.snapshot.fields[1].value == 1800);
}
void ccdMapping() {
    for (const auto &layout : amdPmLayouts()) {
        Test t; t.fake->version = layout.version; t.fake->size = unsigned(layout.bytes); t.fake->bytes = pm_fixture::bytes(layout.bytes);
        const unsigned n = unsigned((layout.bytes-0x634)/0x274);
        t.fake->presentMask = (1u << (n-1))-1u + (1u << 15);
        const auto r = t.run({},nullptr,10000,true);
        assert(!r.error && r.mappingValid && r.mappingRechecked && r.mappingRequested);
        assert(r.firmwareCcdIds.size() == n && r.firmwareCcdIds.front() == 0 && r.firmwareCcdIds.back() == 15);
        assert(r.presentMaskBefore == t.fake->presentMask && r.presentMaskAfter == r.presentMaskBefore);
        assert(r.queriedBefore == 16 && r.queriedAfter == 16 && t.fake->presenceQueries == 32 && t.fake->smuQueries == 3);
        assert(t.fake->commands == 40 && r.snapshot.raw == t.fake->bytes);
        for (auto command : t.fake->messages) assert(command == 2 || command == 3 || command == 4 || command == 5 || command == 0xa5);
        for (const auto &request : t.fake->requests) if (request.space == Space::Memory) assert(!request.write);
        const auto json = amdPmNativeJson(r);
        assert(json.find("\"os_cpu_mapping_verified\":false") != std::string::npos);
    }
    Test old; const auto r = old.run(); assert(!r.mappingRequested && r.firmwareCcdIds.empty() && old.fake->commands == 5);
}
void ccdMappingRejected() {
    for (unsigned mode = 0; mode < 9; ++mode) {
        Test t;
        if (mode == 0) t.fake->smuVersion = 0x732e01;
        if (mode == 1) t.fake->presentMask = 1;
        if (mode == 2) t.fake->presentMask = 0;
        if (mode == 3) t.fake->presentMaskAfter = 0x4001;
        if (mode == 4) t.fake->smuVersionAfter = 0x732e01;
        if (mode == 5) { t.fake->presenceFailAt = 7; t.fake->presenceFailure = 0xfd; }
        if (mode == 6) { t.fake->presenceFailAt = 23; t.fake->presenceFailure = 0xff; }
        if (mode == 7) { t.fake->presenceFailAt = 16; t.fake->presenceFailure = 0; }
        if (mode == 8) t.fake->afterRequest = [&] { if (t.fake->presenceQueries == 32) t.fake->physicalBits = 47; };
        const auto r = t.run({},nullptr,10000,true);
        assert(r.error && !r.mappingValid && !r.mappingRechecked && r.firmwareCcdIds.empty() && r.snapshot.raw.empty());
        if (mode < 3) assert(!t.fake->memoryReads && !r.transferAttempted);
        if (mode == 0) assert(!t.fake->presenceQueries && r.error == -ENOTSUP);
        if (mode == 3 || mode == 4 || mode == 8) assert(r.error == -EAGAIN);
    }
}
void ccdMappingFaultsAndCancel() {
    Test good; assert(!good.run({},nullptr,10000,true).error);
    // Every queried target in both passes: a failed command write must never
    // become an absent target. Also cover reply and argument-read failures.
    auto positions = good.fake->presenceCalls;
    positions.push_back(positions.front()+2); positions.push_back(positions.front()+4);
    positions.push_back(good.fake->calls-1);
    for (auto pos : positions) {
        Test t; t.fake->failAt = int(pos);
        const auto r = t.run({},nullptr,10000,true);
        assert(r.error == -EACCES && !r.mappingValid && r.firmwareCcdIds.empty() && r.snapshot.raw.empty());
        assert(t.fake->calls == pos+1);
    }
    for (unsigned queries : {1u,2u,16u,17u,32u}) {
        Test t; std::atomic<bool> stop{false};
        t.fake->afterRequest = [&] { if (t.fake->presenceQueries == queries) stop.store(true); };
        const auto r = t.run({},&stop,10000,true);
        assert(r.error == -ECANCELED && !r.mappingValid && r.firmwareCcdIds.empty() && r.snapshot.raw.empty());
    }
}
}
int main() {
    layoutsAndPackets(); validation(); metadataAndBounds(); accessFaults(); rejectedAndChanged(); cancellation(); partialDecode();
    ccdMapping(); ccdMappingRejected(); ccdMappingFaultsAndCancel();
    std::cout << "10 AMD native PM scenario groups passed\n";
}
