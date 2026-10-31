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
    bool haveCore=false,haveComplex=false,haveCcd=false,haveSocket=false,terminated=false;
    const bool granite=out.identity.family==0x1a && out.identity.model==0x44;
    for(unsigned subleaf=0;subleaf<8;++subleaf) {
        r=s.cpuid(cpu,0x80000026,subleaf);
        if((out.error=r.error)) return out;
        const unsigned count=r.words[1]&0xffff;
        if(!count) { terminated=true; break; }
        const unsigned type=(r.words[2]>>8)&255,shift=r.words[0]&31;
        if(granite && type==3) { out.error=-ENOTSUP; return out; }
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
        if(type==2) { haveComplex=true; complexShift=shift; }
        if(type==3) { haveCcd=true; ccdShift=shift; }
        if(type==4) { haveSocket=true; socketShift=shift; }
        previousType=type; previousShift=shift; previousCount=count;
    }
    // 57896 Model 44h has one Complex per CCD, and no level type 3.
    // This only resolves CPUID topology; it does not bind a firmware PM slot.
    if(granite && haveComplex) { haveCcd=true; ccdShift=complexShift; out.ccdLevelType=2; }
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
} }
