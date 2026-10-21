#!/usr/bin/env python3
"""REA-guided original dynamic-memory and EC-EDC instruction characterization.

The two selected function bodies and the reused PBO constructor execute in
Unicorn. Qt, PCI, EC, timers, the dynamic-EDC refresh callback, and hardware
identity are synthetic boundaries.
No board contract, physical units or real setting success is inferred.
"""
import argparse
import copy
import hashlib
import io
import itertools
import json
from pathlib import Path
import re
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
import importlib.util

ROOT = Path(__file__).resolve().parents[2]
MEM = '_ZN7AMD_PBO18toggle_dynamic_memEb'
EDC = '_ZN7dyn_edc13apply_clickedEv'
FIXTURE = ROOT / 'analysis/fixtures/legacy-amd-dynamic-controls.json'
FIXTURE_SHA = '6e7eb0aed033d0bfeb5b902640a94a4cdc682e1c71f87d0de69bbd07dac3c1cd'
FIELD_OFFSETS = (0x70, 0x88, 0xa0, 0xb8, 0xd0)


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec); spec.loader.exec_module(result); return result


pbo = module('dynamic_pbo', 'legacy-amd-pbo-transactions.py')
base = pbo.base


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != pbo.SOURCE_SHA:
        raise ValueError('Unexpected original ELF')
    elf = ELFFile(io.BytesIO(data))
    def read(address, size):
        segment = next(s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and s['p_vaddr'] <= address and address + size <= s['p_vaddr'] + s['p_filesz'])
        off = segment['p_offset'] + address - segment['p_vaddr']; return data[off:off+size]
    audit = module('dynamic_audit', 'elf-runtime-audit.py').inspect(path, re.compile('^(?:' + MEM + '|' + EDC + ')$'), True)
    fixture = dict(schema=1, source_elf_sha256=pbo.SOURCE_SHA, pbo_fixture_sha256=pbo.FIXTURE_SHA,
                   functions=[], objects={}, literals=[])
    decoder = Cs(CS_ARCH_X86, CS_MODE_64); decoder.detail = True
    literals = {}
    for fn in audit['selected_functions']:
        name = next(s for s in fn['symbols'] if s in (MEM, EDC))
        code = read(fn['address'], fn['size'])
        fixture['functions'].append(dict(symbol=name, address=fn['address'], size=fn['size'],
            code_hex=code.hex(), code_sha256=hashlib.sha256(code).hexdigest(), calls=fn['calls']))
        for ins in decoder.disasm(code, fn['address']):
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    address = ins.address + ins.size + op.mem.disp
                    text = read(address,256).split(b'\0')[0].decode('utf-8')
                    if not text or not text.isprintable(): raise ValueError('Unclassified RIP-relative data')
                    literals[address] = text
    fixture['literals'] = [dict(address=a,text=t) for a,t in sorted(literals.items())]
    assert len(fixture['functions']) == 2
    return fixture


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA: raise ValueError('Dynamic fixture changed')
    return json.loads(data)


class MemoryMachine(pbo.Machine):
    def __init__(self, fixture, inputs):
        assert fixture['pbo_fixture_sha256'] == pbo.FIXTURE_SHA
        combined = copy.deepcopy(pbo.load_fixture())
        combined['functions'] += fixture['functions']; combined['literals'] += fixture['literals']
        super().__init__(combined, inputs)

    def prepare(self, symbol):
        if symbol == MEM:
            self.reg('rdi',self.this); self.reg('rsi',self.inputs['enabled'])
        else: super().prepare(symbol)


class EdcMachine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.this = self.alloc(0x180); self.ec = []; self.current = None
        self.widgets = {}; self.refreshes = 0; self.info_shown = False
        self.put32(self.this+0x38,inputs['row'])
        self.uc.mem_write(self.this+0x31,bytes([inputs['initial_control'] & 1]))
        self.put64(self.this+0x150,self.alloc(16))
        for offset in (0x40,0x58,*FIELD_OFFSETS,0x100):
            data = self.alloc(24)
            self.put64(self.this+offset,data); self.put64(self.this+offset+8,data+24); self.put64(self.this+offset+16,data+24)
            for row in range(3):
                widget = self.alloc(16); self.put64(data+8*row,widget); self.widgets[widget]=(offset,row)

    def prepare(self, symbol):
        assert symbol == EDC; self.reg('rdi',self.this)

    def external(self, name):
        if name in ('_ZN2EC8Rd_ECRAMEhh','_ZN2EC8Wr_ECRAMEhhh'):
            assert self.reg('rdi') == self.this+0x30 and self.reg('rdx') == 1
            address = self.reg('rsi'); write = 'Wr_' in name
            assert 0xc1 <= address <= 0xd5
            value = self.reg('rcx') & 255 if write else self.inputs['initial_control']
            dropped = write and len(self.ec) == self.inputs.get('discard_at',-1)
            self.ec.append(dict(write=write,address=address,bank=1,value=value,discarded=dropped))
            self.ret(0 if dropped else 1 if write else value)
        elif name == '_ZNK9QComboBox12currentIndexEv':
            offset,row = self.widgets[self.reg('rdi')]; assert row == self.inputs['row']
            self.ret(self.inputs['mode' if offset == 0x40 else 'policy'])
        elif name == '_ZNK15QAbstractButton9isCheckedEv':
            assert self.widgets[self.reg('rdi')] == (0x100,self.inputs['row'])
            self.ret(self.inputs['enabled'])
        elif name == '_ZNK9QLineEdit4textEv':
            offset,row = self.widgets[self.reg('rsi')]; assert row == self.inputs['row']
            self.current = FIELD_OFFSETS.index(offset)
            descriptor = self.alloc(32); self.put32(descriptor,-1); self.put64(self.reg('rdi'),descriptor); self.ret()
        elif name == '_ZNK7QString6toUIntEPbi':
            assert self.reg('rsi') == 0 and self.reg('rdx') == 10
            self.ret(self.inputs['values'][self.current])
        elif name == '_ZN7dyn_edc9refreshitEi':
            assert self.info_shown; self.refreshes += 1; self.ret()
        elif name == '_ZN11QMessageBox11informationEP7QWidgetRK7QStringS4_6QFlagsINS_14StandardButtonEES6_':
            assert 'Applied!' in self.qt_texts; self.info_shown = True; self.ret()
        elif name.startswith(('_ZN7QObject10disconnect','_ZN7QObject7connect','_ZN11QMetaObject10ConnectionD','_ZN6QTimer5start')):
            self.ret()
        else: super().external(name)


