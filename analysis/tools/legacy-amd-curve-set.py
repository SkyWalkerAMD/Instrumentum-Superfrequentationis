#!/usr/bin/env python3
"""Characterize original setters with synthetic PCI/mutex/MP1 boundaries only."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'analysis/fixtures/legacy-amd-curve-set.json'
FIXTURE_SHA = '90e9741891fa9c315bb14081758e1b1abf168d92365e76d17b9118a0dcc85d98'
SOURCE_SHA = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
ALL = '_ZN10AMD_PM_LOG13set_curve_optEi'
SINGLE = '_ZN10AMD_PM_LOG21set_curve_opt_percoreEiii'
OFFSET = '_ZN10AMD_PM_LOG24get_percore_power_offsetEv'
U32 = 0xffffffff


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec); spec.loader.exec_module(value); return value


base = module('curve_set_base', Path(__file__).with_name('emulate-legacy-dispatch.py'))


def load_fixture(path=FIXTURE):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=FIXTURE_SHA: raise ValueError('setter fixture identity mismatch')
    return json.loads(data)


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA: raise ValueError('original ELF identity mismatch')
    elf = ELFFile(io.BytesIO(data)); functions = []
    symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    audit = module('curve_set_audit', Path(__file__).with_name('elf-runtime-audit.py'))
    import re
    report = audit.inspect(path, re.compile('^(?:'+'|'.join((ALL,SINGLE,OFFSET))+')$'), True)
    for name in (ALL, SINGLE, OFFSET):
        symbol = symbols[name]; address = symbol['st_value']; size = symbol['st_size']
        segment = next(s for s in elf.iter_segments() if s['p_type']=='PT_LOAD' and s['p_vaddr'] <= address and address+size <= s['p_vaddr']+s['p_filesz'])
        offset = segment['p_offset']+address-segment['p_vaddr']; code = data[offset:offset+size]
        row = next(f for f in report['selected_functions'] if name in f['symbols'])
        functions.append(dict(symbol=name,address=address,size=size,code_hex=code.hex(),
                              code_sha256=hashlib.sha256(code).hexdigest(),calls=row['calls']))
    return dict(schema=1,source_elf_sha256=SOURCE_SHA,functions=functions,objects={},literals=[])


class Machine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.this = self.alloc(256); self.pci = []; self.messages = []; self.locked = False; self.mp1 = []
        self.index = 0
        # Explicit synthetic object state, not a claim of hardware port discovery.
        fields = {0x28: inputs.get('port',0xf8),0x2c: inputs.get('port',0xf8)+4,0x60:0,
                  0x7c:0xd5,0x80:7,0x84:6,0x88:0x3b10970,0x8c:0x3b10924,0x94:0x3b10a40}
        for offset,value in fields.items(): self.put32(self.this+offset,value)

    def prepare(self, symbol):
        self.reg('rdi',self.this)
        if symbol == SINGLE:
            self.reg('rsi',self.inputs.get('ccd',3)&U32);self.reg('rdx',self.inputs.get('core',7)&U32)
            self.reg('rcx',self.inputs.get('margin',-25)&U32)
        elif symbol == ALL: self.reg('rsi',self.inputs.get('margin',-25)&U32)

    def external(self, name):
        self.calls.append(name); p=self.inputs
        if name in ('_Z14lock_amd_mutexv','_Z17release_amd_mutexv'):
            self.locked = name == '_Z14lock_amd_mutexv'; self.ret()
        elif name == 'usleep@plt':
            assert self.reg('rdi') == 2000; self.ret()
        elif name in ('_Z19WritePciConfigDwordjjj','_Z18ReadPciConfigDwordjj'):
            write = name.startswith('_Z19Write'); port=p.get('port',0xf8)
            bdf,offset = self.reg('rcx'),self.reg('rdx')
            assert bdf==0 and offset in (port,port+4)
            value=self.reg('r8')&U32 if write else p.get('response',1)
            fail=len(self.pci)==p.get('fail_at',-1)
            row=dict(write=write,offset=offset,index=self.index,value=value,locked=self.locked,injected_failure=fail)
            if write and offset==port: self.index=value
            elif write and self.index==0x3b10924: self.messages.append(value)
            self.pci.append(row)
            self.ret((0 if fail else 1) if write else (U32 if fail else value))
        elif name.startswith('_Z10MP1_C2PMSG'):
            assert self.reg('rdi')==0xa8 and self.reg('rsi')==0
            self.mp1.append(dict(command=0xa8,argument=0,synthetic_status=p.get('status',0),synthetic_return=p.get('returned',0x634)))
            self.put32(self.reg('rdx'),p.get('returned',0x634));self.ret(p.get('status',0))
        else: raise ValueError('unexpected boundary '+name)


def investigate(fixture):
    rows=[]
    for symbol in (ALL,SINGLE):
        for port in (0xb8,0xf8):
            for margin in (-50,-25,0,50,0x01234567):
                for ccd,core in ((0,0),(3,7),(15,15),(16,256)):
                    p=dict(port=port,margin=margin,ccd=ccd,core=core)
                    m=Machine(fixture,p); outcome=m.run(symbol)
                    assert outcome['outcome']=='returned' and m.messages==[0xd5,7 if symbol==ALL else 6]
                    assert not m.pci[-1]['write'] and not m.pci[-1]['locked']
                    args=[r['value'] for r in m.pci if r['write'] and r['offset']==port+4 and r['index']==0x3b10a40]
                    expected=margin&U32 if symbol==ALL else ((margin&0x0f0fffff)|(ccd<<28)|((core&15)<<20))&U32
                    assert args==[expected]*3
                    rows.append(dict(symbol=symbol,inputs=p,argument=expected,messages=m.messages,pci=m.pci))
    faults=[]
    for at in range(12):
        m=Machine(fixture,dict(fail_at=at));outcome=m.run(SINGLE)
        assert outcome['outcome']=='returned' and len(m.pci)==12 and m.messages==[0xd5,6]
        faults.append(dict(fail_at=at,pci=m.pci,returned=m.reg('rax')&U32))
    offsets=[]
    for status in (0,1,U32):
        for value in (0,0x634,U32):
            m=Machine(fixture,dict(status=status,returned=value));outcome=m.run(OFFSET)
            assert outcome['outcome']=='returned' and len(m.mp1)==1 and m.reg('rax')&U32==value
            offsets.append(dict(status=status,returned=value,mp1=m.mp1))
    return dict(schema=1,source_elf_sha256=SOURCE_SHA,hardware_execution=False,
                scope='Original instruction execution with synthetic object, PCI, delay, mutex and MP1 boundaries; no constructor port scan or hardware run.',
                setter_cases=rows,fault_cases=faults,offset_query_cases=offsets,
                observations=['Original setter sends D5 then 06/07, writes the argument three times, and reads after unlocking.',
                              'Negative original per-core inputs retain reserved/CCX bits; new low-16-bit encoding follows the pinned public command contract.',
                              'Original calls ignore injected PCI write failures and do not wait for response completion.',
                              'The A8 offset wrapper returns the output word regardless of MP1 status; no runtime query is enabled from this finding.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract',type=Path);parser.add_argument('--fixture',type=Path,default=FIXTURE)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.extract:
        fixture=extract(args.extract)
        args.fixture.write_bytes((json.dumps(fixture,indent=2)+'\n').encode('utf-8'))
    else: fixture=load_fixture(args.fixture)
    assert fixture['source_elf_sha256']==SOURCE_SHA
    report=investigate(fixture)
    report['fixture_sha256']=hashlib.sha256(args.fixture.read_bytes()).hexdigest()
    args.output.write_bytes((json.dumps(report,indent=2)+'\n').encode('utf-8'))
    print(json.dumps({key:len(report[key]) for key in ('setter_cases','fault_cases','offset_query_cases')}))


if __name__=='__main__':main()
