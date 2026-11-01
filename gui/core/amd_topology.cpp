// SPDX-License-Identifier: GPL-2.0-only
#include "amd_topology.h"
#include <cerrno>
#include <limits>
#include <set>
#include <map>
#include <tuple>

namespace octool { namespace core {
namespace {
std::uint32_t mask(unsigned bits) { return std::uint32_t((UINT64_C(1)<<bits)-1); }
}
AmdTopology readAmdTopology(HardwareSession &s,unsigned cpu,bool includeL3) {
    AmdTopology out; out.cpu=cpu; out.l3Requested=includeL3; out.identity=identifyCpu(s,cpu);
    if((out.error=out.identity.error)) return out;
    if(!out.identity.amd) { out.error=-ENOTSUP; return out; }
    auto r=s.cpuid(cpu,0x80000000);
    if((out.error=r.error)) return out;
    if(r.words[0]<0x80000026) { out.error=-ENOTSUP; return out; }
    unsigned previousType=0, previousShift=0, previousCount=0;
    unsigned coreShift=0,complexShift=0,ccdShift=0,socketShift=0;
    unsigned complexCount=0,ccdCount=0;
    bool haveCore=false,haveComplex=false,haveCcd=false,haveSocket=false,terminated=false;
    const bool granite=out.identity.family==0x1a && out.identity.model==0x44;
    for(unsigned subleaf=0;subleaf<8;++subleaf) {
        r=s.cpuid(cpu,0x80000026,subleaf);
        if((out.error=r.error)) return out;
        const unsigned count=r.words[1]&0xffff;
        if(!count) { terminated=true; break; }
        const unsigned type=(r.words[2]>>8)&255,shift=r.words[0]&31;
        if((r.words[2]&255)!=subleaf || type<=previousType || type>4 || shift<previousShift ||
            count<previousCount || std::uint64_t(count)>(UINT64_C(1)<<shift) ||
            (!out.levels.empty() && r.words[3]!=out.apicId)) {
            out.error=-EINVAL; return out;
        }
        out.apicId=r.words[3];
        AmdTopologyLevel level; level.type=type; level.shift=shift; level.logicalCount=count;
        level.globalId=out.apicId>>shift;
        for(unsigned i=0;i<4;++i) level.words[i]=r.words[i];
        out.levels.push_back(level);
        if(type==1) { haveCore=true; coreShift=shift; }
        if(type==2) { haveComplex=true; complexShift=shift; complexCount=count; }
        if(type==3) { haveCcd=true; ccdShift=shift; ccdCount=count; }
        if(type==4) { haveSocket=true; socketShift=shift; }
        previousType=type; previousShift=shift; previousCount=count;
    }
    // 57896 Model 44h has one Complex per CCD. Published 9950X3D CPUID
    // captures include a redundant type 3, despite the PPR's reserved label.
    // Accept only an identical boundary/count; a conflicting level is unknown.
    // This only resolves CPUID topology; it does not bind a firmware PM slot.
    if(granite) {
        if(!haveComplex || (haveCcd && (ccdShift!=complexShift || ccdCount!=complexCount))) {
            out.error=-ENOTSUP; return out;
        }
        out.redundantCcdLevel=haveCcd;
        out.ccdLevelType=haveCcd?3:2;
        haveCcd=true; ccdShift=complexShift;
    }
    else if(haveCcd) out.ccdLevelType=3;
    if(!terminated || !haveCore || !haveCcd || !haveSocket) { out.error=-ENOTSUP; return out; }
    out.socketId=out.apicId>>socketShift;
    out.ccdInSocket=(out.apicId>>ccdShift)&mask(socketShift-ccdShift);
    out.coreInCcd=(out.apicId>>coreShift)&mask(ccdShift-coreShift);
    out.threadInCore=out.apicId&mask(coreShift);
    if(includeL3) {
        r=s.cpuid(cpu,0x80000001);
        if((out.error=r.error)) return out;
        if(!(r.words[2]&(1u<<22))) { out.error=-ENOTSUP; return out; }
        bool found=false,ended=false;
        AmdL3Cache cache;
        for(unsigned subleaf=0;subleaf<8;++subleaf) {
            r=s.cpuid(cpu,0x8000001d,subleaf);
            if((out.error=r.error)) return out;
            const unsigned type=r.words[0]&31,level=(r.words[0]>>5)&7;
            if(!type) { ended=true; break; }
            if(type>3 || !level) { out.error=-EINVAL; return out; }
            if(level!=3) continue;
            if(found || type!=3) { out.error=-EINVAL; return out; }
            found=true; cache.subleaf=subleaf;
            for(unsigned i=0;i<4;++i) cache.words[i]=r.words[i];
            cache.lineBytes=(r.words[1]&4095)+1;
            cache.partitions=((r.words[1]>>12)&1023)+1;
            cache.ways=(r.words[1]>>22)+1;
            cache.sets=std::uint64_t(r.words[2])+1;
            cache.bytes=cache.sets;
            for(unsigned factor : {cache.lineBytes,cache.partitions,cache.ways}) {
                if(cache.bytes>std::numeric_limits<std::uint64_t>::max()/factor) { out.error=-ERANGE; return out; }
                cache.bytes*=factor;
            }
            cache.sharingCapacity=((r.words[0]>>14)&4095)+1;
            while((UINT64_C(1)<<cache.sharingShift)<cache.sharingCapacity) ++cache.sharingShift;
            // Do not attach an L3 spanning a different topology domain to this CCD.
            if(cache.sharingShift>ccdShift || cache.sharingShift<coreShift) { out.error=-EINVAL; return out; }
            cache.cacheId=out.apicId>>cache.sharingShift;
        }
        if(!ended || !found) { out.error=-ENOTSUP; return out; }
        const auto after=readAmdTopology(s,cpu,false);
        if((out.error=after.error)) return out;
        if(after.identity.signature!=out.identity.signature || after.levels.size()!=out.levels.size()) { out.error=-EAGAIN; return out; }
        for(std::size_t i=0;i<out.levels.size();++i) if(after.levels[i].words!=out.levels[i].words) { out.error=-EAGAIN; return out; }
        r=s.cpuid(cpu,0x8000001d,cache.subleaf);
        if((out.error=r.error)) return out;
        for(unsigned i=0;i<4;++i) if(r.words[i]!=cache.words[i]) { out.error=-EAGAIN; return out; }
        out.l3=cache; out.l3Valid=true;
    }
    return out;
}
std::string amdTopologyJson(const AmdTopology &t) {
    std::string out="{\"format\":\"octool-amd-topology\",\"schema\":1,\"cpu\":"+std::to_string(t.cpu)+
        ",\"error\":"+std::to_string(t.error)+",\"source\":\"cpuid\",\"firmware_mapping_verified\":false,\"pm_slot_mapping_verified\":false";
    auto add=[&](const char *key,std::uint64_t value) { out+=",\""+std::string(key)+"\":"+(t.error?"null":std::to_string(value)); };
    add("apic_id",t.apicId); add("socket_id",t.socketId); add("ccd_in_socket",t.ccdInSocket);
    add("ccd_level_type",t.ccdLevelType);
    out+=",\"redundant_ccd_level\":"+(t.error?std::string("null"):std::string(t.redundantCcdLevel?"true":"false"));
    add("core_in_ccd",t.coreInCcd); add("thread_in_core",t.threadInCore);
    out+=",\"cpuid_levels\":[";
    if(!t.error) for(std::size_t i=0;i<t.levels.size();++i) {
        if(i) out+=',';
        const auto &l=t.levels[i];
        out+="{\"type\":"+std::to_string(l.type)+",\"shift\":"+std::to_string(l.shift)+
            ",\"logical_count\":"+std::to_string(l.logicalCount)+",\"words\":[";
        for(unsigned word=0;word<4;++word) { if(word) out+=','; out+=std::to_string(l.words[word]); }
        out+="]}";
    }
    out+=']';
    out+=",\"l3_requested\":"+std::string(t.l3Requested?"true":"false")+",\"l3\":";
    if(t.error || !t.l3Valid) return out+"null}";
    const auto &c=t.l3;
    out+="{\"bytes\":"+std::to_string(c.bytes)+",\"line_bytes\":"+std::to_string(c.lineBytes)+
        ",\"partitions\":"+std::to_string(c.partitions)+",\"ways\":"+std::to_string(c.ways)+",\"sets\":"+std::to_string(c.sets)+
        ",\"sharing_capacity\":"+std::to_string(c.sharingCapacity)+",\"sharing_shift\":"+std::to_string(c.sharingShift)+
        ",\"cache_id\":"+std::to_string(c.cacheId)+",\"subleaf\":"+std::to_string(c.subleaf)+",\"cpuid_words\":[";
    for(unsigned i=0;i<4;++i) { if(i) out+=','; out+=std::to_string(c.words[i]); }
    return out+"]}}";
}
AmdTopologyCatalog readAmdTopologyCatalog(HardwareSession &s,const std::vector<unsigned> &cpus,bool includeL3) {
    AmdTopologyCatalog out; out.requestedCpus=cpus; out.l3Requested=includeL3;
    const auto fail=[&](int error,unsigned cpu,bool selected=true) {
        out.error=error; out.failedCpu=cpu; out.hasFailedCpu=selected; out.cpus.clear(); return out;
    };
    if(cpus.empty() || cpus.size()>4096) return fail(-EINVAL,0,false);
    std::set<unsigned> uniqueCpus;
    for(unsigned cpu:cpus) if(cpu>1048575 || !uniqueCpus.insert(cpu).second) return fail(-EINVAL,cpu);
    std::set<std::uint32_t> apics;
    std::map<std::uint32_t,std::tuple<std::uint32_t,std::uint32_t,std::array<std::uint32_t,4>>> caches;
    for(unsigned cpu:cpus) {
        auto t=readAmdTopology(s,cpu,includeL3);
        if(t.error) return fail(t.error,cpu);
        if(!apics.insert(t.apicId).second) return fail(-EINVAL,cpu);
        if(!out.cpus.empty()) {
            const auto &first=out.cpus.front();
            if(t.identity.signature!=first.identity.signature || t.ccdLevelType!=first.ccdLevelType || t.levels.size()!=first.levels.size())
                return fail(-EINVAL,cpu);
            for(std::size_t i=0;i<t.levels.size();++i)
                if(t.levels[i].type!=first.levels[i].type || t.levels[i].shift!=first.levels[i].shift)
                    return fail(-EINVAL,cpu);
        }
        if(t.l3Valid) {
            const auto geometry=std::make_tuple(t.socketId,t.ccdInSocket,t.l3.words);
            const auto found=caches.find(t.l3.cacheId);
            if(found!=caches.end() && found->second!=geometry) return fail(-EINVAL,cpu);
            caches[t.l3.cacheId]=geometry;
        }
        out.cpus.push_back(t);
    }
    for(const auto &before:out.cpus) {
        const auto after=readAmdTopology(s,before.cpu,includeL3);
        if(after.error) return fail(after.error,before.cpu);
        bool same=before.identity.signature==after.identity.signature && before.levels.size()==after.levels.size() &&
            before.l3Valid==after.l3Valid;
        if(same) for(std::size_t i=0;i<before.levels.size();++i) same=same && before.levels[i].words==after.levels[i].words;
        if(before.l3Valid) same=same && before.l3.words==after.l3.words && before.l3.subleaf==after.l3.subleaf;
        if(!same) return fail(-EAGAIN,before.cpu);
    }
    out.rechecked=true; return out;
}
std::string amdTopologyCatalogJson(const AmdTopologyCatalog &t) {
    std::string out="{\"format\":\"octool-amd-topology-catalog\",\"schema\":1,\"source\":\"cpuid\",\"scope\":\"requested-os-cpus\",\"error\":"+std::to_string(t.error)+
        ",\"complete\":"+(t.error?std::string("false"):std::string("true"))+",\"rechecked\":"+(t.rechecked?"true":"false")+
        ",\"l3_requested\":"+(t.l3Requested?"true":"false")+",\"firmware_mapping_verified\":false,\"pm_slot_mapping_verified\":false,\"failed_cpu\":"+
        (t.hasFailedCpu?std::to_string(t.failedCpu):"null")+",\"requested_cpus\":[";
    for(std::size_t i=0;i<t.requestedCpus.size();++i) { if(i) out+=','; out+=std::to_string(t.requestedCpus[i]); }
    out+="],\"cpus\":[";
    if(!t.error) for(std::size_t i=0;i<t.cpus.size();++i) { if(i) out+=','; out+=amdTopologyJson(t.cpus[i]); }
    return out+"]}";
}
AmdApobTopology decodeAmdApobTopology(const std::vector<std::uint8_t> &record) {
    AmdApobTopology out; out.bytes=record.size(); out.inputBytes=record.size();
    if(record.size()>1048576) { out.error=-EFBIG; out.detail="Topology record exceeds 1 MiB."; return out; }
    const auto word=[&](std::size_t at) {
        return std::uint32_t(record[at]) | (std::uint32_t(record[at+1])<<8) |
            (std::uint32_t(record[at+2])<<16) | (std::uint32_t(record[at+3])<<24);
    };
    if(record.size()>=4 && word(0)==0x424f5041) {
        out.container=true;
        const auto fail=[&](int error,const char *detail) {
            out.error=error; out.detail=detail; return out;
        };
        if(record.size()<16) return fail(-EINVAL,"Truncated APOB container header.");
        if(word(4)<5) return fail(-ENOTSUP,"APOB container versions below 5 are unsupported.");
        if(word(8)!=record.size()) return fail(-EINVAL,"Declared APOB container size does not match the file.");
        std::size_t at=word(12), selected=0, selectedBytes=0;
        unsigned matches=0, records=0;
        if(at<16 || at>record.size()) return fail(-EINVAL,"Invalid first APOB record offset.");
        while(at<record.size()) {
            // Subtraction bounds every load/addition, including zero/oversized
            // TypeSize, truncated HMAC headers and malformed trailing records.
            if(record.size()-at<48) return fail(-EINVAL,"Truncated APOB record header in container.");
            const std::size_t size=word(at+12);
            if(size<48 || size>record.size()-at) return fail(-EINVAL,"Invalid APOB record size in container.");
            ++records;
            if(word(at)==3 && word(at+4)==3) { ++matches; selected=at; selectedBytes=size; }
            at+=size;
        }
        if(!matches) return fail(-ENOENT,"No group 3/type 3 topology record in APOB container.");
        if(matches!=1) return fail(-EEXIST,"Multiple topology records: extract the intended instance before decoding.");
        std::vector<std::uint8_t> single(selectedBytes);
        for(std::size_t i=0;i<selectedBytes;++i) single[i]=record[selected+i];
        auto decoded=decodeAmdApobTopology(single);
        decoded.container=true; decoded.inputBytes=record.size();
        decoded.containerVersion=word(4); decoded.containerRecords=records; decoded.recordOffset=selected;
        return decoded;
    }
    // Maximum getter span: 0x30 + 8 * (1 + 2 * (1 + 8 * 3)).
    if(record.size()<0x1c8) { out.error=-EINVAL; out.detail="Truncated APOB topology record (minimum 456 bytes)."; return out; }
    if(word(0)!=3 || word(4)!=3) { out.error=-ENOTSUP; out.detail="Expected a single APOB group 3/type 3 record, including its header."; return out; }
    if(word(12)!=record.size()) { out.error=-EINVAL; out.detail="Declared record length does not match the file."; return out; }
    out.instance=word(8); out.extensionBytes=record.size()-0x1c8;
    while(out.referenceThreadStride<2 && record[0x33+out.referenceThreadStride]) ++out.referenceThreadStride;
    std::array<std::set<std::pair<std::uint32_t,unsigned>>,2> routeKeys;
    std::set<unsigned> groupIds;
    for(unsigned g=0;g<8;++g) {
        const std::size_t base=0x30+g*0x33;
        if(record[base]==0xff) break;
        AmdApobGroup group; group.ordinal=g; group.storedId=record[base];
        if(!groupIds.insert(group.storedId).second) ++out.duplicateIds;
        std::set<unsigned> subgroupIds;
        for(unsigned s=0;s<2;++s) {
            const std::size_t sub=base+1+s*0x19;
            if(record[sub]==0xff) break;
            AmdApobSubgroup subgroup; subgroup.ordinal=s; subgroup.storedId=record[sub];
            if(!subgroupIds.insert(subgroup.storedId).second) ++out.duplicateIds;
            std::set<unsigned> coreIds;
            for(unsigned c=0;c<8;++c) {
                const std::size_t at=sub+1+c*3;
                if(record[at]==0xff) break;
                AmdApobCore core; core.ordinal=c; core.storedId=record[at];
                if(!coreIds.insert(core.storedId).second) ++out.duplicateIds;
                core.threadFlags={{record[at+1],record[at+2]}};
                // Protocol +0x38 counts a nonzero prefix, NOT a population count.
                while(core.threadCount<2 && core.threadFlags[core.threadCount]) ++core.threadCount;
                if(!core.threadFlags[0] && core.threadFlags[1]) ++out.nonPrefixThreadFlags;
                if(core.threadCount!=out.referenceThreadStride) ++out.differentThreadPrefixes;
                for(unsigned thread=0;thread<2;++thread) if(core.threadFlags[thread]) {
                    AmdApobRegisterRoute route;
                    route.group=g; route.subgroup=s; route.core=c; route.thread=thread;
                    route.bit=out.referenceThreadStride*c+thread;
                    route.providerAAddress=0x203c0020u | (std::uint32_t(group.storedId)<<23);
                    route.providerBAddress=0x20bc0020u | (std::uint32_t(subgroup.storedId)<<22);
                    if(!routeKeys[0].insert({route.providerAAddress,route.bit}).second) ++out.referenceRouteAliases[0];
                    if(!routeKeys[1].insert({route.providerBAddress,route.bit}).second) ++out.referenceRouteAliases[1];
                    out.referenceRoutes.push_back(route);
                }
                subgroup.cores.push_back(core);
            }
            group.subgroups.push_back(subgroup);
        }
        out.groups.push_back(group);
    }
    out.detail="Reference-layout record decoded; input firmware identity and physical/OS/PM mappings remain unverified.";
    return out;
}
std::string amdApobTopologyJson(const AmdApobTopology &t) {
    std::string out="{\"format\":\"octool-amd-apob-topology\",\"schema\":1,\"error\":"+std::to_string(t.error)+
        ",\"source\":\""+(t.container?"offline-apob-container":"offline-apob-record")+"\",\"reference_layout\":\"asus-creator-2503-d20482f8\","
        "\"reference_identity_verified\":false,\"physical_mapping_verified\":false,\"os_cpu_mapping_verified\":false,"
        "\"pm_slot_mapping_verified\":false,\"hardware_accessed\":false,\"record_hmac_verified\":false,"
        "\"record_header_bytes\":48,\"input_bytes\":"+std::to_string(t.inputBytes)+",\"bytes\":"+std::to_string(t.bytes);
    const auto add=[&](const char *key,std::uint64_t value) { out+=",\""+std::string(key)+"\":"+(t.error?"null":std::to_string(value)); };
    add("instance",t.instance); add("extension_bytes",t.extensionBytes);
    add("duplicate_ids",t.duplicateIds); add("non_prefix_thread_flags",t.nonPrefixThreadFlags);
    out+=",\"container\":";
    if(t.container && !t.error) out+="{\"version\":"+std::to_string(t.containerVersion)+
        ",\"record_count\":"+std::to_string(t.containerRecords)+",\"selected_record_offset\":"+std::to_string(t.recordOffset)+"}";
    else out+="null";
    out+=",\"reference_register_routes\":";
    if(t.error) out+="null";
    else {
        out+="{\"evidence\":\"asus-creator-2503-d20482f8-static\",\"runtime_provider_verified\":false,"
            "\"register_effect_verified\":false,\"host_physical_addresses\":false,\"runtime_write_enabled\":false,"
            "\"thread_indices_limited_to_flags\":true,\"first_core_flag_prefix\":"+std::to_string(t.referenceThreadStride)+
            ",\"different_thread_prefixes\":"+std::to_string(t.differentThreadPrefixes)+
            ",\"provider_a_aliases\":"+std::to_string(t.referenceRouteAliases[0])+
            ",\"provider_b_aliases\":"+std::to_string(t.referenceRouteAliases[1])+",\"candidates\":[";
        for(std::size_t i=0;i<t.referenceRoutes.size();++i) {
            if(i) out+=',';
            const auto &r=t.referenceRoutes[i];
            out+="{\"group_ordinal\":"+std::to_string(r.group)+",\"subgroup_ordinal\":"+std::to_string(r.subgroup)+
                ",\"core_ordinal\":"+std::to_string(r.core)+",\"thread_flag_index\":"+std::to_string(r.thread)+
                ",\"bit\":"+std::to_string(r.bit)+",\"provider_a_address\":"+std::to_string(r.providerAAddress)+
                ",\"provider_b_address\":"+std::to_string(r.providerBAddress)+"}";
        }
        out+="]}";
    }
    out+=",\"groups\":[";
    if(!t.error) for(std::size_t g=0;g<t.groups.size();++g) {
        if(g) out+=',';
        const auto &group=t.groups[g];
        out+="{\"ordinal\":"+std::to_string(group.ordinal)+",\"stored_id\":"+std::to_string(group.storedId)+",\"subgroups\":[";
        for(std::size_t s=0;s<group.subgroups.size();++s) {
            if(s) out+=',';
            const auto &sub=group.subgroups[s];
            out+="{\"ordinal\":"+std::to_string(sub.ordinal)+",\"stored_id\":"+std::to_string(sub.storedId)+",\"cores\":[";
            for(std::size_t c=0;c<sub.cores.size();++c) {
                if(c) out+=',';
                const auto &core=sub.cores[c];
                out+="{\"ordinal\":"+std::to_string(core.ordinal)+",\"stored_id\":"+std::to_string(core.storedId)+
                    ",\"thread_flags\":["+std::to_string(core.threadFlags[0])+","+std::to_string(core.threadFlags[1])+
                    "],\"thread_prefix_count\":"+std::to_string(core.threadCount)+"}";
            }
            out+="]}";
        }
        out+="]}";
    }
    return out+"]}";
}
AmdApobComparison readAmdApobComparison(HardwareSession &s,
    const std::vector<std::uint8_t> &bytes,const std::vector<unsigned> &cpus,bool includeL3) {
    AmdApobComparison out; out.record=decodeAmdApobTopology(bytes);
    out.catalog.requestedCpus=cpus; out.catalog.l3Requested=includeL3;
    const auto fail=[&](int error,const char *detail) {
        out.error=error; out.detail=detail; out.inputsRechecked=false; out.completeReferenceMatch=false;
        out.matched=out.missingApics=0; out.candidates.clear(); out.unmatchedCpus.clear(); out.inputs.clear();
        out.catalog.error=error; out.catalog.rechecked=false; out.catalog.cpus.clear(); return out;
    };
    if(out.record.error) return fail(out.record.error,"Invalid APOB record or container.");
    const auto &record=out.record;
    if(record.instance || record.groups.empty() || record.groups.size()>2 || record.duplicateIds || record.nonPrefixThreadFlags)
        return fail(-ENOTSUP,"Reference comparison requires instance 0, one or two unambiguous groups and prefix thread flags.");
    for(const auto &g:record.groups) {
        if(g.storedId>15 || g.subgroups.size()!=1 || g.subgroups[0].cores.empty())
            return fail(-ENOTSUP,"Record is outside the single-subgroup Model 44h reference shape.");
    }
    std::set<unsigned> uniqueCpus;
    if(cpus.empty() || cpus.size()>32) return fail(-EINVAL,"Select between 1 and 32 available OS CPUs for this reference shape.");
    for(unsigned cpu:cpus) if(cpu>1048575 || !uniqueCpus.insert(cpu).second)
        return fail(-EINVAL,"Invalid or duplicate OS CPU ID.");
    const std::array<std::uint32_t,3> leaves={{0,0x80000001,0xb}};
    // Bracket both catalog sweeps with the constructor/provider inputs. The
    // CPUID backend binds each read to the requested OS CPU and restores affinity.
    for(unsigned cpu:cpus) {
        std::array<std::array<std::uint32_t,4>,3> input{};
        for(unsigned i=0;i<leaves.size();++i) {
            const auto r=s.cpuid(cpu,leaves[i]);
            if(r.error) return fail(r.error,"Cannot read reference CPUID inputs.");
            for(unsigned j=0;j<4;++j) input[i][j]=r.words[j];
            if(i==0 && (r.words[0]<0xb || r.words[1]!=0x68747541 || r.words[2]!=0x444d4163 || r.words[3]!=0x69746e65))
                return fail(-ENOTSUP,"Reference comparison requires AMD CPUID Fn0B.");
            // Pinned provider A's entry predicate, not proof it was installed.
            if(i==1 && ((r.words[0]&0x0fff0f00)!=0x00b40f00 || (r.words[1]>>28)!=0))
                return fail(-ENOTSUP,"CPUID does not meet the reference provider A entry predicate.");
            if(i==2 && ((r.words[0]&31)!=1 || (r.words[1]&0xffff)!=2 || (r.words[2]&0xffff)!=0x100))
                return fail(-ENOTSUP,"Reference comparison requires the published SMT2 Fn0B boundary.");
        }
        out.inputs.push_back(input);
    }
    out.catalog=readAmdTopologyCatalog(s,cpus,includeL3);
    if(out.catalog.error) return fail(out.catalog.error,"OS CPU catalog changed or is unavailable.");
    const unsigned shifts[]={1,4,4,5}, counts[]={2,16,16,32};
    std::map<std::uint32_t,unsigned> osByApic;
    for(std::size_t i=0;i<cpus.size();++i) {
        const auto &t=out.catalog.cpus[i];
        if(t.identity.family!=0x1a || t.identity.model!=0x44 || t.socketId || t.apicId>=32 || t.levels.size()!=4 || !t.redundantCcdLevel)
            return fail(-ENOTSUP,"OS topology is outside the published single-socket Model 44h reference shape.");
        for(unsigned j=0;j<4;++j) if(t.levels[j].type!=j+1 || t.levels[j].shift!=shifts[j] || t.levels[j].logicalCount!=counts[j])
            return fail(-ENOTSUP,"Reference comparison requires the actual redundant CCD level and published boundaries.");
        for(unsigned j=0;j<leaves.size();++j) {
            const auto r=s.cpuid(cpus[i],leaves[j]);
            if(r.error) return fail(r.error,"Cannot recheck reference CPUID inputs.");
            for(unsigned k=0;k<4;++k) if(out.inputs[i][j][k]!=r.words[k])
                return fail(-EAGAIN,"Reference CPUID inputs changed; comparison discarded.");
        }
        if(out.inputs[i][2][3]!=t.apicId || out.inputs[i][1][0]!=t.identity.signature)
            return fail(-EAGAIN,"CPUID identity or APIC ID disagrees between leaves.");
        osByApic.emplace(t.apicId,t.cpu);
    }
    // Recovered sorter 0x54e0 returns original group ORDINALS in stored-ID
    // order. Constructor 0x526c uses those ordinals, not the stored IDs.
    const unsigned order[]={0,4,8,12,2,6,10,14,3,7,11,15,1,5,9,13};
    std::set<std::uint32_t> predicted;
    std::set<unsigned> matchedCpus;
    for(unsigned stored:order) for(const auto &g:record.groups) if(g.storedId==stored)
        for(const auto &sub:g.subgroups) for(const auto &core:sub.cores) for(unsigned thread=0;thread<core.threadCount;++thread) {
            AmdApobCpuCandidate row;
            row.group=g.ordinal; row.subgroup=sub.ordinal; row.core=core.ordinal; row.thread=thread;
            row.storedGroup=g.storedId; row.storedSubgroup=sub.storedId; row.storedCore=core.storedId;
            row.apicId=(g.ordinal<<4)+(sub.ordinal<<4)+(core.ordinal<<1)+thread;
            if(!predicted.insert(row.apicId).second) return fail(-EINVAL,"Reference APIC candidates collide.");
            const auto found=osByApic.find(row.apicId);
            if(found!=osByApic.end()) { row.matched=true; row.cpu=found->second; ++out.matched; matchedCpus.insert(row.cpu); }
            else ++out.missingApics;
            out.candidates.push_back(row);
        }
    if(out.candidates.empty()) return fail(-ENODATA,"Record has no enabled thread prefixes to compare.");
    for(unsigned cpu:cpus) if(!matchedCpus.count(cpu)) out.unmatchedCpus.push_back(cpu);
    out.inputsRechecked=true;
    out.completeReferenceMatch=!out.missingApics && out.unmatchedCpus.empty();
    out.detail="Reference APIC comparison only. Record origin, HMAC, installed provider, physical targets and PM slots remain unverified.";
    return out;
}
std::string amdApobComparisonJson(const AmdApobComparison &t) {
    std::string out="{\"format\":\"octool-amd-apob-os-comparison\",\"schema\":1,\"error\":"+std::to_string(t.error)+
        ",\"reference\":\"asus-creator-2503-provider-a-model44-smt2\",\"scope\":\"requested-os-cpus\","
        "\"runtime_provider_verified\":false,\"record_origin_verified\":false,\"record_hmac_verified\":false,"
        "\"physical_mapping_verified\":false,\"pm_slot_mapping_verified\":false,\"runtime_write_enabled\":false,"
        "\"inputs_rechecked\":"+std::string(!t.error && t.inputsRechecked?"true":"false")+
        ",\"complete_reference_match\":"+(!t.error && t.completeReferenceMatch?"true":"false")+
        ",\"matched\":"+(t.error?"null":std::to_string(t.matched))+
        ",\"missing_apics\":"+(t.error?"null":std::to_string(t.missingApics))+
        ",\"topology_catalog\":"+amdTopologyCatalogJson(t.catalog)+",\"cpuid_inputs\":[";
    if(!t.error) for(std::size_t i=0;i<t.inputs.size();++i) {
        if(i) out+=',';
        out+="{\"cpu\":"+std::to_string(t.catalog.requestedCpus[i]);
        const char *names[]={"fn00000000","fn80000001","fn0000000b_0"};
        for(unsigned j=0;j<3;++j) {
            out+=",\""+std::string(names[j])+"\":[";
            for(unsigned k=0;k<4;++k) { if(k) out+=','; out+=std::to_string(t.inputs[i][j][k]); }
            out+=']';
        }
        out+='}';
    }
    out+="],\"candidates\":[";
    if(!t.error) for(std::size_t i=0;i<t.candidates.size();++i) {
        if(i) out+=',';
        const auto &r=t.candidates[i];
        out+="{\"group_ordinal\":"+std::to_string(r.group)+",\"subgroup_ordinal\":"+std::to_string(r.subgroup)+
            ",\"core_ordinal\":"+std::to_string(r.core)+",\"thread\":"+std::to_string(r.thread)+
            ",\"stored_group_id\":"+std::to_string(r.storedGroup)+",\"stored_subgroup_id\":"+std::to_string(r.storedSubgroup)+
            ",\"stored_core_id\":"+std::to_string(r.storedCore)+",\"reference_apic_id\":"+std::to_string(r.apicId)+
            ",\"os_cpu\":"+(r.matched?std::to_string(r.cpu):"null")+"}";
    }
    out+="],\"unmatched_os_cpus\":[";
    if(!t.error) for(std::size_t i=0;i<t.unmatchedCpus.size();++i) { if(i) out+=','; out+=std::to_string(t.unmatchedCpus[i]); }
    return out+"]}";
}
} }
