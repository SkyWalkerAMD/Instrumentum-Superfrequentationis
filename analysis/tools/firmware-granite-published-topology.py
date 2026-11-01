#!/usr/bin/env python3
"""Cross-check REA's APIC consumer with a pinned public 9950X3D CPUID capture.

The capture is from X670E-E BIOS 2904, not the user's X870E Creator. APOB,
configuration and services remain synthetic. No hardware or firmware boots.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import runpy

SOURCE_COMMIT = '895c35aba50508c64cd38d8892fe98b0954142d7'
SOURCE_PATH = 'AuthenticAMD/AuthenticAMD0B40F40_K20_GraniteRidge_06_CPUID.txt'
SOURCE_URL = 'https://raw.githubusercontent.com/InstLatx64/InstLatx64/' + SOURCE_COMMIT + '/' + SOURCE_PATH
SOURCE_SHA = '3f4f76ee0440b141b1a8f180ccaa7b0e2662607df622320cfab0b00e468d3279'


def capture(path):
    raw=path.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==SOURCE_SHA, 'Unexpected public capture'
    blocks=re.split(r'------\[ CPUID Registers / Logical CPU #(\d+) \]------',raw.decode('utf-8'))
    cpus=[]
    for i in range(1,len(blocks),2):
        leaves={}
        for match in re.finditer(r'^CPUID ([0-9A-F]{8}): ([0-9A-F]{8}(?:-[0-9A-F]{8}){3})(?: \[SL ([0-9A-F]{2})\])?',blocks[i+1],re.M):
            leaf=int(match[1],16);sub=int(match[3] or '0',16)
            if leaf not in (1,0xb,0x8000001d,0x80000026):continue
            assert (leaf,sub) not in leaves
            leaves[(leaf,sub)]=tuple(int(w,16) for w in match[2].split('-'))
        cpus.append(dict(index=int(blocks[i]),leaves=leaves))
    assert len(cpus)==32 and [c['index'] for c in cpus]==list(range(32))
    for c in cpus:
        l=c['leaves'];i=c['index']
        assert l[(1,0)][0]==0x00b40f40
        assert l[(0xb,0)]==(1,2,0x100,i)
        assert [l[(0x80000026,s)] for s in range(4)]==[
            (1,2,0x100,i),(4,16,0x201,i),(4,16,0x302,i),(5,32,0x403,i)]
        assert l[(0x8000001d,3)]==(0x3c163,0x3c0003f,0x17fff if i<16 else 0x7fff,1)
        for s in range(3):assert l[(0x8000001d,s)]==cpus[0]['leaves'][(0x8000001d,s)]
    return cpus


def fixture(cpus):
    l=cpus[0]['leaves']
    words=lambda v:','.join('0x%xu'%x for x in v)
    levels=','.join('{{'+words(l[(0x80000026,i)][:3])+'}}' for i in range(4))
    caches=','.join('{{'+words(l[(0x8000001d,i)])+'}}' for i in range(4))
    # Only the register words consumed by the topology decoder are copied.
    # Identity scaffolding, terminators and OS CPU selection stay synthetic.
    return '''// SPDX-License-Identifier: GPL-2.0-only
// Generated/checked by analysis/tools/firmware-granite-published-topology.py.
// Public register facts: InstLatx64 commit '''+SOURCE_COMMIT+'''.
// X670E-E / BIOS 2904. See docs/amd-granite-published-topology.md.
// Capture CPU indices are NOT the user's OS CPU IDs. Leaf terminators and
// backend identity scaffolding are synthetic; no live platform is certified.
#pragma once
#include "amd_topology_fixture.h"
namespace amd_topology_fixture {
inline void published9950x3d(Device &d,unsigned captureCpu) {
    assert(captureCpu<32);
    d.signature=0x00b40f40; d.apic=captureCpu;
    d.levels={'''+levels+'''};
    d.caches={'''+caches+'''};
    if(captureCpu>=16) d.caches[3][2]=0x7fff;
}
}
'''


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('capture','provider-a','provider-b','topology','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--fixture',type=Path,default=Path(__file__).resolve().parents[2]/'gui/tests/amd_9950x3d_topology_fixture.h')
    p.add_argument('--write-fixture',action='store_true')
    a=p.parse_args();cpus=capture(a.capture);header=fixture(cpus)
    if a.write_fixture:a.fixture.write_text(header,encoding='utf-8',newline='\n')
    assert a.fixture.read_text(encoding='utf-8')==header, 'C++ fixture differs from public register facts'
    h=runpy.run_path(str(Path(__file__).with_name('firmware-granite-apic-enumeration.py')))
    assert h['u'].__version__=='2.1.4' and h['pefile'].__version__=='2024.8.26'
    common=h['H'];topology=common['image'](a.topology,common['TOPOLOGY_SHA']);reports={}
    for key,path in (('a',a.provider_a),('b',a.provider_b)):
        sha,functions=common['PROVIDERS'][key];provider=common['image'](path,sha)
        trace=hashlib.sha256();cases=records=0
        for cpu in cpus:
            inputs={k:v for k,v in cpu['leaves'].items() if k==(0xb,0) or k[0]==0x80000026}
            for outer in ((0,4),(4,0),(8,4),(4,12)):
                for x2 in (False,True):
                    result=h['replay'](provider,topology,functions,outer,(0,),tuple(range(15,7,-1)),
                                       (1,1),1,True,x2,inputs)
                    assert sorted(result['apics'])==list(range(32)) and not result['duplicate_apics']
                    observable={k:v for k,v in result.items() if k!='instructions'}
                    trace.update(json.dumps([cpu['index'],outer,x2,observable],sort_keys=True).encode())
                    cases+=1;records+=len(result['apics'])
        reports[key]=dict(source_sha256=sha,cases=cases,output_records=records,trace_sha256=trace.hexdigest())
        print('Completed public-CPUID replay for provider',key,':',cases,'cases',flush=True)
    assert reports['a']['trace_sha256']==reports['b']['trace_sha256']
    result=dict(schema=1,hardware_execution=False,source_url=SOURCE_URL,source_sha256=SOURCE_SHA,
        source_board='ASUS ROG Strix X670E-E Gaming WiFi',source_bios='2904',captured_logical_cpus=32,
        levels=[dict(type=i,shift=s,logical_count=n) for i,s,n in ((1,1,2),(2,4,16),(3,4,16),(4,5,32))],
        l3_groups=[dict(capture_cpu_indices=list(range(16)),bytes=100663296),
                   dict(capture_cpu_indices=list(range(16,32)),bytes=33554432)],
        redundant_type3=True,providers=reports,topology_sha256=common['TOPOLOGY_SHA'],
        fixture_sha256=hashlib.sha256(header.encode()).hexdigest(),
        user_platform_verified=False,runtime_apob_verified=False,pm_slot_mapping_verified=False,
        independent_second_rail_verified=False,remaining_row_units_verified=False,
        limitations=['Public CPUID capture is from a different board and BIOS.',
          'The published capture omits terminating subleaves; C++ fixture termination is synthetic.',
          'APOB records, protocol lookup, fabric counts, proximity and configuration remain fixtures.',
          'Matching APIC sets validate the recovered formula under these inputs, not the actual PM-to-core path.'])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':main()
