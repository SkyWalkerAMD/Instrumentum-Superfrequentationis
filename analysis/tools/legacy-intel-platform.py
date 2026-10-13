#!/usr/bin/env python3
"""Characterize the original maximum non-turbo ratio getter without hardware."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('platform_shared',Path(__file__).with_name('legacy-intel-ratio.py'))
shared=importlib.util.module_from_spec(spec); spec.loader.exec_module(shared)
SYMBOL='_Z23MAX_NON_TURBO_LIM_RATIOv'
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-intel-platform.json'
FIXTURE_SHA='88bf44b14af0332ee28a7c877768cd04d57fd481285157f3ee33197bd0fa3b16'


def load_fixture():
    raw=FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA: raise ValueError('platform fixture identity mismatch')
    fixture=json.loads(raw)
    if fixture['source_elf_sha256']!=shared.base.SOURCE_SHA: raise ValueError('source mismatch')
    return fixture


class PlatformMachine(shared.base.Machine):
    def external(self,name):
        self.calls.append(name)
        assert name=='_Z5RdmsrjPjS_' and self.reg('rdi')==0xce
        if self.inputs.get('supply_data',True):
            self.put32(self.reg('rsi'),self.inputs['raw'])
            self.put32(self.reg('rdx'),self.inputs['raw']>>32)
        self.ret(self.inputs.get('read_result',1))


def investigate(fixture):
    rows=[]
    patterns=[(0xfedcba98a5000034 | (n<<8)) for n in range(256)]+[1<<n for n in range(64)]
    for raw in patterns:
        for status in (0,1):
            inputs=dict(raw=raw,read_result=status)
            machine=PlatformMachine(fixture,inputs); result=machine.run(SYMBOL)
            assert result['outcome']=='returned' and machine.reg('rax')==((raw>>8)&255)
            assert machine.calls==['_Z5RdmsrjPjS_']
            rows.append(dict(inputs=inputs,maximum_non_turbo_ratio=machine.reg('rax')))
    for raw in (0,(1<<64)-1):
        for status in (0,1):
            inputs=dict(raw=raw,read_result=status,supply_data=False)
            machine=PlatformMachine(fixture,inputs); result=machine.run(SYMBOL)
            assert result['outcome']=='returned' and machine.reg('rax')==0
            rows.append(dict(inputs=inputs,maximum_non_turbo_ratio=0))
    return dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,scenarios=rows,
                scope='One complete original function, with only synthetic Rdmsr boundary; no hardware or original process.',
                findings=['The getter returns bits 15:8 of MSR CE: maximum non-turbo ratio.',
                          'It ignores MSR wrapper status; a failed wrapper leaving outputs untouched becomes zero.',
                          'This fixed platform ratio is not a measurement of current core frequency.'])


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
    print(str(len(report['scenarios']))+' original non-turbo ratio scenarios; synthetic MSR only')


if __name__=='__main__': main()
