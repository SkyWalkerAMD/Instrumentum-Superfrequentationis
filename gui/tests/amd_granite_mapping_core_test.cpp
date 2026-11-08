// SPDX-License-Identifier: GPL-2.0-only
#include "amd_granite_mapping.h"
#include "bounded_json.h"
#include "amd_granite_mapping_vectors.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <iostream>
#include <locale>
using namespace octool::core;
namespace {
AmdGraniteMappingInput input(std::uint32_t version,std::uint32_t presence,
                            std::uint32_t first,std::uint32_t second,bool smt=true) {
    AmdGraniteMappingInput i;
    i.tableVersion=version; i.presenceMask=presence; i.coreMasks={{first,second}}; i.smt=smt;
    return i;
}
unsigned populated(const AmdGraniteMapping &m) {
    return unsigned(std::count_if(m.commandRemap.begin(),m.commandRemap.end(),
        [](std::uint16_t v) { return v!=0xffff; }));
}
void referenceByteOracles() {
    // All 64 words, including holes, come from the firmware instruction replay,
    // not a copy of the C++ algorithm. Includes high-byte-only active targets.
    for(const auto &v:amd_granite_mapping_vectors::values) {
        const auto m=diagnoseAmdGraniteMapping(input(v.version,v.presence,v.masks[0],v.masks[1],v.smt));
        assert(!m.error && m.coreSlots.size()==m.pmCcdSlots*8);
        for(unsigned j=0;j<64;++j) assert(m.commandRemap[j]==v.remap[j]);
        assert(populated(m)==m.enabledCores*m.threadsPerCore);
    }
}
void invalidInputsHaveNoMappings() {
    for(const auto &i:{input(0,3,255,255),input(0x620305,3,255,255),
                      input(0x620205,1,255,255),input(0x620105,3,255,255),
                      input(0x620205,0,255,255),input(0x620205,7,255,255),
                      input(0x620105,0x80000001,255,255)}) {
        const auto m=diagnoseAmdGraniteMapping(i);
        assert(m.error && !m.detail.empty() && m.targets.empty() && m.coreSlots.empty());
        assert(!populated(m) && !m.pmCcdSlots && !m.enabledCores && !m.activeTargets);
        assert(m.input.tableVersion==i.tableVersion && m.input.presenceMask==i.presenceMask);
    }
    assert(diagnoseAmdGraniteMapping(input(0,3,255,255)).error==-ENOTSUP);
    assert(diagnoseAmdGraniteMapping(input(0x620205,1,255,255)).error==-EINVAL);
}
void sparseTargetKeepsPmPositions() {
    const auto m=diagnoseAmdGraniteMapping(input(0x620105,2,0xffffffff,0x81));
    assert(!m.error && m.pmCcdSlots==1 && m.activeTargets==1 && m.enabledCores==2);
    assert(!m.targets[0].present && m.targets[0].pmCcdSlot==-1 && m.targets[0].commandTargetRank==-1);
    assert(m.targets[0].rawCoreMask==0xffffffff && !m.targets[0].storedCoreMask);
    assert(m.targets[1].pmCcdSlot==0 && m.targets[1].commandTargetRank==0);
    assert(m.coreSlots.size()==8 && m.coreSlots[0].firmwareTarget==1 && m.coreSlots[7].pmCoreSlot==7);
    assert(m.coreSlots[0].commandIndexes==std::vector<unsigned>({0,1}));
    assert(m.coreSlots[0].encodedCoordinates==std::vector<unsigned>({32,33}));
    assert(m.coreSlots[7].commandIndexes==std::vector<unsigned>({2,3}));
    assert(m.coreSlots[7].encodedCoordinates==std::vector<unsigned>({46,47}));
    for(unsigned c=1;c<7;++c) assert(!m.coreSlots[c].enabled && m.coreSlots[c].commandIndexes.empty() && m.coreSlots[c].commandCoreRank==-1);
}
void emptyTargetsRemainPmSlots() {
    const auto m=diagnoseAmdGraniteMapping(input(0x620205,3,0,0xff));
    assert(!m.error && m.pmCcdSlots==2 && m.activeTargets==1 && m.coreSlots.size()==16);
    assert(m.targets[0].present && m.targets[0].pmCcdSlot==0 && m.targets[0].commandTargetRank==-1);
    assert(m.targets[1].pmCcdSlot==1 && m.targets[1].commandTargetRank==0);
    assert(m.coreSlots[8].pmCoreSlot==8 && m.coreSlots[8].commandIndexes[0]==0);
    for(unsigned i=0;i<8;++i) assert(!m.coreSlots[i].enabled);
    const auto empty=diagnoseAmdGraniteMapping(input(0x620205,3,0,0));
    assert(!empty.error && empty.coreSlots.size()==16 && !empty.activeTargets && !populated(empty));
}
void allMaskBitsRetainTheirDistinctMeaning() {
    const auto high16=diagnoseAmdGraniteMapping(input(0x620205,3,0xffff0000,0xffff0081));
    assert(!high16.error && high16.activeTargets==1);
    assert(high16.targets[0].rawCoreMask==0xffff0000 && high16.targets[0].discardedHighBits==0xffff0000);
    assert(!high16.targets[0].storedCoreMask && high16.targets[0].commandTargetRank==-1);
    assert(high16.targets[1].storedCoreMask==0x81 && high16.coreSlots[8].commandIndexes[0]==0);
    const auto high8=diagnoseAmdGraniteMapping(input(0x620205,3,0x100,1));
    assert(!high8.error && high8.activeTargets==2 && high8.enabledCores==1);
    assert(high8.targets[0].storedCoreMask==0x100 && high8.targets[0].nonEnumeratedBits==0x100);
    assert(!high8.targets[0].enumeratedCoreMask && high8.targets[0].commandTargetRank==0);
    assert(high8.targets[1].commandTargetRank==1 && high8.coreSlots[8].commandIndexes[0]==32);
    assert(high8.commandRemap[0]==0xffff && high8.commandRemap[32]==32);
}
void smtChangesStrideAndPreservesSiblingAliases() {
    const auto on=diagnoseAmdGraniteMapping(input(0x620205,3,0xff,0xff));
    const auto off=diagnoseAmdGraniteMapping(input(0x620205,3,0xff,0xff,false));
    assert(on.threadsPerCore==2 && off.threadsPerCore==1 && on.enabledCores==16 && off.enabledCores==16);
    for(unsigned i=16;i<32;++i) assert(on.commandRemap[i]==0xffff);
    for(unsigned i=8;i<16;++i) assert(off.commandRemap[i]==0xffff);
    assert(on.coreSlots[8].commandIndexes==std::vector<unsigned>({32,33}));
    assert(off.coreSlots[8].commandIndexes==std::vector<unsigned>({16}));
    for(const auto &c:on.coreSlots) {
        assert(c.commandIndexes.size()==2 && c.encodedCoordinates.size()==2);
        assert((c.encodedCoordinates[0]>>1)==(c.encodedCoordinates[1]>>1));
    }
}
void everyByteMaskKeepsStableLocalSlots() {
    for(unsigned mask=0;mask<256;++mask) for(unsigned target=0;target<2;++target) for(bool smt:{false,true}) {
        auto i=input(0x620105,1u<<target,0,0,smt); i.coreMasks[target]=mask;
        const auto m=diagnoseAmdGraniteMapping(i);
        assert(!m.error && m.coreSlots.size()==8 && m.targets[target].pmCcdSlot==0);
        unsigned enabled=0;
        for(unsigned c=0;c<8;++c) {
            const auto &r=m.coreSlots[c];
            assert(r.pmCoreSlot==c && r.localCoreSlot==c && r.firmwareTarget==target);
            assert(r.enabled==bool(mask&(1u<<c)));
            if(r.enabled) { assert(r.commandCoreRank==int(enabled)); ++enabled; }
        }
        assert(m.enabledCores==enabled && populated(m)==enabled*(smt?2u:1u));
    }
}
void jsonRetainsInputsAndUnverifiedBoundary() {
    auto m=diagnoseAmdGraniteMapping(input(0x620105,2,0xffffffff,0x81));
    const auto j=capture_json::Parser(amdGraniteMappingJson(m)).parse();
    assert(j.at("input_provenance").text=="caller-supplied");
    for(const char *name:{"live_capture","runtime_mapping_verified","physical_ccd_mapping_verified",
                         "apic_mapping_verified","os_cpu_mapping_verified","runtime_write_enabled",
                         "command_indexes_are_pm_slots","command_indexes_are_os_cpu_ids"})
        assert(capture_json::equals(j.at(name),capture_json::Json::Boolean,"false"));
    const auto &inputs=j.at("inputs");
    assert(capture_json::word(inputs.at("table_version"))==0x620105);
    assert(capture_json::word(inputs.at("core_masks").items[0])==0xffffffff);
    assert(capture_json::equals(j.at("targets").items[0].at("input_consumed"),capture_json::Json::Boolean,"false"));
    assert(j.at("targets").items[0].at("pm_ccd_slot").kind==capture_json::Json::Null);
    assert(j.at("command_remap").items.size()==64 && j.at("command_remap").items[4].kind==capture_json::Json::Null);
    assert(j.at("core_slots").items[7].at("command_indexes").items[0].text=="2");
    m.detail="quote\" slash\\ newline\n control\x01";
    assert(capture_json::Parser(amdGraniteMappingJson(m)).parse().at("detail").text==m.detail);
    const auto failure=capture_json::Parser(amdGraniteMappingJson(diagnoseAmdGraniteMapping(input(0,3,255,255)))).parse();
    assert(failure.at("targets").items.empty() && failure.at("core_slots").items.empty());
}
void jsonIgnoresTheProcessLocale() {
    struct Grouped : std::numpunct<char> {
        char do_thousands_sep() const override { return ','; }
        std::string do_grouping() const override { return "\3"; }
    };
    const std::locale saved=std::locale();
    std::locale::global(std::locale(std::locale::classic(),new Grouped));
    const auto m=diagnoseAmdGraniteMapping(input(0x620205,3,0xf1234567,0xffffffff));
    const auto text=amdGraniteMappingJson(m);
    std::locale::global(saved);
    const auto j=capture_json::Parser(text).parse();
    assert(capture_json::word(j.at("inputs").at("core_masks").items[0])==0xf1234567);
    assert(j.at("inputs").at("table_version").text=="0x00620205");
}
void emitReferenceVectors() {
    // Emit the checked-in original-byte oracle, never the implementation's
    // own derived mappings, for GUI/CLI integration tests to consume.
    std::cout.imbue(std::locale::classic());
    std::cout << "{\"source\":\"synthetic-original-byte-oracle\",\"vectors\":[";
    unsigned index=0;
    for(const auto &v:amd_granite_mapping_vectors::values) {
        if(index++) std::cout << ',';
        std::cout << "{\"table_version\":" << v.version << ",\"presence_mask\":" << v.presence
                  << ",\"core_masks\":[" << v.masks[0] << ',' << v.masks[1]
                  << "],\"smt\":" << (v.smt?"true":"false") << ",\"command_remap\":[";
        for(unsigned j=0;j<64;++j) {
            if(j) std::cout << ',';
            if(v.remap[j]==0xffff) std::cout << "null"; else std::cout << v.remap[j];
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
}
int main(int argc,char **argv) {
    if(argc==2 && std::string(argv[1])=="--emit") { emitReferenceVectors(); return 0; }
    if(argc!=1) return 2;
    referenceByteOracles(); invalidInputsHaveNoMappings(); sparseTargetKeepsPmPositions();
    emptyTargetsRemainPmSlots(); allMaskBitsRetainTheirDistinctMeaning();
    smtChangesStrideAndPreservesSiblingAliases(); everyByteMaskKeepsStableLocalSlots();
    jsonRetainsInputsAndUnverifiedBoundary(); jsonIgnoresTheProcessLocale();
    std::cout << "9 AMD Granite mapping scenario groups passed\n";
}
