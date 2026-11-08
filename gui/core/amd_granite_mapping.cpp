// SPDX-License-Identifier: GPL-2.0-only
#include "amd_granite_mapping.h"
#include <cerrno>
#include <iomanip>
#include <locale>
#include <sstream>

namespace octool { namespace core {
namespace {
std::string jsonString(const std::string &value) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << '"';
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') s << '\\' << char(c);
        else if(c<32) s << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else s << char(c);
    }
    s << '"';
    return s.str();
}
std::string hexWord(std::uint32_t value) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << "\"0x" << std::hex << std::setw(8) << std::setfill('0') << value << "\"";
    return s.str();
}
void indexOrNull(std::ostringstream &s,int value) {
    if(value<0) s << "null"; else s << value;
}
void unsignedList(std::ostringstream &s,const std::vector<unsigned> &values) {
    s << '[';
    for(std::size_t i=0;i<values.size();++i) { if(i) s << ','; s << values[i]; }
    s << ']';
}
}
AmdGraniteMapping diagnoseAmdGraniteMapping(const AmdGraniteMappingInput &input) {
    AmdGraniteMapping out;
    out.input=input;
    out.commandRemap.fill(0xffff);
    if(input.tableVersion!=0x620105 && input.tableVersion!=0x620205) {
        out.error=-ENOTSUP;
        out.detail="Only reference Granite PM versions 0x620105 and 0x620205 are supported";
        return out;
    }
    if(input.presenceMask==0 || (input.presenceMask&~3u)) {
        out.error=-EINVAL;
        out.detail="The reference presence mask must contain one or both of bits 0 and 1";
        return out;
    }
    const unsigned width=input.tableVersion==0x620205 ? 2u : 1u;
    const unsigned present=(input.presenceMask&1u)+((input.presenceMask>>1)&1u);
    if(width!=present) {
        out.error=-EINVAL;
        out.detail="The supplied presence mask disagrees with the PM table storage width";
        return out;
    }
    out.pmCcdSlots=width;
    out.threadsPerCore=input.smt ? 2u : 1u;
    const unsigned stride=input.smt ? 32u : 16u;
    unsigned storageRank=0,activeRank=0;
    for(unsigned target=0;target<2;++target) {
        AmdGraniteMappingTarget t;
        t.firmwareTarget=target;
        t.rawCoreMask=input.coreMasks[target];
        t.present=(input.presenceMask&(1u<<target))!=0;
        if(t.present) {
            t.pmCcdSlot=int(storageRank);
            t.storedCoreMask=t.rawCoreMask&0xffffu;
            t.enumeratedCoreMask=t.storedCoreMask&0xffu;
            t.discardedHighBits=t.rawCoreMask&0xffff0000u;
            t.nonEnumeratedBits=t.storedCoreMask&0xff00u;
            if(t.storedCoreMask) t.commandTargetRank=int(activeRank++);
            unsigned coreRank=0;
            for(unsigned core=0;core<8;++core) {
                AmdGraniteMappingCore c;
                c.pmCcdSlot=storageRank;
                c.pmCoreSlot=storageRank*8+core;
                c.localCoreSlot=core;
                c.firmwareTarget=target;
                c.enabled=(t.enumeratedCoreMask&(1u<<core))!=0;
                if(c.enabled) {
                    c.commandCoreRank=int(coreRank++);
                    ++out.enabledCores;
                    for(unsigned thread=0;thread<out.threadsPerCore;++thread) {
                        const unsigned index=unsigned(t.commandTargetRank)*stride+
                            unsigned(c.commandCoreRank)*out.threadsPerCore+thread;
                        const unsigned encoded=target*stride+core*out.threadsPerCore+thread;
                        c.commandIndexes.push_back(index);
                        c.encodedCoordinates.push_back(encoded);
                        out.commandRemap[index]=std::uint16_t(encoded);
                    }
                }
                out.coreSlots.push_back(c);
            }
            ++storageRank;
        }
        out.targets.push_back(t);
    }
    out.activeTargets=activeRank;
    out.detail="Reference mapping computed from caller-supplied inputs; physical CCD, APIC and OS CPU binding remain unverified";
    return out;
}
std::string amdGraniteMappingJson(const AmdGraniteMapping &m) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << "{\"error\":" << m.error << ",\"detail\":" << jsonString(m.detail)
      << ",\"reference\":\"granite-smu-98.84.0-static\""
      << ",\"input_provenance\":\"caller-supplied\""
      << ",\"live_capture\":false,\"runtime_mapping_verified\":false"
      << ",\"physical_ccd_mapping_verified\":false,\"apic_mapping_verified\":false"
      << ",\"os_cpu_mapping_verified\":false,\"runtime_write_enabled\":false"
      << ",\"command_indexes_are_pm_slots\":false,\"command_indexes_are_os_cpu_ids\":false"
      << ",\"inputs\":{\"table_version\":" << hexWord(m.input.tableVersion)
      << ",\"presence_mask\":" << hexWord(m.input.presenceMask)
      << ",\"core_masks\":[" << hexWord(m.input.coreMasks[0]) << ',' << hexWord(m.input.coreMasks[1])
      << "],\"smt\":" << (m.input.smt ? "true" : "false") << '}'
      << ",\"pm_ccd_slots\":" << m.pmCcdSlots << ",\"active_command_targets\":" << m.activeTargets
      << ",\"enabled_core_slots\":" << m.enabledCores << ",\"threads_per_core\":" << m.threadsPerCore
      << ",\"targets\":[";
    for(std::size_t i=0;i<m.targets.size();++i) {
        const auto &t=m.targets[i];
        if(i) s << ',';
        s << "{\"firmware_target\":" << t.firmwareTarget << ",\"present\":" << (t.present ? "true" : "false")
          << ",\"input_consumed\":" << (t.present ? "true" : "false") << ",\"pm_ccd_slot\":";
        indexOrNull(s,t.pmCcdSlot);
        s << ",\"command_target_rank\":"; indexOrNull(s,t.commandTargetRank);
        s << ",\"raw_core_mask\":" << hexWord(t.rawCoreMask)
          << ",\"stored_core_mask\":" << hexWord(t.storedCoreMask)
          << ",\"enumerated_core_mask\":" << hexWord(t.enumeratedCoreMask)
          << ",\"discarded_high_bits\":" << hexWord(t.discardedHighBits)
          << ",\"non_enumerated_bits\":" << hexWord(t.nonEnumeratedBits) << '}';
    }
    s << "],\"core_slots\":[";
    for(std::size_t i=0;i<m.coreSlots.size();++i) {
        const auto &c=m.coreSlots[i];
        if(i) s << ',';
        s << "{\"pm_ccd_slot\":" << c.pmCcdSlot << ",\"pm_core_slot\":" << c.pmCoreSlot
          << ",\"local_core_slot\":" << c.localCoreSlot << ",\"firmware_target\":" << c.firmwareTarget
          << ",\"enabled\":" << (c.enabled ? "true" : "false") << ",\"command_core_rank\":";
        indexOrNull(s,c.commandCoreRank);
        s << ",\"command_indexes\":"; unsignedList(s,c.commandIndexes);
        s << ",\"encoded_coordinates\":"; unsignedList(s,c.encodedCoordinates);
        s << ",\"checked_consumer_siblings_alias_core\":" << (c.commandIndexes.size()==2 ? "true" : "false") << '}';
    }
    s << "],\"command_remap\":[";
    for(std::size_t i=0;i<m.commandRemap.size();++i) {
        if(i) s << ',';
        if(m.commandRemap[i]==0xffff) s << "null"; else s << m.commandRemap[i];
    }
    s << "]}";
    return s.str();
}
} }
