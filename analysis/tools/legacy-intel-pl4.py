#!/usr/bin/env python3
"""Replay original VR_CURRENT_CONFIG getters and setters using synthetic I/O."""
import argparse
import hashlib
import importlib.util
import io
import itertools
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
from unicorn.x86_const import UC_X86_REG_XMM0

spec=importlib.util.spec_from_file_location('pl4_shared',Path(__file__).with_name('legacy-intel-ratio.py'))
shared=importlib.util.module_from_spec(spec); spec.loader.exec_module(shared)
GET='_Z20Rd_VR_CURRENT_CONFIGv'
PCI='_Z24Rd_VR_CURRENT_CONFIG_pcuj'
MAX='_Z20Wr_VR_CURRENT_CONFIGv'
SET='_Z20Wr_VR_CURRENT_CONFIGi'
BITS='_Z17max_valuefrombitsm'
FUNCTIONS=(GET,PCI,MAX,SET,BITS)
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-intel-pl4.json'
FIXTURE_SHA='43e976886d4587d4f1f4802dbd4c045a2c9a5143533231e29c2c654fe5cfef85'
U64=(1<<64)-1


def extract(path):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=shared.base.SOURCE_SHA: raise ValueError('source identity mismatch')
    elf=ELFFile(io.BytesIO(data))
    def read(address,size):
        for seg in elf.iter_segments():
            offset=address-seg['p_vaddr']
            if seg['p_type']=='PT_LOAD' and 0<=offset and offset+size<=seg['p_filesz']:
                return data[seg['p_offset']+offset:seg['p_offset']+offset+size]
        raise ValueError('unmapped original data')
    audit=shared.module('pl4_audit','elf-runtime-audit.py')
    report=audit.inspect(path,re.compile('^(?:'+'|'.join(map(re.escape,FUNCTIONS))+')$'),True)
    fixture=dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,functions=[],objects={},literals=[])
    decoder=Cs(CS_ARCH_X86,CS_MODE_64); decoder.detail=True
    constants={}
    for fn in report['selected_functions']:
        symbol=next(s for s in fn['symbols'] if s in FUNCTIONS); code=read(fn['address'],fn['size'])
        fixture['functions'].append(dict(symbol=symbol,address=fn['address'],size=len(code),code_hex=code.hex(),code_sha256=hashlib.sha256(code).hexdigest(),calls=fn['calls']))
        for insn in decoder.disasm(code,fn['address']):
            for op in insn.operands:
                if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
                    address=insn.address+insn.size+op.mem.disp
                    assert insn.mnemonic=='mulsd' and op.size==8
                    raw=read(address,8); assert struct.unpack('<d',raw)==(8.0,)
                    constants[address]=raw
    assert len(fixture['functions'])==5 and len(constants)==1
    for address,raw in constants.items():
        fixture['objects']['scale']=dict(address=address,size=8,source_initial_hex=raw.hex())
    return fixture,dict(source_elf_sha256=shared.base.SOURCE_SHA,functions=report['selected_functions'],constants=fixture['objects'],scope='Four complete original PL4 entries and their real bit-mask helper. No hardware.')


def load_fixture():
    raw=FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA: raise ValueError('PL4 fixture identity mismatch')
    fixture=json.loads(raw)
    if fixture['source_elf_sha256']!=shared.base.SOURCE_SHA: raise ValueError('source mismatch')
    return fixture


class Pl4Machine(shared.base.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs); self.writes=[]
        for obj in fixture['objects'].values(): self.uc.mem_write(obj['address'],bytes.fromhex(obj['source_initial_hex']))

    def prepare(self,symbol): self.reg('rdi',self.inputs.get('input',0)&0xffffffff)

    def external(self,name):
        self.calls.append(name)
        if name=='_Z5RdmsrjPjS_':
            assert self.reg('rdi')==0x601
            if self.inputs.get('supply_data',True):
                self.put32(self.reg('rsi'),self.inputs['raw']); self.put32(self.reg('rdx'),self.inputs['raw']>>32)
            self.ret(self.inputs.get('read_result',1))
        elif name=='_Z18ReadPciConfigDwordjj':
            assert self.reg('rdx')==0xf8 and self.reg('rcx')==(self.inputs.get('input',0)&0xffffffff)
            self.ret(self.inputs['raw']&0xffffffff)
        elif name=='_Z5Wrmsrjjj':
            assert self.reg('rdi')==0x601
            self.writes.append((self.reg('rsi')&0xffffffff)|((self.reg('rdx')&0xffffffff)<<32))
            self.ret(self.inputs.get('write_result',1))
        else: raise ValueError('unrecognized PL4 boundary: '+name)


def investigate(fixture):
    rows=[]
    def case(symbol,inputs):
        m=Pl4Machine(fixture,inputs); assert m.run(symbol)['outcome']=='returned'
        raw=inputs['raw'] if inputs.get('supply_data',True) else 0
        if symbol in (GET,PCI):
            value=struct.unpack('<d',struct.pack('<Q',m.uc.reg_read(UC_X86_REG_XMM0)&U64))[0]
            assert value==(raw&0x1fff)*8 and not m.writes
        else:
            code=0x1fff if symbol==MAX else min(inputs['input']&U64,0x3fff)
            value=(raw&~0x3fff)|code
            assert m.writes==[value]
        rows.append(dict(symbol=symbol,inputs=inputs,value=value,writes=m.writes))
    patterns=[0,U64]+[1<<n for n in range(64)]
    for symbol in (GET,PCI):
        for raw,result in itertools.product(patterns,(0,1) if symbol==GET else (1,)):
            case(symbol,dict(raw=raw,read_result=result,input=0x1234))
    for raw,read_result,write_result in itertools.product(patterns,(0,1),(0,1)):
        case(MAX,dict(raw=raw,read_result=read_result,write_result=write_result))
    for raw,value,read_result,write_result in itertools.product((0,U64,0x8765432140006000,0xa5a5a5a5a5a5a5a5),(-2147483648,-1,0,1,8191,8192,16383,16384,2147483647),(0,1),(0,1)):
        case(SET,dict(raw=raw,input=value,read_result=read_result,write_result=write_result))
    for symbol,result in itertools.product((GET,MAX,SET),(0,1)):
        case(symbol,dict(raw=U64,input=90,supply_data=False,read_result=result))
    return dict(schema=1,source_elf_sha256=shared.base.SOURCE_SHA,scenarios=rows,
                scope='Original PL4 entry bytes and real mask helper; only Rdmsr/Wrmsr/PCI are synthetic. No actual I/O.',
                findings=['Both original getters return low 13 bits multiplied by 8.0, not divided by eight.',
                          'Both setters replace 14 bits. Max helper clears bit 13; integer setter can set it.',
                          'Negative and oversized signed inputs saturate to 16383.',
                          'Read/write failures and lock bit 31 are ignored; there is no readback.',
                          'Scalar MSR and PCU/PCI paths require separate model contracts.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path); parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--static-output',type=Path); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.binary:
        fixture,proof=extract(args.binary)
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA '+hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else: fixture=load_fixture()
    report=investigate(fixture); args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios']))+' original PL4 scenarios; synthetic I/O only')


if __name__=='__main__': main()
