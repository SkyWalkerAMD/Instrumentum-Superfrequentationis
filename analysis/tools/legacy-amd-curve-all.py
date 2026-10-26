#!/usr/bin/env python3
"""Original all-core curve and Force/UnForce paths; synthetic PCI only."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('all_pbo', Path(__file__).with_name('legacy-amd-pbo-transactions.py'))
pbo = importlib.util.module_from_spec(spec); spec.loader.exec_module(pbo)
ALL = '_ZN7AMD_PBO13set_curve_optEi'
FORCE = '_Z28TEST_SMC_MSG_ForceAllDLDOPsmjRj'
UNFORCE = '_Z30TEST_SMC_MSG_UnForceAllDLDOPsmRj'
UNVID = '_Z26TEST_SMC_MSG_UnForceCpuVidRj'
FUNCTIONS = (pbo.CTOR, ALL, FORCE, UNFORCE, UNVID)
FIXTURE = pbo.ROOT / 'analysis/fixtures/legacy-amd-curve-all.json'
FIXTURE_SHA = 'f42f4a992721765853aaf7cbf221414075906369877b00b38797e3fadf4ab93f'


def extract(path):
    pbo.FUNCTIONS = FUNCTIONS
    return pbo.extract(path)


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA: raise ValueError('All-core curve fixture identity mismatch')
    return json.loads(data)


class Machine(pbo.Machine):
    def __init__(self, fixture, inputs):
        inputs = dict(inputs)
        inputs.setdefault('argument_address', 0x3b10a88 if inputs.get('device') in (0x1630,0x1122) else 0x3b10a40)
        super().__init__(fixture, inputs)

    def prepare(self, symbol):
        self.active_symbol = symbol
        if symbol == ALL:
            self.reg('rdi', self.this); self.reg('rsi', self.inputs.get('margin', -25) & pbo.U32)
        elif symbol == FORCE:
            self.reg('rdi', self.inputs.get('margin', -25) & pbo.U32); self.reg('rsi', self.output_word)
        elif symbol in (UNFORCE, UNVID): self.reg('rdi', self.output_word)
        else: super().prepare(symbol)

    def external(self, name):
        if name == '_Z13find_pci_dev2jjj' and self.active_symbol != pbo.CTOR:
            # This helper's last-resort call asks for a BDF, unlike the PBO
            # constructor's device-ID lookup. Explicit synthetic domain-0 BDF 0.
            self.searches.append([self.reg('rdi'),self.reg('rsi'),self.reg('rdx')])
            self.ret(0)
        else: super().external(name)

    def observe(self, symbol):
        if symbol == ALL:
            assert self.run(pbo.CTOR)['outcome'] == 'returned'
            self.pci.clear(); self.searches.clear(); self.delays.clear()
        self.put32(self.output_word, 0xaabbccdd)
        out = self.result(symbol)
        out['output_word'] = int.from_bytes(self.uc.mem_read(self.output_word,4),'little')
        return out


def investigate(fixture):
    settings=[]; releases=[]; faults=[]
    for margin in (-2147483648,-65536,-51,-50,-25,-1,0,1,50,51,65536,2147483647):
        settings.append(Machine(fixture,dict(margin=margin)).observe(ALL))
    for symbol in (FORCE,UNFORCE,UNVID):
        for device,shimada,gpt in ((0x153a,1,0),(0x1630,0,0),(0x1480,0,0),(0x1122,0,1)):
            for response in (0,1,0xfd,0xffffffff):
                releases.append(Machine(fixture,dict(device=device,GLOBAL_IS_SHIMADA=shimada,
                    GLOBAL_IS_GPT=gpt,response=response,returned=0x12345678)).observe(symbol))
    for symbol in (ALL,FORCE,UNFORCE,UNVID):
        baseline=Machine(fixture,{}).observe(symbol)
        for index in range(len(baseline['pci'])):
            faults.append(Machine(fixture,dict(fail_at=index,response=0xfd)).observe(symbol))
    return dict(schema=1,source_elf_sha256=pbo.SOURCE_SHA,fixture_sha256=hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),
        hardware_execution=False,scope='Complete original functions and PBO constructor. Explicit synthetic PCI search results, PCI values/failure sentinels, locks and sleep. No firmware or physical effects are validated.',
        settings=settings,releases=releases,faults=faults)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--extract',type=Path); parser.add_argument('--output',type=Path,required=True); args=parser.parse_args()
    if args.extract:
        data=(json.dumps(extract(args.extract),indent=2)+'\n').encode('utf-8'); FIXTURE.write_bytes(data)
        print('fixture_sha256',hashlib.sha256(data).hexdigest())
    else:
        report=investigate(load_fixture()); args.output.write_bytes((json.dumps(report,indent=2)+'\n').encode('utf-8'))
        print(json.dumps({k:len(report[k]) for k in ('settings','releases','faults')}))
