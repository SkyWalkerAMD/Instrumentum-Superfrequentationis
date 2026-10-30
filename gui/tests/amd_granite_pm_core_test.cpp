// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pm_native.h"
#include "amd_granite_pm_fixture.h"
#include <cassert>
#include <iostream>
#include <set>
using namespace octool::core;
namespace {
struct Test {
    granite_pm_fixture::Device *d = new granite_pm_fixture::Device;
    HardwareService service{std::unique_ptr<HardwareBackend>(d)};
    AmdPmNativeRead run(const AmdPmOptions &o = {}, const std::atomic<bool> *cancel = nullptr, bool map = false) {
        AmdPmNativeRead r;
        const int e = service.transaction([&](HardwareSession &s) { r = readGranitePmNative(s,130,o,map); return r.error; },10000,cancel);
        if (e) r.error = e;
        return r;
    }
};
}
int main() {
    unsigned groups = 0;
    const unsigned sizes[] = {1828,2452}; const std::uint32_t versions[] = {0x620105,0x620205};
    assert(amdPmLayouts().size() == 8 && amdGranitePmLayouts().size() == 2);
    for (unsigned n = 0; n < 2; ++n) {
        const auto b = granite_pm_fixture::bytes(sizes[n]); const auto s = decodeAmdPm(b,versions[n]);
        assert(!s.error && s.graniteRidge && s.raw == b && s.fields.size() == 20 && s.fields[0].value == 2000);
        const double telemetry[3][4]={{1.25,12.5,15.625,60.5},{1.5,20,30,65.25},{1,2,2,70}};
        const char *units[]={"V","A","W","deg C"};
        for(unsigned channel=0;channel<3;++channel) for(unsigned kind=0;kind<4;++kind) {
            const auto &f=s.fields[8+channel*4+kind];
            assert(f.group=="granite_svi" && f.firmwareChannel==channel && f.offset==4*(49+channel*5+kind));
            assert(f.value==telemetry[channel][kind] && f.unit==units[kind] && !f.clock && !f.scaled);
            assert(f.unitEvidence=="granite-smu-98.84.0-static");
        }
        assert(s.firmwareCcdSlots == n+1 && s.firmwareCores.size() == 144*(n+1) && s.firmwareCcds.size() == 12*(n+1));
        assert(s.graniteRaw.size() == sizes[n]/4 && s.graniteRaw.back().value == 100);
        assert(s.originalLimits.empty() && s.originalArrays.empty() && s.originalPackage.empty() && s.originalFabric.empty());
        std::set<unsigned> offsets;
        for (const auto &f : s.firmwareCores) {
            const bool known=f.row==1 || f.row==2 || (f.row>=5 && f.row<=7);
            assert(f.unit==(f.row==1?"V":f.row==2?"deg C":known?"%":"unverified") && !f.scaled && !f.clock);
            assert(f.unitEvidence.empty()!=known); offsets.insert(f.offset);
        }
        for (const auto &f : s.firmwareCcds) {
            const bool known=f.row==3 || f.row==4 || f.row==11;
            assert(f.unit == (f.row==11?"ratio":known?"deg C":"unverified") && !f.scaled && !f.clock);
            assert(f.unitEvidence.empty()!=known); assert(offsets.insert(f.offset).second);
        }
        assert(offsets.size() == 156*(n+1) && *offsets.begin() == 1204 && *offsets.rbegin() == sizes[n]-4);
        const auto json = amdPmJson(s); assert(amdPmJson(parseAmdPmCapture(json)) == json);
        assert(auditLegacyAmdPmLayout(versions[n],sizes[n],n+1).error == -ENOTSUP);
        auto forged = json; const auto pos = forged.find("\"stored_value\":2000"); assert(pos != std::string::npos);
        forged.replace(pos,19,"\"stored_value\":9999"); assert(amdPmJson(parseAmdPmCapture(forged)) == json);
        forged=json; const auto channel=forged.find("\"firmware_channel\":1"); assert(channel!=std::string::npos);
        forged.replace(channel,20,"\"firmware_channel\":99");
        assert(amdPmJson(parseAmdPmCapture(forged))==json);
        assert(json.find("\"freshness_verified\":false")!=std::string::npos);
        assert(json.find("\"setting_readback_verified\":false")!=std::string::npos);
    } ++groups;
    for(unsigned width=1;width<=2;++width) {
        auto b=granite_pm_fixture::bytes(sizes[width-1]);
        for(unsigned core=0;core<8*width;++core) {
            pm_fixture::put(b,0x4b4+4*(8*width+core),0x3fa00000); // 1.25 V model, not VID
            pm_fixture::put(b,0x4b4+4*(2*8*width+core),0x42928000); // 73.25 deg C
            pm_fixture::put(b,0x4b4+4*(5*8*width+core),0x41c80000); // 25 percent already
        }
        for(unsigned slot=0;slot<width;++slot) {
            pm_fixture::put(b,0x4b4+4*(144*width+3*width+slot),0x42750000); // 61.25 C
            pm_fixture::put(b,0x4b4+4*(144*width+4*width+slot),0x42910000); // 72.5 C
            pm_fixture::put(b,0x4b4+4*(144*width+11*width+slot),0x3e800000); // .25 ratio
        }
        const auto s=decodeAmdPm(b,versions[width-1]);
        for(unsigned core=0;core<8*width;++core) {
            assert(s.firmwareCores[8*width+core].value==1.25);
            assert(s.firmwareCores[2*8*width+core].value==73.25);
            assert(s.firmwareCores[5*8*width+core].value==25);
        }
        for(unsigned slot=0;slot<width;++slot) {
            assert(s.firmwareCcds[3*width+slot].value==61.25);
            assert(s.firmwareCcds[4*width+slot].value==72.5);
            assert(s.firmwareCcds[11*width+slot].value==0.25);
        }
        auto forged=amdPmJson(s); const auto p=forged.find("\"unit\":\"deg C\""); assert(p!=std::string::npos);
        forged.replace(p,14,"\"unit\":\"W\"");
        assert(amdPmJson(parseAmdPmCapture(forged))==amdPmJson(s));
        forged=amdPmJson(s); const auto ratio=forged.find("\"unit\":\"ratio\""); assert(ratio!=std::string::npos);
        forged.replace(ratio,14,"\"unit\":\"deg C\"");
        assert(amdPmJson(parseAmdPmCapture(forged))==amdPmJson(s));
    } ++groups;
    for (unsigned n = 0; n < 2; ++n) for (unsigned length = 0; length <= sizes[n]+4; ++length) {
        if (length == sizes[n]) continue;
        const auto s = decodeAmdPm(std::vector<std::uint8_t>(length),versions[n]);
        assert(s.error == -EMSGSIZE && s.fields.empty() && s.graniteRaw.empty());
    } ++groups;
    for (std::uint32_t v : {0x620305u,0x621201u,0x621101u,0x620204u}) assert(!amdPmTableSize(v));
    AmdPmOptions scale; scale.hasBclk = true; scale.bclkMHz = 100;
    assert(decodeAmdPm(granite_pm_fixture::bytes(),0x620205,scale).error == -ENOTSUP);
    ++groups;
    for (unsigned off : {0u,196u,200u,204u,208u,216u,220u,224u,228u,236u,240u,244u,248u,284u,1204u,1268u,2352u,2380u,2388u,2448u}) for (std::uint32_t bits : {0x7f800001u,0x7fc00000u,0x7f800000u,0xff800000u}) {
        auto b = granite_pm_fixture::bytes(); pm_fixture::put(b,off,bits); const auto s = decodeAmdPm(b,0x620205);
        assert(s.error == -EDOM && s.raw == b && s.graniteRaw[off/4].raw == bits && s.graniteRaw[off/4].error == -EDOM);
        assert(parseAmdPmCapture(amdPmJson(s)).raw == b);
        for(const auto &f:s.fields) if(f.offset==off) assert(f.error==-EDOM && f.raw==bits);
    } ++groups;
    for (unsigned n = 0; n < 2; ++n) {
        Test t; t.d->version = versions[n]; t.d->size = sizes[n]; t.d->bytes = granite_pm_fixture::bytes(sizes[n]);
        const auto r = t.run();
        assert(!r.error && r.graniteRidge && r.smuVersion == 0x625400 && r.metadataRechecked && r.transferAccepted);
        assert(r.snapshot.raw == t.d->bytes && r.bytesRead == sizes[n] && !t.d->wrongCpu);
        assert(t.d->messages == std::vector<std::uint32_t>({2,5,4,3,5,4,2}));
        for (const auto &packet : t.d->packets) for (auto arg : packet) assert(!arg);
        for (const auto &request : t.d->requests) if (request.space == Space::Memory) assert(!request.write);
        assert(!r.mappingRequested && !r.mappingValid && r.firmwareCcdIds.empty());
        assert(amdPmNativeJson(r).find("\"platform\":\"granite-ridge\"") != std::string::npos);
    } ++groups;
    for (unsigned mode = 0; mode < 9; ++mode) {
        Test t;
        if (mode == 0) t.d->amd = false;
        if (mode == 1) t.d->family = 0x19;
        if (mode == 2) t.d->model = 8;
        if (mode == 3) t.d->pciId = 0x153a1022;
        if (mode == 4) t.d->hypervisor = true;
        if (mode == 5) t.d->physicalBits = 31;
        if (mode == 6) t.d->physicalBits = 53;
        const auto r = t.run(mode == 7 ? scale : AmdPmOptions());
        if (mode == 8) { assert(!r.error); continue; }
        assert(r.error && t.d->messages.empty() && !t.d->memoryReads && r.snapshot.raw.empty());
    } ++groups;
    for (unsigned mode = 0; mode < 9; ++mode) {
        Test t;
        if (mode == 0) t.d->smuVersion = 0x625300;
        if (mode == 1) t.d->version = 0x730204;
        if (mode == 2) t.d->size = 1828;
        if (mode == 3) t.d->size = 0;
        if (mode == 4) t.d->base = 0;
        if (mode == 5) t.d->base = 0x123456001ull;
        if (mode == 6) t.d->base = 0xfffffffffffcull;
        if (mode == 7) t.d->base = UINT64_MAX;
        if (mode == 8) t.d->physicalBits = 32;
        const auto r = t.run(); assert(r.error && !r.transferAttempted && !t.d->memoryReads && r.snapshot.raw.empty());
    } ++groups;
    for (unsigned mode = 0; mode < 5; ++mode) {
        Test t;
        if (mode == 0) t.d->versionAfter = 0x620105;
        if (mode == 1) t.d->sizeAfter = 1828;
        if (mode == 2) t.d->baseAfter = 0x123457000ull;
        if (mode == 3) t.d->smuVersionAfter = 0x625300;
        if (mode == 4) t.d->afterRequest = [&] { if (t.d->memoryReads) t.d->model = 8; };
        const auto r = t.run(); assert(r.error && !r.metadataRechecked && r.snapshot.raw.empty() && r.snapshot.fields.empty());
    } ++groups;
    for (unsigned command = 0; command < 5; ++command) {
        Test t; t.d->completions[command] = 0xfe; const auto r = t.run();
        assert(r.error == -EIO && r.snapshot.raw.empty());
    } ++groups;
    Test good; assert(!good.run().error);
    std::set<unsigned> faults{0,1,2,good.d->memoryCalls.front(),good.d->memoryCalls[300],good.d->memoryCalls.back(),good.d->calls-1};
    // Each mailbox phase and both sides of a 32-bit index/data transfer.
    for (std::size_t n = 0; n < good.d->pciCalls.size(); n += 30) faults.insert(good.d->pciCalls[n]);
    for (auto fault : faults) { Test t; t.d->failAt = int(fault); const auto r = t.run(); assert(r.error && r.snapshot.raw.empty()); }
    ++groups;
    { Test t; std::atomic<bool> cancel{false}; t.d->afterRequest = [&] { if (t.d->memoryReads == 100) cancel.store(true); };
      const auto r = t.run({},&cancel); assert(r.error == -ECANCELED && r.snapshot.raw.empty() && !r.metadataRechecked); }
    ++groups;
    { Test t; pm_fixture::put(t.d->bytes,2448,0x7f800001); const auto r = t.run();
      assert(r.error == -EDOM && r.metadataRechecked && r.snapshot.raw == t.d->bytes); }
    ++groups;
    for (unsigned partial=0;partial<2;++partial) {
        Test t; if(partial) pm_fixture::put(t.d->bytes,2448,0x7f800001);
        const auto r=t.run({},nullptr,true);
        assert(r.error==(partial?-EDOM:0) && r.mappingRequested && r.mappingValid && r.mappingRechecked);
        assert(r.firmwareCcdIds==std::vector<unsigned>({0,1}) && r.metadataRechecked);
        assert(!r.queriedBefore && !r.queriedAfter && !t.d->presenceQueries);
        assert(t.d->messages==std::vector<std::uint32_t>({2,5,4,3,5,4,2}));
        const auto json=amdPmNativeJson(r);
        assert(json.find("\"source\":\"granite-two-slot-initializer\"")!=std::string::npos);
        assert(json.find("\"inferred_present_mask\":\"0x0003\"")!=std::string::npos);
        assert(json.find("\"physical_mapping_verified\":false")!=std::string::npos);
        assert(r.snapshot.raw==t.d->bytes);
    } ++groups;
    for(unsigned mode=0;mode<6;++mode) {
        Test t; std::atomic<bool> cancel{false};
        if(mode==0) { t.d->version=0x620105; t.d->size=1828; t.d->bytes=granite_pm_fixture::bytes(1828); }
        if(mode==1) t.d->versionAfter=0x620105;
        if(mode==2) t.d->smuVersionAfter=0x625300;
        if(mode==3) t.d->sizeAfter=1828;
        if(mode==4) t.d->failAt=int(good.d->calls-1);
        if(mode==5) t.d->afterRequest=[&] { if(t.d->memoryReads==100) cancel.store(true); };
        const auto r=t.run({},&cancel,true);
        assert(r.error && r.mappingRequested && !r.mappingValid && !r.mappingRechecked && r.firmwareCcdIds.empty());
        assert(r.snapshot.raw.empty() && !r.metadataRechecked && !t.d->presenceQueries);
        if(!mode) assert(!r.transferAttempted && !t.d->memoryReads && r.phase==AmdPmNativePhase::Topology);
    } ++groups;
    assert(groups == 15);
    std::cout << groups << " AMD Granite Ridge PM scenario groups passed\n";
}
