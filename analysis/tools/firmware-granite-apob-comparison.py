#!/usr/bin/env python3
"""Generate C++ comparison oracles from the pinned UEFI enumeration bytes.

Both providers, sorter and APIC constructor execute in Unicorn. CPUID is a
pinned public 9950X3D capture; APOB and firmware services are synthetic. The
generated vectors test the shared GUI/CLI comparator without proving a live
record origin, runtime provider or PM/physical mapping.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('capture','provider-a','provider-b','topology','output'):
        p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--fixture',type=Path,default=Path(__file__).resolve().parents[2]/'gui/tests/amd_apob_reference_vectors.h')
    p.add_argument('--write-fixture',action='store_true')
    a=p.parse_args()
    path=Path(__file__).parent
    public=runpy.run_path(str(path/'firmware-granite-published-topology.py'))
    cpus=public['capture'](a.capture)
    h=runpy.run_path(str(path/'firmware-granite-apic-enumeration.py'));common=h['H']
    assert h['u'].__version__=='2.1.4' and h['pefile'].__version__=='2024.8.26'
    topology=common['image'](a.topology,common['TOPOLOGY_SHA'])
    inputs={k:v for k,v in cpus[0]['leaves'].items() if k==(0xb,0) or k[0]==0x80000026}
    reports={};vectors=None
    for key,provider_path in (('a',a.provider_a),('b',a.provider_b)):
        sha,functions=common['PROVIDERS'][key];provider=common['image'](provider_path,sha)
        result_cases=[];rows=[]
        for outer,n,threads in itertools.product(((0,4),(4,0),(8,4),(4,12)),(1,4,8),(1,2)):
            result=h['replay'](provider,topology,functions,outer,(0,),tuple(range(15,15-n,-1)),
                (1,1 if threads==2 else 0),1,True,True,inputs)
            assert not result['duplicate_apics']
            result_cases.append([*outer,n,threads,len(rows),len(result['apics'])])
            for coordinates,apic in zip(result['coordinates'],result['apics']):
                socket,die,group,subgroup,core,thread=coordinates
                assert socket==die==subgroup==0
                rows.append([group,core,thread,apic])
        this=dict(cases=result_cases,rows=rows)
        if vectors is not None: assert this==vectors
        vectors=this
        reports[key]=dict(source_sha256=sha,cases=len(result_cases),output_records=len(rows),
            trace_sha256=hashlib.sha256(json.dumps(this,sort_keys=True).encode()).hexdigest())
    header='''// SPDX-License-Identifier: GPL-2.0-only
// Generated/checked by firmware-granite-apob-comparison.py from original UEFI
// bytes plus public CPUID and synthetic APOB/service inputs. Not a live map.
#pragma once
namespace amd_apob_reference_vectors {
// group stored IDs, core count, prefix threads, result offset, result count.
static const unsigned cases[][6]={
'''
    header+=''.join('    {'+','.join(map(str,row))+'},\n' for row in vectors['cases'])+'};\n'
    header+='// Group ordinal, core ordinal, thread, constructed APIC ID.\nstatic const unsigned rows[][4]={\n'
    header+=''.join('    {'+','.join(map(str,row))+'},\n' for row in vectors['rows'])+'};\n}\n'
    if a.write_fixture:a.fixture.write_text(header,encoding='utf-8',newline='\n')
    assert a.fixture.read_text(encoding='utf-8')==header,'C++ oracle differs from original-byte replay'
    report=dict(schema=1,hardware_execution=False,providers=reports,topology_sha256=common['TOPOLOGY_SHA'],
        capture_sha256=public['SOURCE_SHA'],fixture_sha256=hashlib.sha256(header.encode()).hexdigest(),
        fixture_cases=len(vectors['cases']),fixture_rows=len(vectors['rows']),
        runtime_provider_verified=False,record_origin_verified=False,pm_slot_mapping_verified=False,
        physical_mapping_verified=False,remaining_row_units_verified=False,independent_second_rail_verified=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__=='__main__':main()
