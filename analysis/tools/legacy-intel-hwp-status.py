#!/usr/bin/env python3
"""Replay the complete original HWP status getter with a synthetic MSR boundary."""
import argparse
import hashlib
import importlib.util
import itertools
from pathlib import Path

spec=importlib.util.spec_from_file_location('hwp_status_shared',Path(__file__).with_name('legacy-intel-ratio.py'))
shared=importlib.util.module_from_spec(spec); spec.loader.exec_module(shared)
SYMBOL='_Z19Rd_HWP_STATUS_Tunerv'
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-intel-hwp-status.json'
FIXTURE_SHA='68e65eb375a9af5ca70a5852a934be1360dd114ed62e5125c13da1ee87a283c4'


def load_fixture():
    import json
    raw=FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA: raise ValueError('HWP status fixture identity mismatch')
    result=json.loads(raw)
    if result['source_elf_sha256']!=shared.base.SOURCE_SHA: raise ValueError('source mismatch')
    return result


class StatusMachine(shared.base.Machine):
    def prepare(self,symbol):
        assert symbol==SYMBOL
        # The original leaves its Rdmsr output uninitialized. Seed the caller's
        # future stack slot to distinguish failed reads from valid zero data.
        self.put32(self.reg('rsp')-20,self.inputs.get('stack_low',0xa5e391f7))

    def external(self,name):
        self.calls.append(name)
        assert name=='_Z5RdmsrjPjS_' and self.reg('rdi')==0x777
        if self.inputs.get('supply_data',True):
            self.put32(self.reg('rsi'),self.inputs['raw'])
            self.put32(self.reg('rdx'),self.inputs['raw']>>32)
        self.ret(self.inputs.get('read_result',1))


def investigate(fixture):
    rows=[]
    for raw,status in itertools.product([0,(1<<64)-1]+[1<<n for n in range(64)],(0,1)):
        inputs=dict(raw=raw,read_result=status)
        m=StatusMachine(fixture,inputs)
        assert m.run(SYMBOL)['outcome']=='returned' and m.reg('rax')==raw&0xffffffff
        assert m.calls==['_Z5RdmsrjPjS_']
        rows.append(dict(inputs=inputs,value=m.reg('rax')))
    for seed,status in itertools.product((0,0xffffffff,0xa5e391f7),(0,1)):
        inputs=dict(raw=0,read_result=status,supply_data=False,stack_low=seed)
        m=StatusMachine(fixture,inputs)
        assert m.run(SYMBOL)['outcome']=='returned' and m.reg('rax')==seed
        assert m.calls==['_Z5RdmsrjPjS_']
        rows.append(dict(inputs=inputs,value=m.reg('rax')))
    return dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,scenarios=rows,
                scope='Complete original getter; synthetic MSR only, no hardware or original process.',
                findings=['Reads MSR 777 and returns only the low 32 bits.',
                          'Ignores wrapper failure; untouched output returns caller stack residue.',
                          'Capability and field semantics require architectural documentation.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--static-output',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.binary:
        shared.FUNCTIONS={SYMBOL:0x777}; fixture,proof=shared.extract(args.binary)
        proof['scope']='Complete original HWP status getter.'
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA '+hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else: fixture=load_fixture()
    report=investigate(fixture); args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios']))+' original HWP status scenarios; synthetic MSR only')


if __name__=='__main__': main()
