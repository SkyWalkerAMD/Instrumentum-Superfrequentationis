// SPDX-License-Identifier: GPL-2.0-only
#include "intel_memory_fixture.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>

using namespace octool::core;
using intel_memory_fixture::Device;
namespace {
struct Borrowed : HardwareBackend {
    Device &d;
    explicit Borrowed(Device &device) : d(device) {}
    Reply execute(const Request &r) override { return d.execute(r); }
    CpuIdReply cpuid(unsigned c, std::uint32_t l, std::uint32_t s) override { return d.cpuid(c,l,s); }
    Backend backend(Space s) const override { return d.backend(s); }
};
IntelMemorySnapshot read(Device &d, const std::atomic<bool> *cancelled = nullptr) {
    HardwareService service(std::unique_ptr<HardwareBackend>(new Borrowed(d)));
    IntelMemoryTarget t; t.cpu = d.expectedCpu; t.controller = d.controller; t.channel = d.channel;
    IntelMemorySnapshot out;
    const int error = service.transaction([&](HardwareSession &s) { out = readIntelMemory(s,t); return out.error; },10000,cancelled);
    if (error) out.error = error;
    return out;
}
void golden() {
    for (unsigned type = 0; type < 2; ++type) {
        const auto s = decodeIntelMemory(IntelMemoryTarget{},intel_memory_fixture::registers(type));
        assert(s.valid && !s.error && s.values.size() == 30 && s.rankMask == 3 && s.dramType == type);
        assert(s.gear2 && !s.gear4 && s.commandStretch == 1 && s.nTo1 == 3);
        for (unsigned i = 0; i < 30; ++i) {
            const bool unused = (i == 7 && type == 1) || (i == 11 && type == 0);
            assert(s.values[i].error == (unused ? -ENOTSUP : 0));
            assert(s.values[i].encoded == intel_memory_fixture::expected(i));
        }
    }
}
void routing() {
    for (unsigned mc = 0; mc < 2; ++mc) for (unsigned channel = 0; channel < 2; ++channel) {
        IntelMemoryTarget t; t.controller = mc; t.channel = channel;
        assert(intelMemoryOffset(t,0xd800) == 0xd800+mc*0x10000);
        assert(intelMemoryOffset(t,0xe014) == 0xe014+mc*0x10000+channel*0x800);
        assert(!decodeIntelMemory(t,intel_memory_fixture::registers()).error);
        assert(decodeIntelMemory(t,intel_memory_fixture::registers(0)).error == (channel ? -ENODEV : 0));
    }
}
void malformedRegisters() {
    const auto initial = intel_memory_fixture::registers();
    for (unsigned i = 0; i < initial.size(); ++i) {
        auto v = initial; v.erase(v.begin()+i); assert(decodeIntelMemory({},v).error == -EINVAL);
        v = initial; v.push_back(initial[i]); assert(decodeIntelMemory({},v).error == -EINVAL);
        v = initial; v[i].offset = 0xfff0; assert(decodeIntelMemory({},v).error == -EINVAL);
        v = initial; v[i].raw = UINT64_MAX; assert(decodeIntelMemory({},v).error);
        if (intelMemoryRegisters()[i].width == 4) {
            v = initial; v[i].raw |= UINT64_C(1)<<32; assert(decodeIntelMemory({},v).error == -EINVAL);
        }
    }
    auto v = initial; v[1].offset = v[0].offset; assert(decodeIntelMemory({},v).error == -EINVAL);
}
void presenceAndType() {
    for (unsigned type = 0; type < 8; ++type) for (unsigned ranks = 0; ranks < 256; ++ranks) {
        auto v = intel_memory_fixture::registers(type); v[1].raw = ranks;
        const auto s = decodeIntelMemory({},v);
        assert(s.valid == (type < 2 && ranks > 0 && ranks < 16));
        if (!s.valid) assert(s.values.empty());
    }
}
void zerosAndUnused() {
    for (unsigned i = 3; i < 11; ++i) {
        auto v = intel_memory_fixture::registers(); v[i].raw = 0;
        const auto s = decodeIntelMemory({},v); assert(s.error == -ENODATA && s.values.empty());
    }
    auto v = intel_memory_fixture::registers(); v[3].raw &= ~UINT64_C(0xf00000);
    assert(!decodeIntelMemory({},v).error); // DDR5 does not use tPPD.
    v[0].raw = 0; assert(decodeIntelMemory({},v).error == -ENODATA);
    v = intel_memory_fixture::registers(0); v[4].raw &= 0xffffff;
    assert(!decodeIntelMemory({},v).error); // DDR4 does not use tREFSBRD.
}
void gearEncodings() {
    for (unsigned bits = 0; bits < 16; ++bits) {
        auto v = intel_memory_fixture::registers(); v[2].raw = 0;
        for (unsigned i = 0; i < 4; ++i) if (bits & (1u<<i)) v[2].raw |= UINT64_C(1) << (i == 0 ? 31 : i == 1 ? 15 : i == 2 ? 30 : 16);
        const auto s = decodeIntelMemory({},v); assert(s.valid);
        assert(s.gear2 == bool(bits&1) && s.gear4 == bool(bits&2));
        assert(s.noGear2Divide == bool(bits&4) && s.noGear4Divide == bool(bits&8));
        const auto json = intelMemoryJson(s);
        assert((json.find("\"unit\":\"encoded\"") != std::string::npos) == bool(bits&12));
        assert((json.find("\"unit\":\"tCK\"") != std::string::npos) == !bool(bits&12));
    }
}
void roundtrip() {
    auto s = decodeIntelMemory({},intel_memory_fixture::registers()); s.offline = false;
    const auto json = intelMemoryJson(s);
    assert(json.find("0x814160500042402a") != std::string::npos);
    const auto parsed = parseIntelMemoryCapture(json);
    assert(parsed.valid && parsed.offline && parsed.registers[3].raw == UINT64_C(0x814160500042402a));
    auto forged = json; const auto pos = forged.find("\"encoded\":36"); assert(pos != std::string::npos);
    forged.replace(pos,12,"\"encoded\":99");
    assert(parseIntelMemoryCapture(forged).values[0].encoded == 36);
    for (const auto *command : {"intel-memory-read","intel-memory-decode"}) {
        const auto report = "{\"schema_version\":1,\"ok\":true,\"error\":0,\"command\":\"" + std::string(command) + "\",\"data\":{\"memory\":"+json+"}}";
        assert(parseIntelMemoryCapture(report).valid);
    }
}
void malformedJson() {
    const auto json = intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers()));
    for (const auto &text : {std::string(),std::string("{}"),json+"x",json.substr(0,json.size()-1),std::string(65537,' '),
            std::string("{\"format\":1,\"format\":2}"),std::string("{\"schema_version\":1,\"ok\":false,\"error\":0}")}) {
        const auto s = parseIntelMemoryCapture(text); assert(s.error && !s.valid && s.values.empty());
    }
    for (const auto *bad : {"0x814160500042402", "0x814160500042402a0", "0x814160500042402g", "814160500042402a", "9007199254740993"}) {
        auto text = json; const auto p = text.find("0x814160500042402a"); text.replace(p,18,bad);
        assert(parseIntelMemoryCapture(text).error);
    }
    auto text = json; text.replace(text.find("raptor-lake-s-743846-001"),22,"wrong-layout"); assert(parseIntelMemoryCapture(text).error);
    text = json; text.replace(text.find("\"channel\":0"),11,"\"channel\":0.00001"); assert(parseIntelMemoryCapture(text).error);
}
void liveSuccess() {
    for (unsigned type = 0; type < 2; ++type) for (unsigned mc = 0; mc < 2; ++mc) for (unsigned ch = 0; ch < (type ? 2u : 1u); ++ch) {
        Device d; d.controller = mc; d.channel = ch; d.memory[0xd800] = type;
        const auto s = read(d); assert(s.valid && !s.offline && !s.error && s.mchbar == d.bar && s.identity.model == 0xb7);
        assert(d.reads == 37 && !d.writes && d.cpuCalls == 3 && !d.wrongCpu);
        for (const auto &r : d.requests) assert(!r.write && r.space != Space::Msr);
        assert(s.values[0].encoded == 36 && s.values[13].encoded == 24000);
    }
}
void identityGates() {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Device d;
        if (!mode) d.amd = true; else if (mode == 1) d.model = 0xba;
        else if (mode == 2) d.hypervisor = true; else if (mode == 3) d.pciId = 0xa7068086; else d.pciClass = 0x03000001;
        const auto s = read(d); assert(s.error && !s.valid && s.values.empty() && !d.writes);
        for (const auto &r : d.requests) assert(r.space == Space::Pci);
    }
}
void barGates() {
    for (const auto bar : {UINT64_C(0),UINT64_C(1),UINT64_C(0xfedc0000),UINT64_C(0xfedc0003),UINT64_C(0x400fedc0001),UINT64_MAX}) {
        Device d; d.bar = bar; const auto s = read(d); assert(s.error && !s.valid);
        for (const auto &r : d.requests) assert(r.space == Space::Pci);
    }
    Device d; d.afterRequest = [&](const Request &r) { if (r.space == Space::Pci && r.address == 0x48) d.bar += UINT64_C(1)<<32; };
    assert(read(d).error == -EAGAIN && d.reads == 5);
}
void failurePositions() {
    for (int call = 0; call < 3; ++call) { Device d; d.failCpuid = call; const auto s = read(d); assert(s.error == -EACCES && d.requests.empty()); }
    for (int call = 0; call < 37; ++call) {
        Device d; d.failRead = call; const auto s = read(d);
        assert(s.error == -EACCES && !s.valid && s.values.empty() && s.registers.empty() && int(d.reads) == call+1 && !d.writes);
    }
}
void races() {
    for (unsigned i = 0; i < 11; ++i) {
        Device d; d.afterRequest = [&](const Request &) { if (d.reads == 16) d.memory[intelMemoryRegisters()[i].offset] ^= 1; };
        assert(read(d).error && !d.writes);
    }
    Device d; d.afterRequest = [&](const Request &) { if (d.reads == 16) d.bar ^= 0x20000; };
    assert(read(d).error == -EAGAIN);
}
void invalidAndCancelled() {
    Device d; d.controller = 2; assert(read(d).error == -EINVAL && !d.cpuCalls);
    d.controller = 0; d.channel = 2; assert(read(d).error == -EINVAL && !d.cpuCalls);
    d.channel = 1; d.memory[0xd800] = 0; assert(read(d).error == -ENODEV && d.reads == 6);
    Device cancelled; std::atomic<bool> stop{true}; assert(read(cancelled,&stop).error == -ECANCELED && !cancelled.cpuCalls);
    Device active; stop = false; active.afterRequest = [&](const Request &) { if (active.reads == 7) stop = true; };
    assert(read(active,&stop).error == -ECANCELED && active.reads == 7 && !active.writes);
}
}
int main() {
    golden(); routing(); malformedRegisters(); presenceAndType(); zerosAndUnused(); gearEncodings(); roundtrip(); malformedJson();
    liveSuccess(); identityGates(); barGates(); failurePositions(); races(); invalidAndCancelled();
    std::cout << "14 Intel memory scenario groups passed\n";
}
