#!/usr/bin/env python3
"""REA-guided original OC entry points; synthetic SMU/MP1 boundaries only."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('oc_base', Path(__file__).with_name('legacy-amd-pbo-transactions.py'))
pbo = importlib.util.module_from_spec(spec); spec.loader.exec_module(pbo)
ENABLE, DISABLE, ALL = '_Z12enableocmodev', '_Z17amd_disableocmodev', '_Z11Wr_AMD_freqi'
CORE, CCD = '_Z14one_core_freq2ii', '_Z12per_ccx_freqsi'
CORE_FAST, CCD_FAST = '_Z18one_core_freq_fastiij', '_Z17per_ccx_freq_fastsij'
VID = '_Z8Wr_ocvidi'
FUNCTIONS = (ENABLE, DISABLE, ALL, CORE, CCD, CORE_FAST, CCD_FAST, VID,
             '_Z21one_core_freq_vermeerii', '_Z19amd_per_ccd_vermeersi',
             '_Z26one_core_freq_vermeer_fastiij', '_Z25per_ccx_freq_vermeer_fastsij')
FIXTURE = ROOT / 'analysis/fixtures/legacy-amd-oc-frequency.json'
FIXTURE_SHA = 'a6b9abbb4e0e355d84d6256f7de2ad0f7a3e24b16e98e398603d1082e07ab2dc'


def extract(path):
    pbo.FUNCTIONS = FUNCTIONS
    fixture = pbo.extract(path)
    elf = ELFFile(io.BytesIO(path.read_bytes()))
    symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    for name in ['SMU_DATAPORT'] + ['SMU_ARG'+str(i) for i in range(6)]:
        s = symbols[name]; fixture['objects'][name] = dict(address=s['st_value'],size=s['st_size'])
    # Preserve bounded original constants/jump tables, never executable libraries.
    decoder = Cs(CS_ARCH_X86, CS_MODE_64); decoder.detail = True
    regions = {}
    for function in fixture['functions']:
        for instruction in decoder.disasm(bytes.fromhex(function['code_hex']),function['address']):
            for operand in instruction.operands:
                if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP: continue
                address = instruction.address + instruction.size + operand.mem.disp
                section = next((s for s in elf.iter_sections() if s['sh_addr'] <= address < s['sh_addr']+s['sh_size'] and s.name == '.rodata'),None)
                if section is None: continue
                offset = address-section['sh_addr']; data=section.data()[offset:offset+256]
                regions[address] = dict(address=address,size=len(data),data_hex=data.hex(),sha256=hashlib.sha256(data).hexdigest())
    fixture['constant_regions'] = list(regions.values())
    return fixture


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA: raise ValueError('OC fixture identity mismatch')
    return json.loads(data)


class Machine(pbo.base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        for row in fixture['constant_regions']:
            data = bytes.fromhex(row['data_hex'])
            assert hashlib.sha256(data).hexdigest() == row['sha256'] and len(data) == row['size']
            self.map(row['address'],len(data)); self.uc.mem_write(row['address'],data)
        for name in ('GLOBAL_IS_SHIMADA','GLOBAL_IS_GPT','GLOBAL_IS_GRANITE'):
            self.uc.mem_write(self.objects[name],bytes([bool(inputs.get(name,name=='GLOBAL_IS_SHIMADA'))]))
        # Addresses are boundary tags, not claims about an initialized mailbox.
        self.addresses = {0x1000+4*i:'arg'+str(i) for i in range(6)}
        self.addresses[0x1020] = 'command'
        for i in range(6): self.put32(self.objects['SMU_ARG'+str(i)],0x1000+4*i)
        self.put32(self.objects['SMU_DATAPORT'],0x1020)
        self.operations = []

    def prepare(self,symbol):
        if symbol in (ALL,VID): self.reg('rdi',self.inputs.get('value',5000))
        elif symbol in (CORE,CCD,CORE_FAST,CCD_FAST):
            self.reg('rdi',self.inputs.get('target',0)); self.reg('rsi',self.inputs.get('value',5000)); self.reg('rdx',self.inputs.get('fast_bdf',0x1234))

    def external(self,name):
        self.calls.append(name)
        if name == '_Z10FamilyTypev': self.ret(self.inputs.get('family',0x1a))
        elif name == '_Z10is_pheonixv': self.ret(self.inputs.get('phoenix',0))
        elif name == '_Z9is_am5_esv': self.ret(self.inputs.get('am5_es',0))
        elif name in ('_Z10Wr_SMU_argjj','_Z10Wr_SMU_argjjj'):
            address, value = self.reg('rdi') & pbo.U32, self.reg('rsi') & pbo.U32
            self.operations.append(dict(kind=self.addresses[address],value=value,bdf=(self.reg('rdx') & pbo.U32) if name.endswith('jjj') else None))
            self.ret(self.inputs.get('boundary_return',0))
        elif name == '_Z10MP1_C2PMSGjjRj':
            self.operations.append(dict(kind='mp1',message=self.reg('rdi') & pbo.U32,argument=self.reg('rsi') & pbo.U32))
            self.put32(self.reg('rdx'),self.inputs.get('mp1_returned',0xffffffff));self.ret(self.inputs.get('boundary_return',0))
        elif name in ('_Z14reset_responsev','_Z8lock_tscv','_Z16retrieve_messagev','_Z14lock_amd_mutexv','_Z17release_amd_mutexv'):
            self.operations.append(dict(kind=name));self.ret(self.inputs.get('boundary_return',0))
        else: raise ValueError('Unexpected OC synthetic boundary '+name)

    def result(self,symbol):
        outcome = self.run(symbol); assert outcome['outcome']=='returned'
        return dict(symbol=symbol,inputs=self.inputs,operations=self.operations,returned=self.reg('rax') & pbo.U32)


def investigate(fixture):
    routing=[]
    for symbol in (ENABLE,DISABLE,ALL):
        for phoenix,gpt in ((0,0),(1,0),(0,1),(1,1)):
            for returned in (0,1,0xfe):
                routing.append(Machine(fixture,dict(phoenix=phoenix,GLOBAL_IS_GPT=gpt,boundary_return=returned)).result(symbol))
    frequency=[]
    for symbol in (CORE,CCD,CORE_FAST,CCD_FAST):
        for target in range(128 if symbol in (CORE,CORE_FAST) else 16):
            for value in (0,5000,0x100000,-1):
                frequency.append(Machine(fixture,dict(target=target,value=value)).result(symbol))
    boundary=[]
    for symbol in (ALL,CORE,CCD,CORE_FAST,CCD_FAST):
        for target in (-1,128,256,0x7fffffff):
            boundary.append(Machine(fixture,dict(target=target,value=-1,boundary_return=0xfe)).result(symbol))
    voltage=[]
    for value in (-1,0,699,700,701,1250,2400,2401,0x7fffffff):
        voltage.append(Machine(fixture,dict(value=value,boundary_return=0xfe)).result(VID))
    aliases=[]
    for row in frequency:
        if row['symbol'] not in (CORE,CORE_FAST) or row['inputs']['value']!=5000: continue
        target=row['inputs']['target']; expected=(target//8<<28)|(target%8<<20)|5000
        original=next(r['value'] for r in row['operations'] if r['kind']=='arg0')
        if original!=expected:aliases.append(dict(symbol=row['symbol'],flat_index=target,original_argument=original,linear_eight_core_argument=expected))
    assert len(aliases)==36
    return dict(schema=1,source_elf_sha256=pbo.SOURCE_SHA,fixture_sha256=FIXTURE_SHA,hardware_execution=False,
        scope='Original function instructions with original bounded constants. SMU writes, MP1, family predicates, locks and timing are synthetic boundaries; no firmware, PCI or Qt code executes.',
        routing=routing,frequency=frequency,boundary=boundary,voltage=voltage,selector_aliases=aliases)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--extract',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.extract:
        data=(json.dumps(extract(args.extract),indent=2)+'\n').encode();FIXTURE.write_bytes(data)
        print('fixture_sha256',hashlib.sha256(data).hexdigest())
    else:
        report=investigate(load_fixture());args.output.write_bytes((json.dumps(report,indent=2)+'\n').encode())
        print(json.dumps({k:len(report[k]) for k in ('routing','frequency','boundary','voltage')}))
