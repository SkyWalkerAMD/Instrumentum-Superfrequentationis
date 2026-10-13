#!/usr/bin/env python3
"""Characterize complete original limit history helpers with synthetic MSR calls."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('limits_shared',Path(__file__).with_name('legacy-intel-ratio.py'))
shared=importlib.util.module_from_spec(spec); spec.loader.exec_module(shared)
READERS={'_Z16get_clipping_logv':(0x64f,31),'_Z21get_hedt_clipping_logv':(0x1b1,14)}
CLEAR='_Z18clear_clipping_logv'
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-intel-limits.json'
FIXTURE_SHA='031c4a0c397789ed263879c06dbb9cd567396a04c5c0344e3ce3e85b297c35a3'


def load_fixture():
    raw=FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA: raise ValueError('limit fixture identity mismatch')
    fixture=json.loads(raw)
    if fixture['source_elf_sha256']!=shared.base.SOURCE_SHA: raise ValueError('source mismatch')
    return fixture


class LimitsMachine(shared.base.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs)
        self.trace=[]

    def external(self,name):
        self.calls.append(name)
        if name=='_Z5RdmsrjPjS_':
            self.trace.append(dict(read=self.reg('rdi')))
            if self.inputs.get('supply_data',True):
                self.put32(self.reg('rsi'),self.inputs['raw'])
                self.put32(self.reg('rdx'),self.inputs['raw']>>32)
            self.ret(self.inputs.get('read_result',1))
        elif name=='_Z5Wrmsrjjj':
            self.trace.append(dict(write=self.reg('rdi'),value=((self.reg('rdx')&0xffffffff)<<32)|(self.reg('rsi')&0xffffffff)))
            self.ret(self.inputs.get('write_result',1))
        elif name.startswith('_ZNSt6vectorIbSaIbEE13_M_insert_aux'):
            obj=self.reg('rdi')
            assert self.reg('rsi')==0 and self.reg('rdx')==0 and self.u64(obj)==0
            ptr=self.alloc(8); self.put64(ptr,self.reg('rcx')&1)
            self.put64(obj,ptr); self.put32(obj+8,0)
            self.put64(obj+16,ptr); self.put32(obj+24,1); self.put64(obj+32,ptr+8)
            self.ret()
        else: raise ValueError('unexpected boundary: '+name)

    def result(self,count):
        assert self.reg('rax')==self.output and self.u64(self.output+16)==self.u64(self.output)
        assert int.from_bytes(self.uc.mem_read(self.output+24,4),'little')==count
        return [bool(self.u64(self.u64(self.output)) & (1<<n)) for n in range(count)]


def investigate(fixture):
    rows=[]; patterns=[0,(1<<64)-1,0xa5c3211087654321]+[1<<n for n in range(64)]
    for symbol,(msr,count) in READERS.items():
        for raw in patterns:
            for status in (0,1):
                inputs=dict(raw=raw,read_result=status)
                m=LimitsMachine(fixture,inputs); result=m.run(symbol)
                expected=[bool(raw & (1<<n)) for n in range(count)]
                assert result['outcome']=='returned' and m.result(count)==expected and m.trace==[dict(read=msr)]
                rows.append(dict(symbol=symbol,inputs=inputs,trace=m.trace,flags=m.result(count)))
        for status in (0,1):
            inputs=dict(raw=(1<<64)-1,read_result=status,supply_data=False)
            m=LimitsMachine(fixture,inputs); result=m.run(symbol)
            assert result['outcome']=='returned' and not any(m.result(count))
            rows.append(dict(symbol=symbol,inputs=inputs,trace=m.trace,flags=m.result(count)))
    for status in (0,1):
        m=LimitsMachine(fixture,dict(write_result=status)); result=m.run(CLEAR)
        assert result['outcome']=='returned' and m.trace==[dict(write=0x64f,value=0)]
        rows.append(dict(symbol=CLEAR,write_result=status,trace=m.trace))
    return dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,scenarios=rows,
                scope='Three complete original functions, allowlisted instructions, synthetic MSR/vector boundaries only; no hardware.',
                findings=['Client reader exposes bits 0..30 from MSR 64F; server reader exposes bits 0..13 from MSR 1B1.',
                          'Both readers ignore wrapper errors; untouched outputs become an all-false vector.',
                          'Original clear helper writes zero to all of MSR 64F without reading, preserving reserved bits or verifying.',
                          'Bit meaning, W0C semantics and supported models require independent Intel SDM evidence.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--static-output',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.binary:
        shared.FUNCTIONS=dict.fromkeys(tuple(READERS)+(CLEAR,)); fixture,proof=shared.extract(args.binary)
        proof['scope']='Complete client/server getter and client clear helper; function bytes pinned to original ELF.'
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA '+hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else: fixture=load_fixture()
    report=investigate(fixture); args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios']))+' original limit history scenarios; synthetic MSR only')


if __name__=='__main__': main()
