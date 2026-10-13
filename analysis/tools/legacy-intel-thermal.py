#!/usr/bin/env python3
"""Characterize the original default TCC temperature getter without hardware."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('thermal_shared',Path(__file__).with_name('legacy-intel-ratio.py'))
shared=importlib.util.module_from_spec(spec); spec.loader.exec_module(shared)
SYMBOL='_Z30Get_TCC_Activation_Temperaturev'
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-intel-thermal.json'
FIXTURE_SHA='fc6c260d63e5e8b3e7f568a566ec6be47612190adddaeaf47cb7e829bf956637'


def load_fixture():
    raw=FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA: raise ValueError('thermal fixture identity mismatch')
    fixture=json.loads(raw)
    if fixture['source_elf_sha256']!=shared.base.SOURCE_SHA: raise ValueError('source mismatch')
    return fixture


class ThermalMachine(shared.base.Machine):
    def external(self,name):
        self.calls.append(name)
        assert name=='_Z5RdmsrjPjS_' and self.reg('rdi')==0x1a2
        if self.inputs.get('supply_data',True):
            self.put32(self.reg('rsi'),self.inputs['raw'])
            self.put32(self.reg('rdx'),self.inputs['raw']>>32)
        self.ret(self.inputs.get('read_result',1))


def investigate(fixture):
    rows=[]
    patterns=[(0xfedcba98a5001234 | (n<<16)) for n in range(256)]+[1<<n for n in range(64)]
    for raw in patterns:
        for status in (0,1):
            inputs=dict(raw=raw,read_result=status)
            machine=ThermalMachine(fixture,inputs); result=machine.run(SYMBOL)
            assert result['outcome']=='returned' and machine.reg('rax')==((raw>>16)&255)
            assert machine.calls==['_Z5RdmsrjPjS_']
            rows.append(dict(inputs=inputs,default_tcc=machine.reg('rax')))
    for raw in (0,(1<<64)-1):
        for status in (0,1):
            inputs=dict(raw=raw,read_result=status,supply_data=False)
            machine=ThermalMachine(fixture,inputs); result=machine.run(SYMBOL)
            assert result['outcome']=='returned' and machine.reg('rax')==0
            rows.append(dict(inputs=inputs,default_tcc=0))
    return dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,scenarios=rows,
                scope='One complete original function, with only synthetic Rdmsr boundary; no hardware or original process.',
                findings=['The getter returns bits 23:16 of MSR 1A2: default TCC activation temperature.',
                          'It does not subtract the configurable offset and ignores wrapper status.',
                          'A failed wrapper leaving outputs untouched becomes zero, not an error.',
                          'The separate server Get_TJMAX_OFFSET BIOS mailbox function is outside this profile.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--static-output',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.binary:
        shared.FUNCTIONS={SYMBOL:None}; fixture,proof=shared.extract(args.binary)
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA '+hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else: fixture=load_fixture()
    report=investigate(fixture); args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios']))+' original default TCC scenarios; synthetic MSR only')


if __name__=='__main__': main()
