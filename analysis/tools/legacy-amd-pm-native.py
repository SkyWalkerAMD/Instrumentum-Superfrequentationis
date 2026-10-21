#!/usr/bin/env python3
"""REA-guided PM address/transfer and VID helper replay, without hardware."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('native_oc', Path(__file__).with_name('legacy-amd-oc-frequency.py'))
oc = importlib.util.module_from_spec(spec); spec.loader.exec_module(oc)
START = '_ZN10AMD_PM_LOG13start_monitorEv'
BASE32 = '_ZN10AMD_PM_LOG15get_mem_addressEv'
BASE64 = '_ZN10AMD_PM_LOG20get_mem_address64bitEv'
VOLTAGE = '_ZN17AMD_Configuration16cal_vid_from_rawEi'
VID = '_ZN17AMD_Configuration16cal_raw_from_vidEi'
FUNCTIONS = (START, BASE32, BASE64, VOLTAGE, VID)
FIXTURE = oc.ROOT / 'analysis/fixtures/legacy-amd-pm-native.json'
FIXTURE_SHA = 'a4013f7ded45a892a3e24ccde1a9198497add06240d0180ce923aa63ed76f60a'


def extract(path):
    oc.FUNCTIONS = FUNCTIONS
    return oc.extract(path)


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA: raise ValueError('Native PM fixture identity mismatch')
    return json.loads(data)


class Machine(oc.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.this = self.alloc(256); self.pci = []; self.index = 0; self.locked = False
        # Explicit synthetic object state, NOT a replayed constructor or proof
        # that this object state is selected on a real Shimada machine.
        for offset,value in ((0x28,0xb8),(0x2c,0xbc),(0x64,2000),(0x88,0x3b10970),
                             (0x8c,0x3b10924),(0x90,3),(0x94,0x3b10a40),(0x98,5),(0x9c,4)):
            self.put32(self.this+offset,value)
        for offset in (0x79,0x7a): self.uc.mem_write(self.this+offset,bytes([inputs.get(hex(offset),0)]))

    def prepare(self, symbol):
        self.reg('rdi',self.this); self.reg('rsi',self.inputs.get('value',0) & 0xffffffff)

    def external(self, name):
        if name == '_Z14lock_amd_mutexv': self.locked = True; self.ret(); return
        if name == '_Z17release_amd_mutexv': self.locked = False; self.ret(); return
        if name == 'usleep@plt': self.ret(); return
        if name in ('_Z19WritePciConfigDwordjjj','_Z18ReadPciConfigDwordjj'):
            write = name.startswith('_Z19Write'); bdf,offset = self.reg('rcx'),self.reg('rdx')
            assert bdf == 0 and offset in (0xb8,0xbc), (bdf,offset)
            fail = len(self.pci) == self.inputs.get('fail_at',-1)
            value = self.reg('r8') & 0xffffffff if write else {
                0x3b10a40:self.inputs.get('low',0x23456000),
                0x3b10a44:self.inputs.get('high',1),0x3b10970:self.inputs.get('response',0xfd)
            }.get(self.index,0xdeadbeef)
            self.pci.append(dict(write=write,offset=offset,index=self.index,value=value,locked=self.locked,failure=fail))
            if write and offset == 0xb8 and not fail: self.index = value
            self.ret((0 if fail else 1) if write else (0xffffffff if fail else value)); return
        super().external(name)

    def result(self, symbol):
        result = self.run(symbol); assert result['outcome'] == 'returned'
        return dict(symbol=symbol,inputs=self.inputs,pci=self.pci,returned=self.reg('rax'))


def investigate(fixture):
    transport = []; faults = []; conversions = []
    for symbol in (START,BASE32,BASE64):
        for flag79,flag7a in itertools.product((0,1),repeat=2):
            for low,high in ((0,0),(0x23456000,1),(0xffffffff,0xffffffff)):
                transport.append(Machine(fixture,{'0x79':flag79,'0x7a':flag7a,'low':low,'high':high}).result(symbol))
        baseline = Machine(fixture,{}).result(symbol)
        for i in range(len(baseline['pci'])): faults.append(Machine(fixture,dict(fail_at=i)).result(symbol))
    for am5,granite,shimada,gpt in itertools.product((0,1),repeat=4):
        flags = dict(am5_es=am5,GLOBAL_IS_GRANITE=granite,GLOBAL_IS_SHIMADA=shimada,GLOBAL_IS_GPT=gpt)
        for value in (-1,0,1,244,245,250,700,1250,1520,2400,2800):
            for symbol in (VOLTAGE,VID): conversions.append(Machine(fixture,dict(flags,value=value)).result(symbol))
    for shimada in (0,1):
        for value in range(512):
            conversions.append(Machine(fixture,dict(am5_es=1,GLOBAL_IS_GRANITE=0,GLOBAL_IS_SHIMADA=shimada,GLOBAL_IS_GPT=0,value=value)).result(VOLTAGE))
    return dict(schema=1,source_elf_sha256=oc.pbo.SOURCE_SHA,hardware_execution=False,
        scope='Full pinned address/transfer and conversion helper instructions. Explicit synthetic PM object, PCI reads/writes, global flags, is_am5_es, locks and sleep. Read failure returns a synthetic FFFFFFFF sentinel. No real firmware, constructor selection or electrical contract is certified.',
        transport=transport,faults=faults,conversions=conversions)


if __name__ == '__main__':
    p=argparse.ArgumentParser(); p.add_argument('--extract',type=Path); p.add_argument('--output',type=Path,required=True); a=p.parse_args()
    if a.extract:
        fixture=extract(a.extract); FIXTURE.write_bytes((json.dumps(fixture,indent=2)+'\n').encode('utf-8'))
    else: fixture=load_fixture()
    report=investigate(fixture); report['fixture_sha256']=hashlib.sha256(FIXTURE.read_bytes()).hexdigest()
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(fixture_sha256=report['fixture_sha256'],counts={k:len(report[k]) for k in ('transport','faults','conversions')})))
