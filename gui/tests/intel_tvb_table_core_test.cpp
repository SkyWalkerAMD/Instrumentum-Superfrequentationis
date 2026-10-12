// SPDX-License-Identifier: GPL-2.0-only
#include "../core/intel_tvb.h"
#include "intel_tvb_table_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace octool::core;
namespace {
struct Test {
    IntelTvbTableFixture *device = new IntelTvbTableFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(device)};
    IntelTvbTableSnapshot read(unsigned entry = 0, unsigned timeout = 5000, const std::atomic<bool> *cancelled = nullptr) {
        IntelTvbTableSnapshot out;
        const int error = service.transaction([&](HardwareSession &s) { out = readIntelTvbTable(s,130,entry); return out.error; },timeout,cancelled);
        if (error) out.error = error;
        return out;
    }
};
void decoding() {
    auto r = decodeIntelTvbThresholds(0x02644601);
    assert(r.temperatureA == 70 && r.downBinsA == 1 && r.temperatureB == 100 && r.downBinsB == 2);
    r = decodeIntelTvbThresholds(0x81fe7f23);
    assert(r.temperatureA == 127 && r.downBinsA == 35 && r.temperatureB == 254 && r.downBinsB == 129);
    for (unsigned shift = 0; shift < 32; shift += 8) for (unsigned value = 0; value <= 255; ++value) {
        const auto fields = decodeIntelTvbThresholds(std::uint32_t(value) << shift);
        const unsigned bytes[] = {fields.downBinsA,fields.temperatureA,fields.temperatureB,fields.downBinsB};
        for (unsigned n = 0; n < 4; ++n) assert(bytes[n] == (n*8 == shift ? value : 0));
    }
    IntelTvbTableRow row; row.activeCores = 1;
    assert(!intelTvbTableRowValid(row));
    row.response.commandAttempted = row.response.completed = true;
    assert(intelTvbTableRowValid(row));
    row.activeCores = 9; assert(!intelTvbTableRowValid(row));
}
void querySelectionAndNoSettings() {
    for (unsigned selected = 0; selected <= 8; ++selected) {
        Test t; t.device->locked = true; // The read path does not require unlocking OC.
        const auto out = t.read(selected);
        assert(!out.error && out.scanCompleted && out.cpu == 130 && out.selectedActiveCores == selected);
        assert(out.rows.size() == (selected ? 1u : 8u));
        for (unsigned i = 0; i < out.rows.size(); ++i) {
            const auto &r = out.rows[i];
            assert(r.activeCores == (selected ? selected : i+1) && intelTvbTableRowValid(r));
            assert(r.response.data == t.device->entries[r.activeCores-1]);
        }
        for (const auto &r : t.device->requests) {
            assert(r.address == 0x150 && r.cpu == 130);
            if (r.write) assert(((r.value >> 32) & 255) == 0x24 && !std::uint32_t(r.value) && ((r.value >> 48) & 255) == 0);
        }
        assert(t.device->mutations == 0 && t.device->settingsAttempts() == 0 && !t.device->wrongCpu);
    }
}
void validationAndGates() {
    for (unsigned bad : {9u,256u,UINT32_MAX}) {
        Test t; assert(t.read(bad).error == -EINVAL && !t.device->calls);
    }
    for (unsigned gate = 0; gate < 4; ++gate) {
        Test t;
        if (gate == 0) t.device->amd = true;
        if (gate == 1) t.device->model = 0x8f;
        if (gate == 2) t.device->msr = false;
        if (gate == 3) t.device->hypervisor = true;
        const auto out = t.read();
        assert(out.error == -ENOTSUP && !out.scanCompleted && out.rows.empty() && t.device->requests.empty());
    }
}
void firmwareRejections() {
    for (unsigned status = 1; status <= 255; ++status) {
        Test t; t.device->statuses[1] = status;
        const auto out = t.read();
        assert(out.error == -EIO && out.scanCompleted && out.rows.size() == 8);
        for (unsigned i = 0; i < 8; ++i) assert(intelTvbTableRowValid(out.rows[i]) == (i != 1));
        const auto &failed = out.rows[1].response;
        assert(failed.completed && failed.commandAttempted && failed.firmwareStatus == status && !failed.data);
        assert(t.device->selectors.back() == 8);
    }
}
void everyTransportFailure() {
    Test baseline; assert(!baseline.read().error); const auto calls = baseline.device->calls;
    for (unsigned at = 0; at < calls; ++at) {
        Test t; t.device->failAt = int(at);
        const auto out = t.read();
        assert(out.error == -EACCES && !out.scanCompleted && t.device->calls == at+1);
        if (!out.rows.empty()) {
            const auto &last = out.rows.back();
            assert(!intelTvbTableRowValid(last) && !last.response.data && !last.response.completed);
            for (unsigned i = 0; i+1 < out.rows.size(); ++i) assert(intelTvbTableRowValid(out.rows[i]));
        }
        assert(t.device->mutations == 0);
    }
}
void boundedBusy() {
    for (unsigned entry : {0u,1u,4u,8u}) {
        Test t; t.device->busyBefore = !entry; t.device->busyEntry = entry;
        const auto out = t.read();
        assert(out.error == -ETIMEDOUT && !out.scanCompleted && t.device->calls < 140);
        assert(t.device->selectors.size() == entry);
        assert(!intelTvbTableRowValid(out.rows.back()) && !out.rows.back().response.data);
        assert(t.device->settingsAttempts() == 0);
    }
}
void cancellationAndDeadline() {
    for (bool timeout : {false,true}) for (bool afterQuery : {false,true}) {
        Test t; std::atomic<bool> cancelled(false);
        t.device->afterRequest = [&] {
            if (t.device->requests.back().write != afterQuery) return;
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(25));
            else cancelled.store(true);
        };
        const auto out = t.read(0,timeout ? 10 : 1000,&cancelled);
        assert(out.error == (timeout ? -ETIMEDOUT : -ECANCELED) && !out.scanCompleted);
        assert(t.device->selectors.size() == (afterQuery ? 1u : 0u));
        for (const auto &row : out.rows) assert(!intelTvbTableRowValid(row) && !row.response.data);
    }
}
void firstCompletionSample() {
    Test t;
    t.device->afterRequest = [&] {
        if (!t.device->requests.back().write && !t.device->selectors.empty()) t.device->mailbox = UINT64_MAX;
    };
    const auto out = t.read(1);
    assert(!out.error && out.scanCompleted && out.rows.size() == 1);
    assert(out.rows[0].response.data == 0x02644601 && t.device->selectors.size() == 1);
}
}
int main() {
    decoding(); querySelectionAndNoSettings(); validationAndGates(); firmwareRejections();
    everyTransportFailure(); boundedBusy(); cancellationAndDeadline(); firstCompletionSample();
    std::cout << "8 Intel TVB table scenario groups passed\n";
}
