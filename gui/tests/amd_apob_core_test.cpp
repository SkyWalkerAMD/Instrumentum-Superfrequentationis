// SPDX-License-Identifier: GPL-2.0-only
#include "amd_topology.h"
#include "amd_apob_fixture.h"
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
}
void boundedSingleRecord() {
    const auto good=record();
    for(std::size_t n=0;n<good.size();++n) {
        auto b=good; b.resize(n); const auto t=decodeAmdApobTopology(b);
        assert(t.error==-EINVAL && t.groups.empty());
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
        if(t.error) assert(t.groups.empty() && json.find("\"instance\":null")!=std::string::npos);
        else assert(t.groups.size()<=8);
    }
}
int main() {
    sparseIdsAndFlags(); boundedSingleRecord(); completeSpanAndArbitraryIds();
    sentinelAndAmbiguousRecords(); jsonAndMutationBoundaries();
    std::cout << "5 AMD APOB topology scenario groups passed\n";
}
