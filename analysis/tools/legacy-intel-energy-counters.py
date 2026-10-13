#!/usr/bin/env python3
"""Characterize the original RAPL unit and PP0 counter getters without hardware."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('counter_shared',Path(__file__).with_name('legacy-intel-ratio.py'))
shared=importlib.util.module_from_spec(spec); spec.loader.exec_module(shared)
UNIT='_Z21intel_get_energy_unitv'
COUNTER='_Z18get_current_energyv'
FUNCTIONS={UNIT:0x606,COUNTER:0x639}
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-intel-energy-counters.json'
FIXTURE_SHA='f97c64c2d4879ea550d9592c1f33780b173aaabff44222793f1a6df8721ff009'


def load_fixture():
    raw=FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA: raise ValueError('energy counter fixture identity mismatch')
    result=json.loads(raw)
    if result['source_elf_sha256']!=shared.base.SOURCE_SHA: raise ValueError('source mismatch')
    return result


class CounterMachine(shared.base.Machine):
    def prepare(self,symbol): self.msr=FUNCTIONS[symbol]

    def external(self,name):
        self.calls.append(name)
        assert name=='_Z5RdmsrjPjS_' and self.reg('rdi')==self.msr
        if self.inputs.get('supply_data',True):
            self.put32(self.reg('rsi'),self.inputs['raw'])
            self.put32(self.reg('rdx'),self.inputs['raw']>>32)
        self.ret(self.inputs.get('read_result',1))


def investigate(fixture):
    rows=[]
    patterns=[0,(1<<64)-1]+[1<<n for n in range(64)]+[0xfedcba98a5e00057|(n<<8) for n in range(32)]
    for symbol,raw,status in itertools.product(FUNCTIONS,patterns,(0,1)):
        inputs=dict(raw=raw,read_result=status)
        m=CounterMachine(fixture,inputs); result=m.run(symbol)
        expected=((raw>>8)&31) if symbol==UNIT else raw&0xffffffff
        assert result['outcome']=='returned' and m.reg('rax')==expected and m.calls==['_Z5RdmsrjPjS_']
        rows.append(dict(symbol=symbol,inputs=inputs,value=expected))
    for symbol,raw,status in itertools.product(FUNCTIONS,(0,(1<<64)-1),(0,1)):
        inputs=dict(raw=raw,read_result=status,supply_data=False)
        m=CounterMachine(fixture,inputs)
        assert m.run(symbol)['outcome']=='returned' and m.reg('rax')==0
        rows.append(dict(symbol=symbol,inputs=inputs,value=0))
    return dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,scenarios=rows,
                scope='Two complete original functions with synthetic Rdmsr boundary only; no hardware or original process.',
                findings=['Unit getter returns MSR 606 bits 12:8, an exponent rather than joules.',
                          'get_current_energy reads MSR 639 (PP0), not package MSR 611, and returns the low 32 bits.',
                          'Both getters ignore wrapper failure; untouched outputs become zero.',
                          'Model applicability, other domains and conversion units require separate primary evidence.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--static-output',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.binary:
        shared.FUNCTIONS=FUNCTIONS; fixture,proof=shared.extract(args.binary)
        proof['scope']='Complete original unit and PP0 energy counter functions.'
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA '+hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else: fixture=load_fixture()
    report=investigate(fixture); args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios']))+' original energy counter scenarios; synthetic MSR only')


if __name__=='__main__': main()
