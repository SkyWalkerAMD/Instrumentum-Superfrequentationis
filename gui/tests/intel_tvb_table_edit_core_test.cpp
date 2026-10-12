// SPDX-License-Identifier: GPL-2.0-only
#include "intel_tvb.h"
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
    IntelTvbTableEditSnapshot read(unsigned entry = 1) {
        IntelTvbTableEditSnapshot out;
        const int error = service.transaction([&](HardwareSession &s) { out = readIntelTvbTableEdit(s,130,entry); return out.error; });
        assert(error == out.error); return out;
    }
    IntelTvbTableUpdate apply(const IntelTvbTableEditSnapshot &old,
                              IntelTvbTableField field = IntelTvbTableField::TemperatureA, unsigned value = 80) {
        IntelTvbTableUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelTvbTable(s,old,field,value); return out.error; });
        assert(error == out.error); return out;
    }
};
const unsigned shifts[] = {8,0,16,24};
void encoding() {
    for (unsigned field = 0; field < 4; ++field) for (unsigned value = 0; value <= 256; ++value) {
        const unsigned maximum = field % 2 == 0 ? 100 : 10;
        std::uint32_t encoded = 123;
        const auto error = encodeIntelTvbTable(IntelTvbTableField(field),value,0x02644601,encoded);
        if (value > maximum) assert(error == -ERANGE && encoded == 123);
        else {
            const auto mask = UINT32_C(255) << shifts[field];
            assert(!error && ((encoded & mask) >> shifts[field]) == value);
            assert((encoded & ~mask) == (UINT32_C(0x02644601) & ~mask));
        }
    }
    std::uint32_t encoded = 123;
    assert(encodeIntelTvbTable(IntelTvbTableField(4),0,0,encoded) == -EINVAL && encoded == 123);
    assert(encodeIntelTvbTable(IntelTvbTableField::TemperatureA,UINT32_MAX,0,encoded) == -ERANGE && encoded == 123);
    for (unsigned field = 0; field < 4; ++field) {
        const auto invalid = std::uint32_t(field % 2 == 0 ? 101 : 11) << shifts[field];
        assert(!intelTvbTableValuesEditable(invalid));
        assert(encodeIntelTvbTable(IntelTvbTableField(field),0,invalid,encoded) == -ERANGE && encoded == 123);
    }
    assert(intelTvbTableValuesEditable(0) && intelTvbTableValuesEditable(0x0a64640a));
}
void readingAndGates() {
    for (unsigned entry = 1; entry <= 8; ++entry) {
        Test t; const auto old = t.read(entry);
        assert(!old.error && old.valid && old.activeCores == entry && old.configuration.cpu == 130);
        assert(old.value.data == t.device->entries[entry-1] && intelTvbTableEditable(old) == (entry == 1));
        assert((t.device->commands == std::vector<unsigned>{0x18,0x24}) && !t.device->tableSettingsAttempts() && !t.device->wrongCpu);
    }
    for (unsigned entry : {0u,9u,256u,UINT32_MAX}) {
        Test t; assert(t.read(entry).error == -EINVAL && !t.device->calls);
    }
    for (unsigned gate = 0; gate < 4; ++gate) {
        Test t;
        if (gate == 0) t.device->amd = true;
        if (gate == 1) t.device->model = 0x8f;
        if (gate == 2) t.device->msr = false;
        if (gate == 3) t.device->hypervisor = true;
        assert(t.read().error == -ENOTSUP && t.device->requests.empty());
    }
    Test t; t.device->locked = true; const auto old = t.read(); const auto calls = t.device->calls;
    assert(!old.error && old.valid && !intelTvbTableEditable(old));
    assert(t.apply(old).error == -EPERM && t.device->calls == calls);
}
void invalidSnapshots() {
    Test t; const auto old = t.read(); const auto calls = t.device->calls;
    for (unsigned mode = 0; mode < 12; ++mode) {
        auto bad = old;
        if (mode == 0) bad.valid = false;
        if (mode == 1) bad.error = -EIO;
        if (mode == 2) bad.activeCores = 0;
        if (mode == 3) bad.activeCores = 9;
        if (mode == 4) bad.value.completed = false;
        if (mode == 5) bad.value.commandAttempted = false;
        if (mode == 6) bad.value.error = -EIO;
        if (mode == 7) bad.value.firmwareStatus = 128;
        if (mode == 8) bad.configuration.valid = false;
        if (mode == 9) bad.configuration.error = -EIO;
        if (mode == 10) bad.configuration.identity.model = 0x8f;
        if (mode == 11) bad.configuration.response.commandAttempted = false;
        assert(!intelTvbTableEditable(bad) && t.apply(bad).error == -EINVAL && t.device->calls == calls);
    }
    assert(t.apply(old,IntelTvbTableField(4)).error == -EINVAL && t.device->calls == calls);
    assert(t.apply(old,IntelTvbTableField::DownBinsB,11).error == -ERANGE && t.device->calls == calls);
    const auto unsupported = t.read(2); const auto end = t.device->calls;
    assert(t.apply(unsupported).error == -ERANGE && t.device->calls == end);
}
void writesAndNoops() {
    for (unsigned entry = 1; entry <= 8; ++entry) for (unsigned field = 0; field < 4; ++field)
        for (unsigned value : {0u,10u}) {
            Test t; t.device->entries[entry-1] = 0x02644601; const auto previous = t.device->entries;
            const auto old = t.read(entry); const auto out = t.apply(old,IntelTvbTableField(field),value);
            assert(!out.error && out.verified && !out.unchanged && out.writeAttempted && out.stage == IntelOcStage::Complete);
            const auto mask = UINT32_C(255) << shifts[field];
            const auto expected = (old.value.data & ~mask) | (std::uint32_t(value) << shifts[field]);
            assert(out.expected == expected && out.readback.value.data == expected);
            for (unsigned n = 0; n < 8; ++n) assert(t.device->entries[n] == (n+1 == entry ? expected : previous[n]));
            assert(t.device->tableSettingsAttempts() == 1 && t.device->settingsAttempts() == 0 && !t.device->wrongCpu);
            assert((t.device->commands == std::vector<unsigned>{0x18,0x24,0x18,0x24,0x25,0x18,0x24}));
            t.device->clearTrace(); const auto noop = t.apply(out.readback,IntelTvbTableField(field),value);
            assert(!noop.error && noop.verified && noop.unchanged && !noop.writeAttempted && !t.device->tableSettingsAttempts());
            assert((t.device->commands == std::vector<unsigned>{0x18,0x24}));
        }
}
void staleEntry() {
    for (unsigned bit = 0; bit < 32; ++bit) {
        Test t; const auto old = t.read(); t.device->entries[0] ^= UINT32_C(1) << bit;
        assert(t.apply(old).error == -EAGAIN && !t.device->tableSettingsAttempts());
    }
}
void staleContext() {
    for (unsigned bit = 0; bit < 32; ++bit) {
        Test t; const auto old = t.read(); t.device->data ^= UINT32_C(1) << bit;
        assert(t.apply(old).error == -EAGAIN && !t.device->tableSettingsAttempts());
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        Test t; const auto old = t.read();
        if (mode == 0) t.device->flexExtra ^= 1;
        if (mode == 1) t.device->locked = true;
        if (mode == 2) t.device->stepping = 2;
        if (mode == 3) t.device->hypervisor = true;
        const auto out = t.apply(old);
        assert(out.error == (mode == 0 ? -EAGAIN : mode == 1 ? -EPERM : mode == 2 ? -ENODEV : -ENOTSUP));
        assert(!out.verified && !out.writeAttempted && !t.device->tableSettingsAttempts());
    }
}
void transportFailures() {
    Test reference; const auto old = reference.read(); const unsigned reads = reference.device->calls;
    reference.device->clearTrace(); assert(reference.apply(old).verified); const unsigned writes = reference.device->calls;
    for (unsigned i = 0; i < reads; ++i) {
        Test t; t.device->failAt = int(i); const auto out = t.read();
        assert(out.error == -EACCES && !out.valid && !intelTvbTableEditable(out) && !t.device->tableSettingsAttempts());
    }
    for (unsigned i = 0; i < writes; ++i) {
        Test t; const auto before = t.read(); t.device->clearTrace(); t.device->failAt = int(i);
        const auto out = t.apply(before);
        assert(out.error == -EACCES && !out.verified && t.device->tableSettingsAttempts() <= 1);
        assert(out.writeAttempted == bool(t.device->tableSettingsAttempts()));
    }
}
void firmwareFailures() {
    for (unsigned status = 1; status <= 255; ++status) for (unsigned command : {0x18u,0x24u,0x25u}) {
        Test t; const auto old = t.read(); t.device->failCommand = command; t.device->status = status;
        const auto out = t.apply(old);
        assert(out.error == -EIO && !out.verified && out.response.firmwareStatus == status);
        assert(out.writeAttempted == (command == 0x25) && !t.device->mutations);
    }
}
void busyBounds() {
    for (unsigned command : {0u,0x18u,0x24u,0x25u}) {
        Test t; const auto old = t.read(); t.device->clearTrace();
        if (!command) t.device->busyBefore = true; else t.device->busyCommand = command;
        const auto out = t.apply(old);
        assert(out.error == -ETIMEDOUT && !out.verified && out.writeAttempted == (command == 0x25));
        assert(t.device->calls < 125 && t.device->tableSettingsAttempts() == (command == 0x25 ? 1u : 0u));
    }
}
void verifyWholeEntryAndContext() {
    for (unsigned bit = 0; bit < 32; ++bit) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (r.write && ((r.value >> 32) & 255) == 0x25) t.device->entries[0] ^= UINT32_C(1) << bit;
        };
        const auto out = t.apply(old);
        assert(out.error == -EIO && !out.verified && out.writeAttempted && t.device->tableSettingsAttempts() == 1);
    }
    for (unsigned mode = 0; mode < 7; ++mode) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (!r.write || ((r.value >> 32) & 255) != 0x25) return;
            if (mode == 0) t.device->flexExtra ^= 1;
            if (mode == 1) t.device->locked = true;
            if (mode == 2) t.device->stepping = 2;
            if (mode == 3) t.device->hypervisor = true;
            if (mode == 4) t.device->data ^= 4;
            if (mode == 5) { t.device->failCommand = 0x18; t.device->status = 254; }
            if (mode == 6) t.device->statuses[0] = 128;
        };
        const auto out = t.apply(old);
        assert(out.error && !out.verified && out.writeAttempted && t.device->tableSettingsAttempts() == 1);
    }
    Test t; const auto old = t.read(); t.device->discardChange = true;
    assert(t.apply(old).error == -EIO && t.device->tableSettingsAttempts() == 1);
}
void cancellationAndDeadline() {
    for (bool timeout : {false,true}) for (bool afterWrite : {false,true}) {
        Test t; const auto old = t.read(); std::atomic<bool> cancelled(false);
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (afterWrite ? !r.write || ((r.value >> 32) & 255) != 0x25 : r.address != 0x194) return;
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(25)); else cancelled = true;
        };
        IntelTvbTableUpdate out;
        const int error = t.service.transaction([&](HardwareSession &s) {
            out = applyIntelTvbTable(s,old,IntelTvbTableField::TemperatureA,80); return out.error;
        },timeout ? 10 : 1000,&cancelled);
        assert(error == (timeout ? -ETIMEDOUT : -ECANCELED) && !out.verified && out.writeAttempted == afterWrite);
        assert(t.device->tableSettingsAttempts() == (afterWrite ? 1u : 0u));
    }
}
}
int main() {
    encoding(); readingAndGates(); invalidSnapshots(); writesAndNoops(); staleEntry(); staleContext();
    transportFailures(); firmwareFailures(); busyBounds(); verifyWholeEntryAndContext(); cancellationAndDeadline();
    std::cout << "11 Intel TVB table editing scenario groups passed\n";
}
