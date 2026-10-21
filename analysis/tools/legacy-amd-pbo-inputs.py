#!/usr/bin/env python3
"""Pinned original Fmax fuse and rail input evidence, with synthetic PCI only."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('pbo_inputs_base', Path(__file__).with_name('legacy-amd-pbo-transactions.py'))
pbo = importlib.util.module_from_spec(spec); spec.loader.exec_module(pbo)
FUSE = '_Z14get_fused_fmaxv'
FIXTURE = ROOT / 'analysis/fixtures/legacy-amd-pbo-fuse.json'
FIXTURE_SHA = '5147f84d3d4cc27b2d523357ced9356fafaeccf1fed11d8aeee8089dfbf85435'

def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA: raise ValueError('Fmax fuse fixture identity mismatch')
    return json.loads(data)

class Machine(pbo.base.Machine):
    def __init__(self, fixture, raw, fail_at=-1):
        super().__init__(fixture, {})
        self.raw, self.fail_at = raw, fail_at
        self.index, self.pci, self.delays = 0, [], []
        self.uc.mem_write(self.objects['GLOBAL_IS_SHIMADA'], b'\1')

    def external(self, name):
        self.calls.append(name)
        if name in ('_Z6is_mtsv', '_Z10is_raph_esv', '_Z13find_pci_dev2jjj'): self.ret(0)
        elif name == 'usleep@plt': self.delays.append(self.reg('rdi')); self.ret()
        elif name in ('_Z19WritePciConfigDwordjjj', '_Z18ReadPciConfigDwordjj'):
            write = name.startswith('_Z19Write')
            bdf, offset = self.reg('rcx'), self.reg('rdx')
            assert bdf == 0 and offset == (0xf8 if write else 0xfc)
            fail = len(self.pci) == self.fail_at
            value = self.reg('r8') if write else (pbo.U32 if fail else self.raw)
            if write: self.index = value
            assert self.index == 0x5d584
            self.pci.append(dict(write=write,offset=offset,index=self.index,value=value,injected_failure=fail))
            self.ret((0 if fail else 1) if write else value)
        else: raise ValueError('Unexpected synthetic boundary ' + name)

    def result(self):
        result = self.run(FUSE); assert result['outcome'] == 'returned'
        return dict(raw=self.raw,fail_at=self.fail_at,returned_mhz=self.reg('rax') & pbo.U32,pci=self.pci,delays=self.delays)

def research():
    fixture = load_fixture()
    fused = [Machine(fixture,(n << 19) | rest).result() for n in range(256) for rest in (0,0xf807ffff)]
    for row in fused: assert row['returned_mhz'] == ((row['raw'] >> 19) & 255) * 25
    faults = [Machine(fixture,200 << 19,n).result() for n in (0,1)]
    assert faults[1]['returned_mhz'] == 6375
    original = pbo.load_fixture(); rails = []
    for a,b in [(0,0)] + [(mv,0) for mv in range(250,2801,5)] + [(0,mv) for mv in range(250,2801,5)] + [(700,1250),(250,2800)]:
        row = pbo.Machine(original,dict(rail0=a,rail1=b)).result(pbo.RAILS)
        args = [r['value'] for r in row['pci'] if r['write'] and r['offset']==0xfc and r['index']==0x3b10a40]
        expected = None if not (a or b) else ((a or b)-245)//5 | ((((b or a)-245)//5) << 17)
        assert args == ([] if expected is None else [expected,0])
        rails.append(dict(rail0_mv=a,rail1_mv=b,encoded=expected))
    return dict(schema=1,source_sha256=pbo.SOURCE_SHA,fixture_sha256=FIXTURE_SHA,
        hardware_execution=False,fused=fused,faults=faults,rails=rails,
        scope='Original instruction behavior only. PCI, lookup, platform predicates and delay are synthetic. Fuse values are not current Fmax readback. Rail packing does not establish firmware or physical rail semantics.')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--extract',type=Path); parser.add_argument('--output',type=Path,required=True); args=parser.parse_args()
    if args.extract:
        pbo.FUNCTIONS = (FUSE,)
        data=(json.dumps(pbo.extract(args.extract),indent=2)+'\n').encode()
        FIXTURE.write_bytes(data)
        print('fixture_sha256',hashlib.sha256(data).hexdigest())
    else:
        report=research(); args.output.write_bytes((json.dumps(report,indent=2)+'\n').encode())
        print(json.dumps({k:len(report[k]) for k in ('fused','faults','rails')}))
