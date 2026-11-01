// SPDX-License-Identifier: GPL-2.0-only
#include "amd_topology.h"
#include "amd_apob_fixture.h"
#include "amd_apob_comparison_fixture.h"
#include "amd_apob_reference_vectors.h"
#include <cassert>
#include <cerrno>
#include <iostream>
using namespace octool::core;
using amd_apob_fixture::record;
using amd_apob_fixture::word;
void sparseIdsAndFlags() {
    const auto t=decodeAmdApobTopology(record());
    assert(!t.error && t.bytes==456 && t.instance==0x102 && !t.extensionBytes);
    assert(t.groups.size()==2 && t.groups[0].storedId==8 && t.groups[1].storedId==4);
    assert(t.groups[0].ordinal==0 && t.groups[1].ordinal==1);
    const auto &s=t.groups[0].subgroups;
    assert(s.size()==1 && s[0].ordinal==0 && s[0].storedId==1 && s[0].cores.size()==2);
    const auto &c=s[0].cores;
    assert(c[0].storedId==6 && c[0].threadCount==2 && c[1].storedId==2 && c[1].threadCount==0);
    assert(c[1].threadFlags[0]==0 && c[1].threadFlags[1]==1);
    assert(t.groups[1].subgroups[0].cores[0].threadCount==1);
    assert(t.nonPrefixThreadFlags==1 && !t.duplicateIds);
    assert(t.referenceThreadStride==2 && t.differentThreadPrefixes==2);
    assert(t.referenceRoutes.size()==4 && !t.referenceRouteAliases[0] && !t.referenceRouteAliases[1]);
    const auto &r=t.referenceRoutes;
    assert(r[0].providerAAddress==0x243c0020 && r[0].providerBAddress==0x20fc0020 && r[0].bit==0);
    assert(r[1].bit==1 && r[2].bit==3 && r[2].core==1 && r[2].thread==1);
    assert(r[3].providerAAddress==0x223c0020 && r[3].providerBAddress==0x20bc0020 && r[3].bit==0);
    // Changing the stored core ID must not change a reference selector bit.
    auto b=record(); b[0x35]=200;
    assert(decodeAmdApobTopology(b).referenceRoutes[2].bit==3);
}
void boundedSingleRecord() {
    const auto good=record();
    for(std::size_t n=0;n<good.size();++n) {
        // Construct the exact truncated size; no resize growth path for GCC 14
        // to diagnose while optimizing this every-length boundary sweep.
        std::vector<std::uint8_t> b(n);
        for(std::size_t i=0;i<n;++i) b[i]=good[i];
        const auto t=decodeAmdApobTopology(b);
        assert(t.error==-EINVAL && t.groups.empty() && t.referenceRoutes.empty());
    }
    for(unsigned at:{0u,4u,12u}) {
        auto b=good; word(b,at,0xffffffff);
        const auto t=decodeAmdApobTopology(b); assert(t.error && t.groups.empty());
    }
    // Allocate both records up front; GCC 11 diagnoses vector range insertion
    // here as an overread under -O3, despite the separate source container.
    std::vector<std::uint8_t> b(good.size()*2);
    for(std::size_t i=0;i<b.size();++i) b[i]=good[i%good.size()];
    assert(decodeAmdApobTopology(b).error==-EINVAL);
    b=good; b.resize(1048576,0x5a); word(b,12,unsigned(b.size()));
    auto t=decodeAmdApobTopology(b); assert(!t.error && t.extensionBytes==1048120 && t.groups.size()==2);
    b.push_back(0); word(b,12,unsigned(b.size()));
    t=decodeAmdApobTopology(b); assert(t.error==-EFBIG && t.groups.empty());
}
void completeSpanAndArbitraryIds() {
    auto b=record();
    for(unsigned i=48;i<456;++i) b[i]=254;
    const auto t=decodeAmdApobTopology(b); assert(!t.error && t.groups.size()==8 && t.duplicateIds==127);
    for(const auto &g:t.groups) {
        assert(g.storedId==254 && g.subgroups.size()==2);
        for(const auto &s:g.subgroups) {
            assert(s.storedId==254 && s.cores.size()==8);
            for(const auto &c:s.cores) assert(c.storedId==254 && c.threadCount==2 && c.threadFlags[1]==254);
        }
    }
    assert(t.groups[7].subgroups[1].cores[7].ordinal==7);
    assert(t.referenceRoutes.size()==256 && t.referenceRouteAliases[0]==240 && t.referenceRouteAliases[1]==240);
}
void sentinelAndAmbiguousRecords() {
    auto b=record(); b[0x30]=255;
    assert(decodeAmdApobTopology(b).groups.empty()); // Later entries intentionally nonempty.
    b=record(); b[0x31]=255;
    auto t=decodeAmdApobTopology(b); assert(t.groups[0].subgroups.empty() && t.groups.size()==2);
    b=record(); b[0x32]=255;
    t=decodeAmdApobTopology(b); assert(t.groups[0].subgroups[0].cores.empty());
    b=record(); b[0x63]=8; b[0x35]=6;
    t=decodeAmdApobTopology(b); assert(t.duplicateIds==2 && t.groups.size()==2);
    assert(t.referenceRouteAliases[0]==1 && t.referenceRouteAliases[1]==0);
    // A zero initial prefix aliases different entries, even though the raw
    // second flags remain nonzero and the original selector accepts them.
    b=record(); b[0x33]=0; t=decodeAmdApobTopology(b);
    assert(t.referenceThreadStride==0 && t.referenceRoutes.size()==3);
    assert(t.referenceRouteAliases[0]==1 && t.referenceRouteAliases[1]==1);
    assert(t.referenceRoutes[0].bit==1 && t.referenceRoutes[1].bit==1);
    // Address construction is OR, not addition. Distinct stored IDs can
    // collide with bits already set in the fixed address base.
    b=record(); b[0x30]=0; b[0x63]=64; b[0x31]=0; b[0x64]=2;
    t=decodeAmdApobTopology(b); assert(!t.duplicateIds);
    assert(t.referenceRouteAliases[0]==1 && t.referenceRouteAliases[1]==1);
    assert(t.referenceRoutes[0].providerAAddress==0x203c0020 && t.referenceRoutes[3].providerAAddress==0x203c0020);
    assert(t.referenceRoutes[0].providerBAddress==0x20bc0020 && t.referenceRoutes[3].providerBAddress==0x20bc0020);
    for(unsigned first:{0u,1u,2u,255u}) for(unsigned second:{0u,1u,2u,255u}) {
        b=record(); b[0x33]=std::uint8_t(first); b[0x34]=std::uint8_t(second);
        const auto c=decodeAmdApobTopology(b).groups[0].subgroups[0].cores[0];
        assert(c.threadCount==(first?(second?2u:1u):0u));
    }
}
void jsonAndMutationBoundaries() {
    const auto good=record();
    for(unsigned at=0;at<456;++at) for(unsigned v:{0u,1u,254u,255u}) {
        auto b=good; b[at]=std::uint8_t(v); const auto t=decodeAmdApobTopology(b);
        const auto json=amdApobTopologyJson(t);
        assert(json.find("\"physical_mapping_verified\":false")!=std::string::npos);
        assert(json.find("\"pm_slot_mapping_verified\":false")!=std::string::npos);
        assert(json.find("\"reference_identity_verified\":false")!=std::string::npos);
        assert(json.find("\"hardware_accessed\":false")!=std::string::npos);
        if(t.error) assert(t.groups.empty() && t.referenceRoutes.empty() && json.find("\"instance\":null")!=std::string::npos &&
                           json.find("\"reference_register_routes\":null")!=std::string::npos);
        else {
            assert(t.groups.size()<=8 && t.referenceRoutes.size()<=256);
            assert(json.find("\"runtime_provider_verified\":false")!=std::string::npos);
            assert(json.find("\"runtime_write_enabled\":false")!=std::string::npos);
            for(const auto &route:t.referenceRoutes) assert(route.thread<2 && route.bit<16);
        }
    }
}
void completeContainerAndAmbiguity() {
    auto b=amd_apob_fixture::container();
    auto t=decodeAmdApobTopology(b);
    assert(!t.error && t.container && t.containerVersion==5 && t.containerRecords==3);
    assert(t.recordOffset==80 && t.inputBytes==584 && t.bytes==456 && t.groups.size()==2);
    assert(t.instance==0x102 && t.groups[0].storedId==8);
    assert(t.referenceRoutes.size()==4 && t.referenceRoutes[2].bit==3);
    auto json=amdApobTopologyJson(t);
    assert(json.find("\"source\":\"offline-apob-container\"")!=std::string::npos);
    assert(json.find("\"record_hmac_verified\":false")!=std::string::npos);
    assert(json.find("\"selected_record_offset\":80")!=std::string::npos);
    assert(json.find("\"pm_slot_mapping_verified\":false")!=std::string::npos);
    for(unsigned instance:{0x102u,0u,0xffffffffu}) {
        b=amd_apob_fixture::container(true); word(b,544,instance);
        t=decodeAmdApobTopology(b); assert(t.error==-EEXIST && t.groups.empty());
    }
    b=amd_apob_fixture::container(); word(b,84,4);
    assert(decodeAmdApobTopology(b).error==-ENOENT);
    // HMAC is opaque, including all-ones; successful decoding never verifies it.
    b=amd_apob_fixture::container();
    for(unsigned i=96;i<128;++i) b[i]=255;
    t=decodeAmdApobTopology(b); assert(!t.error && t.groups[0].storedId==8);
    word(b,4,4); assert(decodeAmdApobTopology(b).error==-ENOTSUP);
}
void malformedContainersNeverExposePartialGroups() {
    const auto good=amd_apob_fixture::container();
    for(std::size_t n=0;n<good.size();++n) {
        std::vector<std::uint8_t> b(n);
        for(std::size_t i=0;i<n;++i) b[i]=good[i];
        auto t=decodeAmdApobTopology(b); assert(t.error && t.groups.empty());
        if(n>=16) {
            word(b,8,unsigned(n)); t=decodeAmdApobTopology(b);
            if(n==536) assert(!t.error && t.containerRecords==2);
            else assert(t.error && t.groups.empty());
        }
    }
    for(unsigned at:{8u,12u,44u,92u,548u}) for(unsigned value:{0u,15u,47u,0xfffffff0u,0xffffffffu}) {
        auto b=good; word(b,at,value); const auto t=decodeAmdApobTopology(b);
        assert(t.error && t.groups.empty());
        const auto json=amdApobTopologyJson(t);
        assert(json.find("\"groups\":[]")!=std::string::npos);
        assert(json.find("\"container\":null")!=std::string::npos);
    }
    // Every structural field is mutated independently, including the tail
    // AFTER a usable topology record. No loop can be driven by TypeSize zero.
    for(unsigned at:{32u,36u,80u,84u,536u,540u}) for(unsigned value:{0u,1u,3u,0xffffffffu}) {
        auto b=good; word(b,at,value); const auto t=decodeAmdApobTopology(b);
        if(t.error) assert(t.groups.empty());
        else assert(t.groups.size()==2 && t.containerRecords==3);
    }
    auto b=good; b.push_back(0); word(b,8,unsigned(b.size()));
    assert(decodeAmdApobTopology(b).error==-EINVAL);
}
namespace {
struct Borrowed : HardwareBackend {
    amd_topology_fixture::CatalogDevice &d;
    explicit Borrowed(amd_topology_fixture::CatalogDevice &value):d(value) {}
    Reply execute(const Request &r) override { return d.execute(r); }
    CpuIdReply cpuid(unsigned c,std::uint32_t l,std::uint32_t sub) override { return d.cpuid(c,l,sub); }
    Backend backend(Space s) const override { return d.backend(s); }
};
AmdApobComparison compare(amd_topology_fixture::CatalogDevice &d,const std::vector<std::uint8_t> &b,
                         const std::vector<unsigned> &cpus,bool l3=false) {
    HardwareService service{std::unique_ptr<HardwareBackend>(new Borrowed(d))}; AmdApobComparison out;
    const int error=service.transaction([&](HardwareSession &s) { out=readAmdApobComparison(s,b,cpus,l3); return out.error; });
    assert(error==out.error && !d.registerRequests); return out;
}
void referenceComparisonPreservesOsIds() {
    std::vector<unsigned> cpus; for(unsigned i=0;i<32;++i) cpus.push_back(1000-7*i);
    amd_topology_fixture::CatalogDevice d(cpus); amd_apob_comparison_fixture::configure(d,cpus);
    const auto r=compare(d,amd_apob_comparison_fixture::record(),cpus,true);
    assert(!r.error && r.inputsRechecked && r.completeReferenceMatch && r.matched==32 && !r.missingApics);
    assert(r.catalog.requestedCpus==cpus && r.unmatchedCpus.empty() && r.candidates.size()==32);
    // Original-byte consumer: stored ID 0 sorts before 4, but group ordinal 1
    // still contributes 16. Reversing OS IDs must not renumber that result.
    for(unsigned i=0;i<32;++i) {
        const auto &c=r.candidates[i]; const unsigned apic=i<16?i+16:i-16;
        assert(c.group==(i<16?1u:0u) && c.apicId==apic && c.cpu==cpus[31-apic] && c.matched);
        assert(c.storedCore==15-(i%16)/2);
        assert(r.catalog.cpus[i].cpu==cpus[i] && r.catalog.cpus[i].l3.bytes==(i<16?33554432u:100663296u));
    }
    const auto json=amdApobComparisonJson(r);
    for(const auto key:{"runtime_provider_verified","record_origin_verified","record_hmac_verified","physical_mapping_verified","pm_slot_mapping_verified","runtime_write_enabled"})
        assert(json.find("\""+std::string(key)+"\":false")!=std::string::npos);
    assert(json.find("\"complete_reference_match\":true")!=std::string::npos);
}
void referenceComparisonReportsPartialSets() {
    const std::vector<unsigned> cpus={130,7};
    for(unsigned mode=0;mode<3;++mode) {
        auto b=amd_apob_comparison_fixture::record();
        if(mode==1) { b[0x63]=255; } // Only group 0 => both OS APICs unmatched.
        if(mode==2) { b[0x66+3*7]=0; b[0x67+3*7]=0; } // Remove both APIC 30/31 candidates.
        amd_topology_fixture::CatalogDevice d(cpus); amd_apob_comparison_fixture::configure(d,cpus);
        const auto r=compare(d,b,cpus);
        assert(!r.error && !r.completeReferenceMatch && r.inputsRechecked);
        assert(r.matched==(mode?0u:2u)); assert(r.unmatchedCpus.size()==(mode?2u:0u));
        assert(r.missingApics==(mode==1?16u:30u));
        if(mode) assert(r.unmatchedCpus==cpus);
    }
}
void referenceComparisonRejectsRecordsBeforeCpuid() {
    const std::vector<unsigned> cpus={130};
    for(unsigned mode=0;mode<9;++mode) {
        auto b=amd_apob_comparison_fixture::record();
        if(mode==0) b.resize(455);
        if(mode==1) b[8]=1;
        if(mode==2) b[0x63]=4;
        if(mode==3) b[0x33]=0;
        if(mode==4) b[0x30]=16;
        if(mode==5) b[0x31]=255;
        if(mode==6) b[0x32]=255;
        if(mode==7) b[0x35]=15;
        if(mode==8) b[0x96]=8; // Third group is outside the reference shape.
        amd_topology_fixture::CatalogDevice d(cpus); amd_apob_comparison_fixture::configure(d,cpus);
        const auto r=compare(d,b,cpus); assert(r.error && r.candidates.empty() && !d.calls);
    }
    for(const auto &ids:std::vector<std::vector<unsigned>>{{},{130,130},{1048576},std::vector<unsigned>(33,130)}) {
        amd_topology_fixture::CatalogDevice d(cpus);
        assert(compare(d,amd_apob_comparison_fixture::record(),ids).error==-EINVAL && !d.calls);
    }
}
void referenceComparisonRejectsUnsupportedCpuid() {
    const std::vector<unsigned> cpus={130,7};
    for(unsigned mode=0;mode<11;++mode) {
        amd_topology_fixture::CatalogDevice d(cpus); amd_apob_comparison_fixture::configure(d,cpus);
        auto &cpu=d.devices.at(7);
        if(mode==0) cpu.maximum=1;
        if(mode==1) cpu.package=1u<<28;
        if(mode==2) cpu.extendedSignature=0x00b60f00;
        if(mode==3) cpu.smtShift=0;
        if(mode==4) cpu.smtCount=1;
        if(mode==5) cpu.smtType=0x200;
        if(mode==6) cpu.levels.erase(cpu.levels.begin()+2),cpu.levels[2][2]=0x402;
        if(mode==7) cpu.apic=32;
        if(mode==8) cpu.apic=31; // Duplicate APIC ID in two different OS CPUs.
        if(mode==9) cpu.extendedSignature^=1; // Entry gate passes; leaf identity differs.
        if(mode==10) cpu.changeReferenceInputs=true;
        const auto r=compare(d,amd_apob_comparison_fixture::record(),cpus);
        assert(r.error && !r.inputsRechecked && r.candidates.empty() && r.inputs.empty() && r.catalog.cpus.empty());
        const auto json=amdApobComparisonJson(r);
        assert(json.find("\"candidates\":[]")!=std::string::npos && json.find("\"matched\":null")!=std::string::npos);
    }
}
void referenceComparisonFaultsAtEveryRead() {
    const std::vector<unsigned> cpus={130,7}; const auto b=amd_apob_comparison_fixture::record();
    amd_topology_fixture::CatalogDevice baseline(cpus); amd_apob_comparison_fixture::configure(baseline,cpus);
    assert(!compare(baseline,b,cpus,true).error);
    for(unsigned i=0;i<baseline.calls;++i) {
        amd_topology_fixture::CatalogDevice d(cpus); amd_apob_comparison_fixture::configure(d,cpus); d.failAt=i;
        const auto r=compare(d,b,cpus,true);
        assert(r.error==-EACCES && r.catalog.cpus.empty() && r.candidates.empty() && !r.inputsRechecked);
    }
    amd_topology_fixture::CatalogDevice d(cpus); amd_apob_comparison_fixture::configure(d,cpus);
    d.changeCpu=7;
    assert(compare(d,b,cpus,true).error && d.changed);
}
void referenceComparisonAgreesWithOriginalByteOracles() {
    std::vector<unsigned> cpus; for(unsigned i=0;i<32;++i) cpus.push_back(130+3*i);
    for(const auto &v:amd_apob_reference_vectors::cases) {
        auto b=amd_apob_comparison_fixture::record(); b[0x30]=std::uint8_t(v[0]); b[0x63]=std::uint8_t(v[1]);
        for(unsigned g=0;g<2;++g) {
            const unsigned at=48+51*g;
            if(v[2]<8) b[at+2+3*v[2]]=255;
            for(unsigned c=0;c<v[2];++c) b[at+4+3*c]=v[3]==2?1:0;
        }
        amd_topology_fixture::CatalogDevice d(cpus); amd_apob_comparison_fixture::configure(d,cpus);
        const auto r=compare(d,b,cpus);
        assert(!r.error && r.candidates.size()==v[5] && r.matched==v[5] && !r.missingApics);
        for(unsigned i=0;i<v[5];++i) {
            const auto &expected=amd_apob_reference_vectors::rows[v[4]+i]; const auto &c=r.candidates[i];
            assert(c.group==expected[0] && !c.subgroup && c.core==expected[1] && c.thread==expected[2] && c.apicId==expected[3]);
            assert(c.matched && c.cpu==cpus[31-expected[3]]);
        }
    }
}
}
int main() {
    sparseIdsAndFlags(); boundedSingleRecord(); completeSpanAndArbitraryIds();
    sentinelAndAmbiguousRecords(); jsonAndMutationBoundaries();
    completeContainerAndAmbiguity(); malformedContainersNeverExposePartialGroups();
    referenceComparisonPreservesOsIds(); referenceComparisonReportsPartialSets();
    referenceComparisonRejectsRecordsBeforeCpuid(); referenceComparisonRejectsUnsupportedCpuid(); referenceComparisonFaultsAtEveryRead();
    referenceComparisonAgreesWithOriginalByteOracles();
    std::cout << "13 AMD APOB topology scenario groups passed\n";
}
