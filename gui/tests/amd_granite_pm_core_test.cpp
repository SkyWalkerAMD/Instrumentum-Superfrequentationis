// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pm_native.h"
#include "amd_granite_pm_fixture.h"
#include <cassert>
#include <iostream>
#include <set>
#include <utility>
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
        assert(!s.error && s.graniteRidge && s.raw == b && s.fields.size() == 41 && s.fields[0].value == 2000);
        const auto &request=s.fields.back();
        assert(request.group=="granite_shared_request" && request.unit=="V" && request.offset==76);
        assert(request.value==1.5 && request.unitEvidence=="granite-smu-98.84.0-static");
        assert(!request.producerNote.empty() && !request.clock && !request.scaled);
        const double telemetry[3][4]={{1.25,12.5,15.625,60.5},{1.5,20,30,65.25},{1,2,2,70}};
        const char *units[]={"V","A","W","deg C"};
        for(unsigned channel=0;channel<3;++channel) for(unsigned kind=0;kind<4;++kind) {
            const auto &f=s.fields[8+channel*5+kind];
            assert(f.group=="granite_svi" && f.firmwareChannel==channel && f.offset==4*(49+channel*5+kind));
            assert(f.value==telemetry[channel][kind] && f.unit==units[kind] && !f.clock && !f.scaled);
            assert(f.unitEvidence=="granite-smu-98.84.0-static");
        }
        const double mirrors[]={1.125,.875,.625};
        std::set<unsigned> commonOffsets;
        for(const auto &f:s.fields) assert(commonOffsets.insert(f.offset).second);
        for(unsigned channel=0;channel<3;++channel) {
            const auto &f=s.fields[7+channel*5];
            assert(f.group=="granite_command_mirror" && f.firmwareChannel==channel);
            assert(f.value==mirrors[channel] && f.unit=="V" && f.offset==4*(48+channel*5));
            assert(f.unitEvidence=="granite-smu-98.84.0-static" && !f.clock && !f.scaled);
        }
        assert(s.firmwareCcdSlots == n+1 && s.firmwareCores.size() == 144*(n+1) && s.firmwareCcds.size() == 12*(n+1));
        assert(s.graniteRaw.size() == sizes[n]/4 && s.graniteRaw.back().value == 100);
        assert(s.originalLimits.empty() && s.originalArrays.empty() && s.originalPackage.empty() && s.originalFabric.empty());
        std::set<unsigned> offsets;
        for (const auto &f : s.firmwareCores) {
            const bool known=(f.row>=1 && f.row<=3) || (f.row>=5 && f.row<=11) || f.row>=13;
            assert(f.unit==(f.row==1?"V":f.row==2?"deg C":f.row==3 || f.row==9 || f.row==10?"GHz":f.row==13 || f.row==14?"ratio":f.row==8 || f.row==11?"code":known?"%":"unverified") && !f.scaled && !f.clock);
            assert(f.unitEvidence.empty()!=known); offsets.insert(f.offset);
            assert(f.producerEvidence.empty()==known);
            if(!known) {
                assert(f.producerEvidence=="granite-mp5-98.84.0-static" && !f.producerNote.empty());
                assert(f.producerKind==(f.row==0?"model":"counter_rate"));
                assert(f.producerSourceAddress==0x11a18+0x1bc*(f.slot%8)+(f.row==0?0x10:f.row==4?0xf4:0xf8));
                if(f.row==0) assert(f.counterSourceAddresses.empty());
                else assert(f.counterSourceAddresses==std::vector<std::uint32_t>{0x1600000+0x20000*(f.slot%8)+(f.row==4?0x134u:0x13cu)});
            } else {
                assert(!f.producerSourceAddress && f.counterSourceAddresses.empty());
            }
            if(f.row==8 || f.row==11) assert(f.value==(f.row==8?3.5:127.5));
            if(f.row==9 || f.row==10) assert(f.value==(f.row==9?5.5:6.25));
            if(f.row==3) assert(f.value==4.375);
            const double counters[]={.625,.125,25,75,150};
            if(f.row>=13) assert(f.value==counters[f.row-13]);
        }
        for (const auto &f : s.firmwareCcds) {
            assert(f.alternativeConfigurationInputs == (f.row == 2));
            const bool known=(f.row>=3 && f.row<=5) || f.row==7 || f.row>=9;
            assert(f.unit == (f.row==9 || f.row==11?"%":f.row==5 || f.row==10?"GHz":f.row==7?"code":known?"deg C":"unverified") && !f.scaled && !f.clock);
            assert(f.unitEvidence.empty()!=known); assert(offsets.insert(f.offset).second);
            assert(f.producerEvidence.empty()==known);
            if(!known) {
                assert(f.producerKind==(f.row<=1?"model":f.row==2?"configuration":"counter_rate") && !f.producerNote.empty());
                const std::uint32_t addresses[]={0x12804,0x12848,0x8030,0,0,0,0x128a8,0,0x12888};
                assert(f.producerSourceAddress==addresses[f.row]);
                const std::vector<std::uint32_t> counters=f.row==6?std::vector<std::uint32_t>{0x187f134}:
                    f.row==8?std::vector<std::uint32_t>{0x187f170,0x187f174,0x187f17c}:std::vector<std::uint32_t>{};
                assert(f.counterSourceAddresses==counters);
            } else assert(!f.producerSourceAddress && f.counterSourceAddresses.empty());
            if(f.row==7) assert(f.value==32767.5);
            if(f.row==5 || f.row==9 || f.row==10) assert(f.value==(f.row==5?6:f.row==9?150.125:5.75));
        }
        assert(offsets.size() == 156*(n+1) && *offsets.begin() == 1204 && *offsets.rbegin() == sizes[n]-4);
        const auto json = amdPmJson(s); assert(amdPmJson(parseAmdPmCapture(json)) == json);
        assert(auditLegacyAmdPmLayout(versions[n],sizes[n],n+1).error == -ENOTSUP);
        for(const auto &entry:std::vector<std::pair<std::string,std::string>>{
            {"\"software_command_mirror\":true","\"software_command_mirror\":false"},
            {"\"startup_query_packet_sent\":false","\"startup_query_packet_sent\":true"},
            {"\"startup_completion_checked\":false","\"startup_completion_checked\":true"},
            {"\"independent_runtime_device_getter_verified\":false","\"independent_runtime_device_getter_verified\":true"},
            {"\"smt_target_stride\":32","\"smt_target_stride\":16"},
            {"\"runtime_masks_required\":true","\"runtime_masks_required\":false"},
            {"\"command_indexes_are_pm_slots\":false","\"command_indexes_are_pm_slots\":true"},
            {"\"runtime_mapping_verified\":false","\"runtime_mapping_verified\":true"},
            {"\"software_request_mirror\":true","\"software_request_mirror\":false"},
            {"\"firmware_consumer_channel\":0","\"firmware_consumer_channel\":1"},
            {"\"reference_address_space\":\"smu_local\"","\"reference_address_space\":\"host_physical\""},
            {"\"measurement\":false","\"measurement\":true"},
            {"\"policy_state\":0","\"policy_state\":99"},
            {"\"clock_policy_kind\":0","\"clock_policy_kind\":99"},
            {"\"nominal_reference_mhz\":100","\"nominal_reference_mhz\":200"},
            {"\"sample_share\":true","\"sample_share\":false"},
            {"\"time_residency\":false","\"time_residency\":true"},
            {"\"selected_state_verified\":false","\"selected_state_verified\":true"},
            {"\"physical_clock_identity_verified\":false","\"physical_clock_identity_verified\":true"},
            {"\"zero_override_retains_policy\":true","\"zero_override_retains_policy\":false"},
            {"\"measured_frequency\":false","\"measured_frequency\":true"},
            {"\"request_policy_value\":true","\"request_policy_value\":false"},
            {"\"frequency_model\":true","\"frequency_model\":false"},
            {"\"fused_frequency_policy\":true","\"fused_frequency_policy\":false"},
            {"\"physical_unit_verified\":false","\"physical_unit_verified\":true"},
            {"\"physical_measurement_verified\":false","\"physical_measurement_verified\":true"},
            {"\"producer_kind\":\"model\"","\"producer_kind\":\"sensor\""},
            {"\"producer_evidence\":\"granite-mp5-98.84.0-static\"","\"producer_evidence\":\"hardware-tested\""},
            {"\"producer_source_address\":\"0x00011a28\"","\"producer_source_address\":\"0xffffffff\""},
            {"\"counter_source_addresses\":[\"0x01600134\"]","\"counter_source_addresses\":[\"0xffffffff\"]"},
            {"\"reference_address_space\":\"mp5_local\"","\"reference_address_space\":\"host_physical\""},
            {"\"reference_addresses_are_host_addresses\":false","\"reference_addresses_are_host_addresses\":true"},
            {"\"input_role\":\"threshold_alternatives\"","\"input_role\":\"physical_rails\""},
            {"\"target_selection\":\"present_firmware_targets\"","\"target_selection\":\"os_cpu\""},
            {"\"independent_rail_addressing\":false","\"independent_rail_addressing\":true"},
            {"\"changed_code_can_retain_previous_value\":true","\"changed_code_can_retain_previous_value\":false"},
            {"\"local_write_completion_polled\":false","\"local_write_completion_polled\":true"},
            {"\"local_write_returns_device_payload\":false","\"local_write_returns_device_payload\":true"},
            {"\"raw_read_host_route_verified\":false","\"raw_read_host_route_verified\":true"},
            {"\"device_readback\":false","\"device_readback\":true"},
            {"\"runtime_write_enabled\":false","\"runtime_write_enabled\":true"},
            {"\"code_meaning_verified\":false","\"code_meaning_verified\":true"},
            {"\"firmware_ccd_storage_slot\":0","\"firmware_ccd_storage_slot\":99"},
            {"\"firmware_local_core_slot\":0","\"firmware_local_core_slot\":99"}}) {
            auto forged=json; const auto p=forged.find(entry.first); assert(p!=std::string::npos);
            forged.replace(p,entry.first.size(),entry.second);
            assert(amdPmJson(parseAmdPmCapture(forged))==json);
        }
        auto forged = json; const auto pos = forged.find("\"stored_value\":2000"); assert(pos != std::string::npos);
        forged.replace(pos,19,"\"stored_value\":9999"); assert(amdPmJson(parseAmdPmCapture(forged)) == json);
        forged=json; const auto channel=forged.find("\"firmware_channel\":1"); assert(channel!=std::string::npos);
        forged.replace(channel,20,"\"firmware_channel\":99");
        assert(amdPmJson(parseAmdPmCapture(forged))==json);
        assert(json.find("\"freshness_verified\":false")!=std::string::npos);
        assert(json.find("\"setting_readback_verified\":false")!=std::string::npos);
    } ++groups;
    // Two fixed firmware-target positions exist even in a one-slot PM table.
    // They must not inherit compact slot IDs or claim a measured voltage.
    for(unsigned width=1;width<=2;++width) {
        const auto s=decodeAmdPm(granite_pm_fixture::bytes(sizes[width-1]),versions[width-1]);
        unsigned count=0;const double expected[]={.75,1.125,.875,1.25};
        for(const auto &f:s.fields) if(f.group=="granite_target_config" || f.group=="granite_local_config") {
            ++count;const bool local=f.offset==269*4;const unsigned index=f.offset/4-259;
            assert(f.unit=="unverified" && f.unitEvidence.empty() && !f.slotCount && !f.clock && !f.scaled);
            assert(f.producerEvidence=="granite-smu-98.84.0-static" && f.producerKind=="configuration");
            assert(f.value==(local?1:expected[index]) && !f.producerNote.empty());
            assert(f.producerSourceAddress==(local?0xc27c:0xa55c+8*(index%2)+16*(index/2)));
            if(!local) assert(f.slot==index%2 && f.row==index/2);
        }
        assert(count==5);const auto json=amdPmJson(s);
        for(const auto &entry:std::vector<std::pair<std::string,std::string>>{
            {"\"firmware_target\":1","\"firmware_target\":0"},
            {"\"configuration_kind\":\"B\"","\"configuration_kind\":\"voltage\""},
            {"\"target_present_verified\":false","\"target_present_verified\":true"},
            {"\"absent_target_can_retain_value\":true","\"absent_target_can_retain_value\":false"},
            {"\"target_is_compact_pm_slot\":false","\"target_is_compact_pm_slot\":true"},
            {"\"step_setter_reads_device\":false","\"step_setter_reads_device\":true"}}) {
            auto forged=json;const auto p=forged.find(entry.first);assert(p!=std::string::npos);
            forged.replace(p,entry.first.size(),entry.second);assert(amdPmJson(parseAmdPmCapture(forged))==json);
        }
    } ++groups;
    for(unsigned width=1;width<=2;++width) for(std::uint32_t bits:
            {0x80000000u,1u,0xbf800000u,0x43160000u,0x7f800000u,0x7f800001u,0xffc12345u}) {
        auto b=granite_pm_fixture::bytes(sizes[width-1]);
        for(unsigned index:{259u,260u,261u,262u,269u}) pm_fixture::put(b,4*index,bits);
        const auto s=decodeAmdPm(b,versions[width-1]);
        for(const auto &f:s.fields) if(f.group=="granite_target_config" || f.group=="granite_local_config") {
            assert(f.raw==bits && f.unit=="unverified" && f.unitEvidence.empty() && !f.scaled);
            assert(bool(f.error)==((bits&0x7f800000u)==0x7f800000u));
        }
        assert(s.fields[7].value==1.125 && s.fields.back().value==1.5);
        const auto json=amdPmJson(s);assert(amdPmJson(parseAmdPmCapture(json))==json);
    } ++groups;
    // Every policy state is exported, including the zero-share state. Preserve
    // the already converted values, without BCLK scaling or physical IDs.
    for(unsigned width=1;width<=2;++width) {
        const auto s=decodeAmdPm(granite_pm_fixture::bytes(sizes[width-1]),versions[width-1]);
        const double expected[5][4]={{25,0,50,25},{2000,1800,1600,1400},
            {1800,1600,1200,1000},{3600,1600,2400,1000},{1,1.125,1.25,1.375}};
        std::set<unsigned> offsets;
        for(const auto &f:s.fields) if(f.offset>=67*4 && f.offset<87*4) {
            assert(offsets.insert(f.offset).second);
            const unsigned kind=(f.offset/4-67)/4,state=(f.offset/4-67)%4;
            assert(f.value==expected[kind][state] && f.slot==state && !f.slotCount);
            assert(f.unit==(kind==0?"%":kind==4?"V":"MHz") && !f.clock && !f.scaled);
            assert(f.group==(kind==0?"granite_dpm_share":kind==4?"granite_dpm_voltage":"granite_dpm_clock"));
            assert(f.unitEvidence=="granite-smu-98.84.0-static" && !f.producerNote.empty());
        }
        assert(offsets.size()==20);
    } ++groups;
    // A capture may contain exceptional or out-of-range values. Decode raw
    // words independently; never clamp a share or apply a voltage conversion.
    for(unsigned width=1;width<=2;++width) for(std::uint32_t bits:
            {0x80000000u,1u,0xbf800000u,0x43160000u,0x7f800000u,0x7f800001u,0xffc12345u}) {
        auto b=granite_pm_fixture::bytes(sizes[width-1]);
        for(unsigned index=67;index<87;++index) pm_fixture::put(b,4*index,bits);
        const auto s=decodeAmdPm(b,versions[width-1]);
        for(const auto &f:s.fields) if(f.offset>=67*4 && f.offset<87*4) {
            assert(f.raw==bits && !f.scaled && !f.clock);
            assert(bool(f.error)==((bits&0x7f800000u)==0x7f800000u));
        }
        assert(s.fields[7].value==1.125 && s.fields.back().value==1.5);
        const auto json=amdPmJson(s);assert(amdPmJson(parseAmdPmCapture(json))==json);
    } ++groups;
    // Preserve requests independently of channel command/telemetry values,
    // including negative zero, subnormals and invalid encodings. No conversion
    // from the already exported volt value back into a hardware VID is applied.
    for(unsigned width=1;width<=2;++width) for(std::uint32_t bits:
            {0x80000000u,1u,0xbf800000u,0x40700000u,0x7f800000u,0x7f800001u,0xffc12345u}) {
        auto b=granite_pm_fixture::bytes(sizes[width-1]); pm_fixture::put(b,76,bits);
        const auto s=decodeAmdPm(b,versions[width-1]); const auto &f=s.fields.back();
        assert(f.raw==bits && f.unit=="V" && f.group=="granite_shared_request");
        assert(bool(f.error)==((bits&0x7f800000u)==0x7f800000u));
        assert(!f.scaled && !f.clock && s.fields[7].value==1.125 && s.fields[8].value==1.25);
        const auto json=amdPmJson(s); assert(amdPmJson(parseAmdPmCapture(json))==json);
        assert(json.find("\"software_request_mirror\":true,\"measurement\":false")!=std::string::npos);
    } ++groups;
    // Producer evidence never grants a unit, rescales a raw word or trusts an
    // imported producer note. Cover each new row in both exact layouts.
    for(unsigned width=1;width<=2;++width) for(unsigned group=0;group<2;++group) {
        const std::vector<unsigned> rows=group?std::vector<unsigned>{0,1,2,6,8}:std::vector<unsigned>{0,4,12};
        for(unsigned row:rows) for(std::uint32_t bits:{0x80000000u,0x00000001u,0xc2480000u,0x7f800000u,0x7f800001u}) {
            auto bytes=granite_pm_fixture::bytes(sizes[width-1]);
            const unsigned offset=0x4b4+4*(group?144*width+row*width:row*8*width);
            pm_fixture::put(bytes,offset,bits);const auto s=decodeAmdPm(bytes,versions[width-1]);
            const auto &f=group?s.firmwareCcds[row*width]:s.firmwareCores[row*8*width];
            assert(f.raw==bits && !f.producerEvidence.empty() && f.unitEvidence.empty());
            assert(f.unit=="unverified" && !f.scaled && !f.clock);
            assert(bool(f.error)==((bits&0x7f800000u)==0x7f800000u));
            const auto json=amdPmJson(s);auto forged=json;
            const std::string key="\"producer_note\":";const auto p=forged.find(key);
            assert(p!=std::string::npos);const auto end=forged.find('\"',p+key.size()+1);
            assert(end!=std::string::npos);forged.replace(p+key.size(),end-(p+key.size())+1,"\"measured GHz\"");
            assert(amdPmJson(parseAmdPmCapture(forged))==json);
        }
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
            pm_fixture::put(b,0x4b4+4*(144*width+11*width+slot),0x41c80000); // 25 percent, already scaled
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
            assert(s.firmwareCcds[11*width+slot].value==25);
        }
        auto forged=amdPmJson(s); const auto p=forged.find("\"unit\":\"deg C\""); assert(p!=std::string::npos);
        forged.replace(p,14,"\"unit\":\"W\"");
        assert(amdPmJson(parseAmdPmCapture(forged))==amdPmJson(s));
        forged=amdPmJson(s); const auto ratio=forged.find("\"unit\":\"ratio\""); assert(ratio!=std::string::npos);
        forged.replace(ratio,14,"\"unit\":\"deg C\"");
        assert(amdPmJson(parseAmdPmCapture(forged))==amdPmJson(s));
        // Old captures with the incorrect CCD ratio label must be reinterpreted
        // from raw bytes without changing the stored value or inventing trust.
        forged=amdPmJson(s); const auto ccd=forged.find("\"firmware_ccd_arrays\"");
        const auto percent=forged.find("\"unit\":\"%\"",ccd); assert(percent!=std::string::npos);
        forged.replace(percent,10,"\"unit\":\"ratio\"");
        const auto meaning=forged.find("\"counter_meaning_verified\":false"); assert(meaning!=std::string::npos);
        forged.replace(meaning,32,"\"counter_meaning_verified\":true");
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
    for (unsigned off : {0u,192u,212u,232u,1716u,1908u,2412u,196u,200u,204u,208u,216u,220u,224u,228u,236u,240u,244u,248u,284u,1204u,1268u,1396u,2036u,2100u,2164u,2228u,2292u,2352u,2380u,2388u,2396u,2428u,2436u,2448u}) for (std::uint32_t bits : {0x7f800001u,0x7fc00000u,0x7f800000u,0xff800000u}) {
        auto b = granite_pm_fixture::bytes(); pm_fixture::put(b,off,bits); const auto s = decodeAmdPm(b,0x620205);
        assert(s.error == -EDOM && s.raw == b && s.graniteRaw[off/4].raw == bits && s.graniteRaw[off/4].error == -EDOM);
        assert(parseAmdPmCapture(amdPmJson(s)).raw == b);
        for(const auto &f:s.fields) if(f.offset==off) assert(f.error==-EDOM && f.raw==bits);
        for(const auto &f:s.firmwareCores) if(f.offset==off) assert(f.error==-EDOM && f.raw==bits);
        for(const auto &f:s.firmwareCcds) if(f.offset==off) assert(f.error==-EDOM && f.raw==bits);
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
    assert(groups == 21);
    std::cout << groups << " AMD Granite Ridge PM scenario groups passed\n";
}
