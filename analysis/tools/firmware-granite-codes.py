#!/usr/bin/env python3
"""Prove integer-code sources and PM storage decomposition; no physical labels."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct

H=runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))
Slice,SHA,ACC=H['Slice'],H['SHA'],H['ACC']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('mp5','study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();body=a.mp5.read_bytes();assert hashlib.sha256(body).hexdigest()==SHA
    study=json.loads(a.study.read_text());assert study['source_sha256']==SHA
    code={int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
    for address,instruction in code.items():
        raw=bytes.fromhex(instruction['bytes']);assert body[address:address+len(raw)]==raw
    cases=words=0;trace=hashlib.sha256()
    for width,ordinal in ((1,0),(2,0),(2,1)):
        for initial in range(256):
            vm=Slice(body,code);H['seed_accumulation'](vm)
            expected=[[0.0]*8 for _ in range(2)];ccd=0.0
            for sample in range(4):
                for core in range(8):
                    first=(initial+core+sample)&255;second=(255-initial+core+2*sample)&255
                    vm.put(0xeb28+0x520*core+2,first,1)
                    vm.put(0x15884+0x2c+core,second,1)
                    expected[0][core]+=first/4;expected[1][core]+=second/4
                upper=(initial*257+sample)&65535;ccd+=upper/4
                # Distinct lower bits must not affect CCD row 7.
                vm.put(0x12f2c+4,(upper<<16)|(0xffff if sample%2 else 0x1234))
                vm.run(0x2f370,0x2f536)
            values=H['export_snapshot'](vm,width,ordinal)
            for row,series in zip((8,11),expected):
                for core,value in enumerate(series):
                    assert values[row*8+core]==value
                    flat_slot=ordinal*8+core
                    assert flat_slot//8==ordinal and flat_slot%8==core
                    trace.update(struct.pack('<5If',width,ordinal,initial,row,flat_slot,value));words+=1
            assert values[144+7]==ccd
            trace.update(struct.pack('<4If',width,ordinal,initial,7,ccd));words+=1;cases+=1
    report=dict(schema=1,source_sha256=SHA,hardware_execution=False,
        cases=cases,checked_code_words=words,samples_per_case=4,
        core_code_rows={'8':{'source':'byte[0xeb28 + 0x520 * local_slot + 2]','bits':8},
                        '11':{'source':'byte[0x15884 + 0x2c + local_slot]','bits':8}},
        ccd_code_rows={'7':{'source':'word[0x12f30] >> 16','bits':16}},
        unit='code',fractional_average_possible=True,additional_scaling=False,
        code_meanings_verified=False,code_is_os_core_identifier=False,
        storage_decomposition='storage CCD slot = flat core slot / 8; local core slot = flat core slot % 8',
        pm_physical_ccd_mapping_verified=False,pm_os_cpu_mapping_verified=False,
        trace_sha256=trace.hexdigest())
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__=='__main__':main()
