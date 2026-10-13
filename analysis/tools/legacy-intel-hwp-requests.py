#!/usr/bin/env python3
"""Replay the complete original PECI HWP request getter with a synthetic MSR boundary."""
import argparse
import hashlib
import importlib.util
import itertools
from pathlib import Path

spec=importlib.util.spec_from_file_location('hwp_requests_shared',Path(__file__).with_name('legacy-intel-ratio.py'))
shared=importlib.util.module_from_spec(spec); spec.loader.exec_module(shared)
SYMBOL='_Z25Rd_PECI_HWP_REQUEST_Tunerv'
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-intel-hwp-requests.json'
FIXTURE_SHA='6c9fcf56649a3a1731948ff550a2137d11b4027cca467bf30e8a596e3d069fdc'


def load_fixture():
    import json
    raw=FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA: raise ValueError('PECI HWP request fixture identity mismatch')
    result=json.loads(raw)
    if result['source_elf_sha256']!=shared.base.SOURCE_SHA: raise ValueError('source mismatch')
    return result


class RequestMachine(shared.base.Machine):
    def prepare(self,symbol):
        assert symbol==SYMBOL
        # The original leaves its Rdmsr output uninitialized. Seed the caller's
        # future stack slot to distinguish failed reads from valid zero data.
        self.put32(self.reg('rsp')-20,self.inputs.get('stack_low',0xa5e391f7))
        self.put32(self.reg('rsp')-24,self.inputs.get('stack_high',0x87654321))

    def external(self,name):
        self.calls.append(name)
        assert name=='_Z5RdmsrjPjS_' and self.reg('rdi')==0x775
        if self.inputs.get('supply_data',True):
            self.put32(self.reg('rsi'),self.inputs['raw'])
            self.put32(self.reg('rdx'),self.inputs['raw']>>32)
        self.ret(self.inputs.get('read_result',1))


def investigate(fixture):
    rows=[]
    for raw,status in itertools.product([0,(1<<64)-1]+[1<<n for n in range(64)],(0,1)):
        inputs=dict(raw=raw,read_result=status)
        m=RequestMachine(fixture,inputs)
        assert m.run(SYMBOL)['outcome']=='returned' and m.reg('rax')==raw
        assert m.calls==['_Z5RdmsrjPjS_']
        rows.append(dict(inputs=inputs,value=m.reg('rax')))
    for seed,status in itertools.product((0,0xffffffff,0xa5e391f7),(0,1)):
        inputs=dict(raw=0,read_result=status,supply_data=False,stack_low=seed)
        m=RequestMachine(fixture,inputs)
        assert m.run(SYMBOL)['outcome']=='returned' and m.reg('rax')==((0x87654321<<32)|seed)
        assert m.calls==['_Z5RdmsrjPjS_']
        rows.append(dict(inputs=inputs,value=m.reg('rax')))
    return dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,scenarios=rows,
                scope='Complete original getter; synthetic MSR only, no hardware or original process.',
                findings=['Reads MSR 775 and combines both DWORDs into the full 64-bit result.',
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
        shared.FUNCTIONS={SYMBOL:0x775}; fixture,proof=shared.extract(args.binary)
        proof['scope']='Complete original PECI HWP request getter.'
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA '+hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else: fixture=load_fixture()
    report=investigate(fixture); args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios']))+' original PECI HWP request scenarios; synthetic MSR only')


if __name__=='__main__': main()
