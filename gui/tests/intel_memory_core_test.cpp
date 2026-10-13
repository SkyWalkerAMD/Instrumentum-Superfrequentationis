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
        assert(s.valid && !s.error && s.version == 4 && s.values.size() == 68 && s.rankMask == 3 && s.dramType == type);
        assert(s.gear2 && !s.gear4 && s.commandStretch == 1 && s.nTo1 == 3);
        for (unsigned i = 0; i < 43; ++i) {
            const bool unused = ((i == 7 || i == 39 || i == 42) && type == 1) || (i == 11 && type == 0);
            assert(s.values[i].error == (unused ? -ENOTSUP : i == 32 || i == 33 ? -ENODEV : 0));
            assert(s.values[i].encoded == intel_memory_fixture::expected(i));
        }
    }
}
void routing() {
    for (unsigned mc = 0; mc < 2; ++mc) for (unsigned channel = 0; channel < 2; ++channel) {
        IntelMemoryTarget t; t.controller = mc; t.channel = channel;
        assert(intelMemoryOffset(t,0xd800) == 0xd800+mc*0x10000);
        assert(intelMemoryOffset(t,0xd80c) == 0xd80c+mc*0x10000+channel*4);
        assert(intelMemoryOffset(t,0xe014) == 0xe014+mc*0x10000+channel*0x800);
        assert(intelMemoryOffset(t,0xe020) == 0xe020+mc*0x10000+channel*0x800);
        assert(intelMemoryOffset(t,0xe050) == 0xe050+mc*0x10000+channel*0x800);
        for (const auto offset : {0xe438u,0xe440u,0xe448u,0xe4c0u,0xe0b8u})
            assert(intelMemoryOffset(t,offset) == offset+mc*0x10000+channel*0x800);
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
        auto v = intel_memory_fixture::registers(type); v[2].raw = ranks;
        const auto s = decodeIntelMemory({},v);
        assert(s.valid == (type < 2 && ranks > 0 && ranks < 16));
        if (!s.valid) assert(s.values.empty());
    }
}
void zerosAndUnused() {
    for (const auto absent : {UINT64_C(0),UINT64_C(0x10001800)}) {
        auto v = intel_memory_fixture::registers(); v[1].raw = absent; v[2].raw = 15;
        assert(decodeIntelMemory({},v).error == -ENODATA);
        Device d; d.memory[0xd80c] = absent; d.memory[0xe454] = 15;
        assert(read(d).error == -ENODATA && d.reads == 7);
    }
    auto sizeOnly = intel_memory_fixture::registers(); sizeOnly[1].raw = 1u<<16;
    assert(decodeIntelMemory({},sizeOnly).valid);

    for (unsigned i = 4; i < 14; ++i) {
        auto v = intel_memory_fixture::registers(); v[i].raw = 0;
        const auto s = decodeIntelMemory({},v); assert(s.error == -ENODATA && s.values.empty());
    }
    auto v = intel_memory_fixture::registers(); v[4].raw &= ~UINT64_C(0xf00000);
    assert(!decodeIntelMemory({},v).error); // DDR5 does not use tPPD.
    v[0].raw = 0; assert(decodeIntelMemory({},v).error == -ENODATA);
    v = intel_memory_fixture::registers(0); v[5].raw &= 0xffffff;
    assert(!decodeIntelMemory({},v).error); // DDR4 does not use tREFSBRD.
}
void gearEncodings() {
    for (unsigned bits = 0; bits < 16; ++bits) {
        auto v = intel_memory_fixture::registers(); v[3].raw = 0;
        for (unsigned i = 0; i < 4; ++i) if (bits & (1u<<i)) v[3].raw |= UINT64_C(1) << (i == 0 ? 31 : i == 1 ? 15 : i == 2 ? 30 : 16);
        const auto s = decodeIntelMemory({},v); assert(s.valid);
        assert(s.gear2 == bool(bits&1) && s.gear4 == bool(bits&2));
        assert(s.noGear2Divide == bool(bits&4) && s.noGear4Divide == bool(bits&8));
        const auto json = intelMemoryJson(s);
        assert(std::string(intelMemoryUnit(s,intelMemoryFields()[0])) == (bits&12 ? "encoded" : "tCK"));
        assert((json.find("\"unit\":\"tCK\"") != std::string::npos) == !bool(bits&12));
    }
}
void roundtrip() {
    auto s = decodeIntelMemory({},intel_memory_fixture::registers()); s.offline = false;
    const auto json = intelMemoryJson(s);
    assert(json.find("0x814160500042402a") != std::string::npos);
    const auto parsed = parseIntelMemoryCapture(json);
    assert(parsed.valid && parsed.offline && parsed.registers[4].raw == UINT64_C(0x814160500042402a));
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
        assert(d.reads == 53 && !d.writes && d.cpuCalls == 3 && !d.wrongCpu);
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
    for (int call = 0; call < 53; ++call) {
        Device d; d.failRead = call; const auto s = read(d);
        assert(s.error == -EACCES && !s.valid && s.values.empty() && s.registers.empty() && int(d.reads) == call+1 && !d.writes);
    }
}
void races() {
    for (unsigned i = 0; i < 19; ++i) {
        Device d; d.afterRequest = [&](const Request &) { if (d.reads == 24) d.memory[intelMemoryRegisters()[i].offset] ^= 1; };
        assert(read(d).error && !d.writes);
    }
    Device d; d.afterRequest = [&](const Request &) { if (d.reads == 24) d.bar ^= 0x20000; };
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
void legacySnapshots() {
    for (unsigned type = 0; type < 2; ++type) {
        auto registers = intel_memory_fixture::registers(type,1);
        registers[3].raw = UINT64_C(0x180000068); // Original v1 fixture.
        const auto s = decodeIntelMemory({},registers,1);
        assert(s.valid && s.version == 1 && s.values.size() == 30);
        const auto json = intelMemoryJson(s);
        assert(json.find("octool-intel-memory-v1") != std::string::npos && json.find("RTL_Rank") == std::string::npos);
        const auto restored = parseIntelMemoryCapture(json);
        assert(restored.valid && restored.offline && restored.version == 1 && restored.registers.size() == 12);
        assert(intelMemoryJson(restored) == json);
        assert(decodeIntelMemory({},registers,2).error == -EINVAL);
        auto bad = json; bad.replace(bad.find("memory-v1"),9,"memory-v2");
        assert(parseIntelMemoryCapture(bad).error == -EINVAL);
        bad = json; bad.replace(bad.find("memory-v1"),9,"memory-v3");
        assert(parseIntelMemoryCapture(bad).error == -EINVAL);
    }
    const auto registers = intel_memory_fixture::registers();
    assert(decodeIntelMemory({},registers,1).error == -EINVAL);
    for (unsigned version : {0u,5u,UINT32_MAX}) {
        assert(intelMemoryRegisters(version).empty() && intelMemoryFields(version).empty());
        assert(decodeIntelMemory({},registers,version).error == -EINVAL);
        assert(decodeIntelMemory({}, {},version).error == -EINVAL);
    }
    auto json = intelMemoryJson(decodeIntelMemory({},registers));
    json.replace(json.find("memory-v4"),9,"memory-v1");
    assert(parseIntelMemoryCapture(json).error == -EINVAL);
}
void rankLatencyPresence() {
    for (unsigned type = 0; type < 2; ++type) for (unsigned ranks = 1; ranks < 16; ++ranks) {
        auto v = intel_memory_fixture::registers(type); v[2].raw = ranks;
        // Unoccupied ranks and the LPDDR-only high half may contain any value.
        for (unsigned rank = 0; rank < 4; ++rank) if (!(ranks & (1u<<rank))) v[12].raw &= ~(UINT64_C(255) << (rank*8));
        const auto s = decodeIntelMemory({},v); assert(s.valid);
        for (unsigned rank = 0; rank < 4; ++rank) {
            assert(s.values[30+rank].error == (ranks & (1u<<rank) ? 0 : -ENODEV));
            if (ranks & (1u<<rank)) {
                auto bad = v; bad[12].raw &= ~(UINT64_C(255) << (rank*8));
                const auto invalid = decodeIntelMemory({},bad);
                assert(invalid.error == -ENODATA && invalid.values.empty());
            }
        }
        const auto parsed = parseIntelMemoryCapture(intelMemoryJson(s));
        assert(parsed.valid && parsed.rankMask == ranks && parsed.values.size() == 68);
    }
}
void powerDownFields() {
    for (unsigned type = 0; type < 2; ++type) for (unsigned index = 34; index < 43; ++index) {
        auto regs = intel_memory_fixture::registers(type);
        const auto &field = intelMemoryFields()[index];
        for (auto &r : regs) if (r.offset == field.offset) r.raw &= ~(((UINT64_C(1) << (field.high-field.low+1))-1) << field.low);
        const auto s = decodeIntelMemory({},regs);
        const bool unused = type == 1 && (index == 39 || index == 42);
        assert(s.valid == (unused || index == 42));
        if (s.valid) assert(s.values[index].error == (unused ? -ENOTSUP : 0) && !s.values[index].encoded);
        else assert(s.error == -ENODATA && s.values.empty());
    }
    auto regs = intel_memory_fixture::registers(0);
    // Only reserved bits change, including bits next to the 10-bit tWRPDEN.
    regs[13].raw ^= (UINT64_C(31)<<54) | (UINT64_C(7)<<29);
    const auto s = decodeIntelMemory({},regs); assert(s.valid);
    for (unsigned i = 34; i < 43; ++i) assert(s.values[i].encoded == intel_memory_fixture::expected(i));
}
void extendedUnits() {
    for (unsigned overrides = 0; overrides < 4; ++overrides) {
        auto regs = intel_memory_fixture::registers(0);
        if (overrides & 1) regs[3].raw |= UINT64_C(1)<<30;
        if (overrides & 2) regs[3].raw |= UINT64_C(1)<<16;
        const auto s = decodeIntelMemory({},regs); assert(s.valid);
        const auto &fields = intelMemoryFields();
        for (unsigned i = 0; i < 43; ++i) {
            const std::string expected = i >= 30 && i <= 33 ? "QCLK" : i == 35 || i == 36 ? "MC_DCLK" : overrides ? "encoded" : "tCK";
            assert(intelMemoryUnit(s,fields[i]) == expected);
        }
        auto json = intelMemoryJson(s);
        const auto position = json.find("\"encoded\":40",json.find("\"name\":\"RTL_Rank0\""));
        assert(position != std::string::npos); json.replace(position,12,"\"encoded\":99");
        json.replace(json.find("\"unit\":\"QCLK\""),13,"\"unit\":\"ns\"");
        const auto parsed = parseIntelMemoryCapture(json); assert(parsed.valid && parsed.values[30].encoded == 40);
        assert(std::string(intelMemoryUnit(parsed,fields[30])) == "QCLK");
    }
}
void refreshGoldenAndUnits() {
    const char *units[] = {"1024_DCLK","MC_DCLK","count","flag","flag","tREFI_count","tREFI_count","DCLK","encoded","2*tCK","tCK","tCK","encoded","encoded"};
    for (unsigned type = 0; type < 2; ++type) for (unsigned overrides = 0; overrides < 4; ++overrides) {
        auto regs = intel_memory_fixture::registers(type);
        if (overrides & 1) regs[3].raw |= UINT64_C(1)<<30;
        if (overrides & 2) regs[3].raw |= UINT64_C(1)<<16;
        const auto s = decodeIntelMemory({},regs); assert(s.valid && s.values.size() == 68);
        for (unsigned i = 43; i < 57; ++i) {
            const bool unused = type ? i == 53 || i == 55 : i == 52 || i == 54 || i == 56;
            assert(s.values[i].error == (unused ? -ENOTSUP : 0));
            assert(s.values[i].encoded == intel_memory_fixture::expected(i));
            const std::string expected = overrides && i >= 52 && i <= 54 ? "encoded" : units[i-43];
            assert(std::string(intelMemoryUnit(s,intelMemoryFields()[i])) == expected);
        }
    }
}
void refreshZeroAndBoundaries() {
    // Preserve raw configuration, including disabled flags and zero delays.
    // Existing channel presence / primary timing checks still apply.
    for (unsigned type = 0; type < 2; ++type) {
        auto regs = intel_memory_fixture::registers(type);
        for (unsigned i = 14; i < 18; ++i) regs[i].raw = 0;
        const auto s = decodeIntelMemory({},regs); assert(s.valid);
        for (unsigned i = 43; i < 57; ++i) assert(!s.values[i].encoded);
        regs[4].raw = 0; assert(decodeIntelMemory({},regs).error == -ENODATA);
    }
    // Sweep every representable code independently, including codes outside
    // firmware recommendations. This reader is not a configuration validator.
    for (unsigned i = 43; i < 57; ++i) {
        const auto &f = intelMemoryFields()[i];
        const unsigned maximum = (1u << (f.high-f.low+1))-1;
        const unsigned type = f.dramTypes == 1 ? 0 : 1;
        for (unsigned code = 0; code <= maximum; ++code) {
            auto regs = intel_memory_fixture::registers(type);
            for (auto &r : regs) if (r.offset == f.offset) r.raw = (r.raw & ~(std::uint64_t(maximum)<<f.low)) | (std::uint64_t(code)<<f.low);
            const auto s = decodeIntelMemory({},regs);
            assert(s.valid && !s.values[i].error && s.values[i].encoded == code);
        }
    }
}
void refreshReservedAndAliases() {
    for (unsigned type = 0; type < 2; ++type) {
        auto regs = intel_memory_fixture::registers(type);
        regs[15].raw ^= UINT64_C(0xffffe000);
        regs[16].raw ^= UINT64_C(0xffffe000ffe003ff);
        // Ignore TXSR: the desktop DDR4/5 interpretation is not established.
        regs[17].raw ^= UINT64_C(0xfc0fffffffffffff);
        const auto s = decodeIntelMemory({},regs); assert(s.valid);
        for (unsigned i = 43; i < 57; ++i) assert(s.values[i].encoded == intel_memory_fixture::expected(i));
        const auto json = intelMemoryJson(s); const auto parsed = parseIntelMemoryCapture(json);
        assert(parsed.valid && parsed.version == 4 && parsed.registers.size() == 19);
        for (unsigned i = 14; i < 18; ++i) assert(parsed.registers[i].raw == regs[i].raw);
        // DDR4 does not consume the high TZQCAL field at all.
        if (!type) { regs[16].raw &= ~(UINT64_C(0x1fff)<<32); assert(decodeIntelMemory({},regs).valid); }
    }
}
void allCaptureVersions() {
    for (unsigned version = 1; version <= 4; ++version) for (unsigned type = 0; type < 2; ++type) {
        const auto regs = intel_memory_fixture::registers(type,version);
        const auto s = decodeIntelMemory({},regs,version); assert(s.valid);
        assert(s.values.size() == (version == 1 ? 30u : version == 2 ? 43u : version == 3 ? 57u : 68u));
        const auto json = intelMemoryJson(s);
        const auto parsed = parseIntelMemoryCapture(json);
        assert(parsed.valid && parsed.version == version && intelMemoryJson(parsed) == json);
        for (unsigned other = 1; other <= 5; ++other) if (other != version) {
            assert(decodeIntelMemory({},regs,other).error == -EINVAL);
            auto bad = json; const auto key = "memory-v"+std::to_string(version);
            bad.replace(bad.find(key),key.size(),"memory-v"+std::to_string(other));
            assert(parseIntelMemoryCapture(bad).error == -EINVAL);
        }
    }
}
void ddrioGoldenAndUnits() {
    const char *units[] = {"flag","flag","flag","flag","DCLK","MC_DCLK","8*tCK","MC_DCLK","tCK","encoded","flag"};
    for (unsigned type = 0; type < 2; ++type) for (unsigned overrides = 0; overrides < 4; ++overrides) {
        auto regs = intel_memory_fixture::registers(type);
        if (overrides & 1) regs[3].raw |= UINT64_C(1)<<30;
        if (overrides & 2) regs[3].raw |= UINT64_C(1)<<16;
        const auto s = decodeIntelMemory({},regs);
        assert(s.valid && s.version == 4 && s.registers.size() == 19 && s.values.size() == 68);
        for (unsigned i = 57; i < 68; ++i) {
            assert(!s.values[i].error && s.values[i].encoded == intel_memory_fixture::expected(i));
            const std::string unit = overrides && (i == 63 || i == 65) ? "encoded" : units[i-57];
            assert(intelMemoryUnit(s,intelMemoryFields()[i]) == unit);
        }
        assert(s.values[63].encoded == 21); // 21 stored units of 8*tCK, not 21 tCK or 168 ns.
    }
}
void ddrioBoundaryAndIsolation() {
    const unsigned lows[] = {31,30,29,28,24,20,14,10,5,1,0};
    const unsigned widths[] = {1,1,1,1,4,4,6,4,5,4,1};
    for (unsigned type = 0; type < 2; ++type) {
        auto regs = intel_memory_fixture::registers(type); regs[18].raw = 0;
        const auto zero = decodeIntelMemory({},regs); assert(zero.valid);
        for (unsigned i = 57; i < 68; ++i) assert(!zero.values[i].encoded && !zero.values[i].error);
        // A reader preserves zero latency even where firmware recommends nonzero.
        for (unsigned f = 0; f < 11; ++f) for (unsigned code = 0; code < (1u<<widths[f]); ++code) {
            regs[18].raw = std::uint64_t(code)<<lows[f];
            const auto s = decodeIntelMemory({},regs); assert(s.valid);
            for (unsigned other = 0; other < 11; ++other) assert(s.values[57+other].encoded == (f == other ? code : 0));
        }
        regs[18].raw = UINT32_MAX; assert(decodeIntelMemory({},regs).error == -ENODATA);
        regs[18].raw = UINT64_C(1)<<32; assert(decodeIntelMemory({},regs).error == -EINVAL);
    }
}
void ddrioCaptureAndFailure() {
    const auto original = intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers()));
    auto forged = original;
    const auto p = forged.find("\"encoded\":21",forged.find("DDRIO_self_refresh_latency")); assert(p != std::string::npos);
    forged.replace(p,12,"\"encoded\":99");
    forged.replace(forged.find("8*tCK"),5,"ns");
    const auto parsed = parseIntelMemoryCapture(forged);
    assert(parsed.valid && intelMemoryJson(parsed) == original);
    // The two added reads are last in each pass. Both errors and changes must
    // discard the complete sample, not publish an otherwise valid older table.
    for (int failure : {23,47}) {
        Device d; d.failRead = failure; const auto s = read(d);
        assert(s.error == -EACCES && s.values.empty() && s.registers.empty() && !d.writes);
    }
    Device changing; changing.afterRequest = [&](const Request &) { if (changing.reads == 24) changing.memory[0xe0b8] ^= 1; };
    assert(read(changing).error == -EAGAIN && !changing.writes);
    for (unsigned call : {24u,48u}) {
        Device d; std::atomic<bool> stop{false}; d.afterRequest = [&](const Request &) { if (d.reads == call) stop = true; };
        const auto s = read(d,&stop); assert(s.error == -ECANCELED && s.values.empty() && s.registers.empty() && !d.writes);
    }
}
void refreshRecomputedAndCancelled() {
    auto json = intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers()));
    const auto p = json.find("\"encoded\":66",json.find("\"name\":\"tREFIx9\"")); assert(p != std::string::npos);
    json.replace(p,12,"\"encoded\":99");
    const auto u = json.find("1024_DCLK"); assert(u != std::string::npos); json.replace(u,9,"ns");
    const auto parsed = parseIntelMemoryCapture(json); assert(parsed.valid && parsed.values[43].encoded == 66);
    assert(intelMemoryJson(parsed).find("\"unit\":\"1024_DCLK\"") != std::string::npos);
    for (unsigned call : {19u,20u,21u,22u,23u,42u,43u,44u,45u,46u}) {
        Device d; std::atomic<bool> stop{false}; d.afterRequest = [&](const Request &) { if (d.reads == call) stop = true; };
        const auto s = read(d,&stop);
        assert(s.error == -ECANCELED && s.values.empty() && s.registers.empty() && d.reads == call && !d.writes);
    }
}
}
int main() {
    golden(); routing(); malformedRegisters(); presenceAndType(); zerosAndUnused(); gearEncodings(); roundtrip(); malformedJson();
    liveSuccess(); identityGates(); barGates(); failurePositions(); races(); invalidAndCancelled();
    legacySnapshots(); rankLatencyPresence(); powerDownFields(); extendedUnits();
    refreshGoldenAndUnits(); refreshZeroAndBoundaries(); refreshReservedAndAliases(); allCaptureVersions(); refreshRecomputedAndCancelled();
    ddrioGoldenAndUnits(); ddrioBoundaryAndIsolation(); ddrioCaptureAndFailure();
    std::cout << "26 Intel memory scenario groups passed\n";
}