def investigate(fixture):
    memory = []
    for enabled in (0,1):
        baseline = None
        for fail in range(-1,14):
            m = MemoryMachine(fixture,dict(enabled=enabled,device=0x153a,fail_at=fail))
            ctor = m.result(pbo.CTOR)
            assert not ctor['pci']
            r = m.result(MEM); trace = r['pci']
            assert len(trace) == 14 and all(x['write'] for x in trace) and not m.locked
            pairs = [(x['offset'],x['index'],x['value']) for x in trace]
            if baseline is None: baseline = pairs
            assert pairs == baseline
            assert [(x['index'],x['value']) for x in trace if x['offset']==0xbc] == [
                (0x50200,1),(0x3b10970,0),(0x3b10a40,enabled),
                (0x3b10970,0),(0x3b10970,1),(0x3b10a40,enabled),(0x3b10924,0x92)]
            memory.append(r)
    edc = []
    def case(**changes):
        inputs = dict(row=0,mode=2,policy=1,enabled=1,initial_control=0xa5,values=[100,200,300,400,500])
        inputs.update(changes)
        m = EdcMachine(fixture,inputs); r = m.run(EDC, instruction_limit=20000)
        assert r['outcome'] == 'returned' and m.info_shown and m.refreshes == 1
        assert len([x for x in m.ec if not x['write']]) == 1
        assert m.ec[0]['address']==0xc1 and m.ec[1]['address']==0xc1
        assert m.ec[1]['value'] == inputs['initial_control'] & 0xfe
        # The first occurrence of each field pair precedes clearing the other
        # selector banks to 65000. Preserve byte order and original truncation.
        writes = {address:next(x['value'] for x in m.ec if x['write'] and x['address']==address)
                  for address in (0xc6,0xc7,0xc8,0xc9,0xd0,0xd1,0xd2,0xd3,0xd4,0xd5)}
        for value,address in zip(inputs['values'],(0xc6,0xc8,0xd0,0xd2,0xd4)):
            assert (writes[address],writes[address+1]) == ((value >> 8) & 255,value & 255)
        assert m.ec[-1]['address']==0xc1 and m.ec[-1]['value']==(inputs['initial_control'] | inputs['enabled'])
        edc.append(dict(inputs=inputs,ec=m.ec,feedback=m.qt_texts,refresh_stub_calls=m.refreshes,steps=r['steps']))
    for row,mode,policy,enabled,control in itertools.product(range(3),range(3),range(3),(0,1),(0,1,0xa4,0xa5)):
        case(row=row,mode=mode,policy=policy,enabled=enabled,initial_control=control)
    for field,value in itertools.product(range(5),(0,1,255,256,65000,65535,65536,0xffffffff)):
        values=[100,200,300,400,500];values[field]=value;case(values=values)
    case();baseline=edc[-1]
    for n,row in enumerate(baseline['ec']):
        if row['write']:
            case(discard_at=n)
            clean=lambda rows:[{k:v for k,v in r.items() if k!='discarded'} for r in rows]
            assert clean(edc[-1]['ec'])==clean(baseline['ec'])
    return dict(schema=1,source_elf_sha256=pbo.SOURCE_SHA,fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
        hardware_execution=False,memory_cases=memory,edc_cases=edc,
        scope=__doc__,limitations=['Full original constructors and actual Qt text parsing are not executed for EDC.',
        'The final EDC refresh callback is stubbed after the original Applied message; it is not a readback proof.',
        'Synthetic discarded EC writes do not establish the real EC API error convention.',
        'No physical board identity, units, safe operating values or EC coordination protocol established.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path);parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    fixture=extract(args.binary) if args.binary else load_fixture()
    if args.export_fixture: args.export_fixture.write_bytes(encoded(fixture))
    result=investigate(fixture);args.output.write_bytes(encoded(result))
    print(json.dumps(dict(memory_cases=len(result['memory_cases']),edc_cases=len(result['edc_cases']),fixture_sha256=result['fixture_sha256'])))


if __name__ == '__main__': main()
