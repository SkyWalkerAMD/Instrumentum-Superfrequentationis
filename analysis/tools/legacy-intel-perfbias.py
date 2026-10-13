#!/usr/bin/env python3
"""Characterize misleading legacy Intel 'perfbias' entries; never access hardware."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('perfbias_shared',Path(__file__).with_name('legacy-intel-ratio.py'))
shared=importlib.util.module_from_spec(spec); spec.loader.exec_module(shared)
GET='_Z17is_intel_perfbiasv'
DISABLE='_Z18dis_intel_perfbiasv'
ENABLE='_Z17en_intel_perfbiasv'
FUNCTIONS=(GET,DISABLE,ENABLE)
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-intel-perfbias.json'
FIXTURE_SHA='8ac1cf69f6d2082f29cc0c07a544403c3c0b5ca1682007386bbb447b26155b35'
MASKS=(1,1<<31,1<<32,1<<63,(1<<64)-1)


def load_fixture():
    raw=FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA: raise ValueError('perfbias fixture identity mismatch')
    result=json.loads(raw)
    if result['source_elf_sha256']!=shared.base.SOURCE_SHA: raise ValueError('source mismatch')
    return result


class BiasMachine(shared.base.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs); self.writes=[]; self.reads=0; self.mask_indexes=[]; self.topology=None
        self.masks=inputs.get('masks',MASKS)

    def external(self,name):
        self.calls.append(name)
        if name=='_Z5RdmsrjPjS_':
            assert self.reg('rdi')==0x48; self.reads+=1
            if self.inputs.get('supply_data',True):
                self.put32(self.reg('rsi'),self.inputs['raw']); self.put32(self.reg('rdx'),self.inputs['raw']>>32)
            self.ret(self.inputs.get('read_result',1))
        elif name in ('_ZN18My_Processor_CoresC1Ev','_ZN18My_Processor_CoresC2Ev'):
            self.topology=self.reg('rdi'); data=self.alloc(len(self.masks))
            self.put64(self.topology+0x40,data); self.put64(self.topology+0x48,data+len(self.masks)); self.ret(self.topology)
        elif name=='_ZN18My_Processor_Cores18get_Processor_MaskEi':
            assert self.reg('rdi')==self.topology; index=self.reg('rsi'); self.mask_indexes.append(index)
            self.ret(self.masks[index])
        elif name=='_Z7WrmsrTxjjjm':
            assert self.reg('rdi')==0x48
            self.writes.append(dict(raw=(self.reg('rsi')&0xffffffff)|((self.reg('rdx')&0xffffffff)<<32),mask=self.reg('rcx')))
            self.ret(self.inputs.get('write_result',1))
        elif name=='usleep@plt':
            assert self.reg('rdi')==1000; self.ret(0)
        elif name in ('_ZN18My_Processor_CoresD1Ev','_ZN18My_Processor_CoresD2Ev'):
            assert self.reg('rdi')==self.topology; self.ret(0)
        else: raise ValueError('unrecognized perfbias boundary: '+name)


def investigate(fixture):
    rows=[]
    def case(symbol,inputs):
        m=BiasMachine(fixture,inputs); assert m.run(symbol)['outcome']=='returned' and m.reads==1
        raw=inputs['raw'] if inputs.get('supply_data',True) else 0
        if symbol==GET:
            assert m.reg('rax')==((raw>>1)&1) and not m.writes and not m.mask_indexes
        else:
            value=(raw&~2)|(2 if symbol==ENABLE else 0)
            assert m.writes==[dict(raw=value,mask=mask&0xffffffff) for mask in m.masks]
            assert m.mask_indexes==list(range(len(m.masks)))
        rows.append(dict(symbol=symbol,inputs=inputs,writes=m.writes,reads=m.reads))
    for symbol in FUNCTIONS:
        for raw,read_result,write_result in itertools.product([0,(1<<64)-1]+[1<<n for n in range(64)],(0,1),(1,) if symbol==GET else (0,1)):
            case(symbol,dict(raw=raw,read_result=read_result,write_result=write_result))
        for result in (0,1): case(symbol,dict(raw=(1<<64)-1,supply_data=False,read_result=result,write_result=0))
    for symbol in (DISABLE,ENABLE): case(symbol,dict(raw=(1<<64)-1,masks=[]))
    return dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,scenarios=rows,
                scope='Complete original entries with synthetic MSR, topology/mask and delay boundaries. No real CPU enumeration or I/O.',
                findings=['The named perfbias entries read/write MSR 48 bit 1, not MSR 1B0 energy performance bias.',
                          'One MSR sample is copied to every synthetic target, preserving that sample rather than each target register.',
                          'The 64-bit target mask is truncated through EAX -> ECX before WrmsrTx.',
                          'Wrapper failures are ignored; untouched read outputs become zero, then setters broadcast that value.',
                          'No capability check, per-target old-value comparison or readback occurs in these entries.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path); parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--static-output',type=Path); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.binary:
        shared.FUNCTIONS=FUNCTIONS; fixture,proof=shared.extract(args.binary)
        proof['scope']='Three complete original Intel perfbias-named entries.'
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA '+hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else: fixture=load_fixture()
    report=investigate(fixture); args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios']))+' original perfbias scenarios; synthetic I/O and topology only')


if __name__=='__main__': main()
