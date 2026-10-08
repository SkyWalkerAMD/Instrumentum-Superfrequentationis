#!/usr/bin/env python3
"""Recover original AMD software profile selection with synthetic CPUID/PCI.

Only the selected predicate/table-store instructions execute in Unicorn. No
CPUID opcode, hardware register, kernel, complete GUI or panel body runs.
"""
import argparse
import hashlib
import importlib.util
import io
import itertools
import json
from pathlib import Path
import platform
import re

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
import unicorn
from unicorn import UC_HOOK_MEM_WRITE

spec=importlib.util.spec_from_file_location('dispatch',Path(__file__).with_name('emulate-legacy-dispatch.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-amd-initialization.json'
FIXTURE_SHA='ea27159591a37b7513a721e9eedca1edbe4a59629a4911f00d539d52432399bb'
FAMILY='_Z10FamilyTypev'
PHX='_Z10is_pheonixv'
GRANITE='_Z10is_granitev'
SHIMADA='_Z10is_shimadav'
GPT='_Z6is_gptv'
SETTERS={'pheonix':'_Z14set_to_pheonixv','shimada':'_Z14set_to_shimadav','gpt':'_Z10set_to_gptv'}
STARTUP='MainWindow.constructor.amd-profile-selection'
PARENT='_ZN10MainWindowC2EP7QWidget'
START,END,JOIN=0x8e4348,0x8e4389,0x8e42ab
FUNCTIONS=(FAMILY,PHX,GRANITE,SHIMADA,GPT)+tuple(SETTERS.values())
MASK=(1<<64)-1


def encoded(value):return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8')


def extract(path):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=base.SOURCE_SHA:raise ValueError('original ELF hash mismatch')
    elf=ELFFile(io.BytesIO(data))
    symbols=list(elf.get_section_by_name('.symtab').iter_symbols())
    byname={s.name:s for s in symbols}
    objects={s['st_value']:s for s in symbols if s['st_info']['type']=='STT_OBJECT' and s['st_size']}

    def read(address,size,allow_bss=False):
        for seg in elf.iter_segments():
            rel=address-seg['p_vaddr']
            if seg['p_type']!='PT_LOAD' or rel<0:continue
            if rel+size<=seg['p_filesz']:return data[seg['p_offset']+rel:seg['p_offset']+rel+size]
            if allow_bss and seg['p_filesz']<=rel and rel+size<=seg['p_memsz']:return bytes(size)
        raise ValueError('address outside permitted ELF segments')

    sp=importlib.util.spec_from_file_location('audit',Path(__file__).with_name('elf-runtime-audit.py'))
    audit=importlib.util.module_from_spec(sp);sp.loader.exec_module(audit)
    report=audit.inspect(path,re.compile('^('+'|'.join(map(re.escape,FUNCTIONS+(PARENT,)))+')$'),True)
    report.pop('imports')
    fixture={'schema':1,'source_elf_sha256':base.SOURCE_SHA,
             'scope':'AMD family predicates and global software table stores; synthetic CPUID/PCI; partial constructor with explicit boundary',
             'functions':[],'objects':{},'literals':[],'profiles':{}}
    dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True
    decoded={}
    for name in FUNCTIONS+(PARENT,):
        f=next(f for f in report['selected_functions'] if name in f['symbols'])
        start,end=(START,END) if name==PARENT else (f['address'],f['address']+f['size'])
        code=read(start,end-start);ins=list(dec.disasm(code,start));decoded[name]=ins
        if sum(i.size for i in ins)!=len(code):raise ValueError('incomplete decode')
        row=dict(symbol=STARTUP if name==PARENT else name,address=start,size=len(code),
                 code_hex=code.hex(),code_sha256=hashlib.sha256(code).hexdigest(),
                 calls=[c for c in f['calls'] if start<=c['instruction']<end])
        if name==PARENT:
            row.update(parent_symbol=PARENT,parent_function_sha256=f['code_sha256'],stop_address=JOIN,
                       preconditions='Enter the original constructor region after prior initialization; stop at join before UI setup')
            f['instructions']=[i for i in f['instructions'] if START<=i['address']<END]
            f['instruction_scope']='Only the AMD profile-selection span; whole parent hash retained'
            f['calls']=row['calls'];f['literals']=[]
        fixture['functions'].append(row)
        for i in ins:
            for op in i.operands:
                if op.type!=X86_OP_MEM or op.mem.base!=X86_REG_RIP:continue
                address=i.address+i.size+op.mem.disp
                if address not in objects:raise ValueError('unrecognized RIP object')
                s=objects[address]
                if s['st_size'] not in (1,8):raise ValueError('unexpected profile variable width')
                fixture['objects'][s.name]={'address':address,'size':s['st_size'],
                                           'source_initial_hex':read(address,s['st_size'],True).hex()}
    for profile,name in SETTERS.items():
        ins=decoded[name];stores=[]
        if ins[0].mnemonic!='endbr64' or ins[-1].mnemonic!='ret' or len(ins[1:-1])%2:
            raise ValueError('profile is not a straight store list')
        for n in range(1,len(ins)-1,2):
            lea,mov=ins[n:n+2]
            if (lea.mnemonic!='lea' or lea.reg_name(lea.operands[0].reg)!='rax' or
                lea.operands[1].type!=X86_OP_MEM or lea.operands[1].mem.base!=X86_REG_RIP or
                mov.mnemonic!='mov' or mov.operands[0].type!=X86_OP_MEM or
                mov.reg_name(mov.operands[0].mem.base)!='rax' or mov.operands[0].mem.disp or
                mov.operands[0].size!=8 or mov.operands[1].type!=X86_OP_IMM):
                raise ValueError('unexpected profile assignment')
            address=lea.address+lea.size+lea.operands[1].mem.disp
            stores.append({'instruction':mov.address,'object':objects[address].name,'address':address,
                           'value':mov.operands[1].imm & MASK})
        fixture['profiles'][profile]=stores
    report.update(fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
                  fixture_code_bytes=sum(f['size'] for f in fixture['functions']),profiles=fixture['profiles'],
                  scope=fixture['scope'])
    return report,fixture


def load_fixture(path=FIXTURE):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=FIXTURE_SHA:raise ValueError('AMD fixture hash mismatch')
    value=json.loads(data)
    if value['source_elf_sha256']!=base.SOURCE_SHA or value['schema']!=1:raise ValueError('AMD source/schema mismatch')
    return value


class AmdMachine(base.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs)
        self.fixture=fixture;self.object_by_address={r['address']:(n,r['size']) for n,r in fixture['objects'].items()}
        for name,row in fixture['objects'].items():
            raw=bytes.fromhex(row['source_initial_hex'])
            self.uc.mem_write(row['address'],raw)
        self.uc.mem_write(self.objects['GLOBAL_IS_PHX'],bytes([inputs.get('initial_phx',0)]))
        self.before=self.state();self.writes=[];self.pci_queries=[];self.cpuid_queries=[]
        self.map(JOIN,1);self.uc.mem_write(JOIN,b'\xc3')
        self.uc.hook_add(UC_HOOK_MEM_WRITE,self.on_write)

    def state(self):
        return {name:int.from_bytes(self.uc.mem_read(row['address'],row['size']),'little')
                for name,row in self.fixture['objects'].items()}

    def on_write(self,uc,access,address,size,value,unused):
        if address in self.object_by_address:
            name,width=self.object_by_address[address]
            if size!=width:raise ValueError('wrong profile store width')
            self.writes.append({'object':name,'value':value & ((1<<(8*size))-1)})

    def hook(self,uc,pc,size,unused):
        if pc==JOIN:
            self.steps+=1;self.stop('constructor-join')
        else:super().hook(uc,pc,size,unused)

    def external(self,name):
        self.calls.append(name)
        if name=='_Z7CpuidTxjPjS_S_S_m':
            if self.reg('rcx')!=0x80000001 or self.u64(self.reg('rsp')+48)!=0:
                raise ValueError('CPUID helper parameters differ')
            pointers=[self.reg('rdx'),self.reg('r8'),self.reg('r9'),self.u64(self.reg('rsp')+40)]
            for ptr,value in zip(pointers,[self.inputs.get('eax',0),0,0,0]):self.put32(ptr,value)
            self.cpuid_queries.append({'leaf':0x80000001,'helper_mask':0,'eax':self.inputs.get('eax',0)})
            self.ret()
        elif name=='_Z18FindPciDeviceById2jjj':
            vendor,device,index=self.reg('rdi'),self.reg('rsi'),self.reg('rdx')
            if vendor!=0x1022 or device not in (0x14e8,0x14d8,0x153a,0x1122) or index:
                raise ValueError('unexpected PCI search')
            result=0 if device in self.inputs.get('pci_devices',[]) else -1
            self.pci_queries.append({'vendor':vendor,'device':device,'index':index,'result':result})
            self.ret(result)
        else:
            self.calls.pop();return super().external(name)


def family(eax):return {6:0xf,8:0x11,10:0x13}.get((eax>>20)&255,0)


def investigate(fixture):
    rows=[]

    def run(symbol,inputs):
        m=AmdMachine(fixture,inputs);r=m.run(symbol)
        expected=dict(m.before);f=family(inputs.get('eax',0));devices=inputs.get('pci_devices',[])
        if symbol==FAMILY:
            if r['outcome']!='returned' or r['return_al']!=f:raise AssertionError('FamilyType differs')
        elif symbol==STARTUP:
            phx=0x14e8 in devices;granite=f==0 and 0x14d8 in devices;shimada=f==0 and 0x153a in devices;gpt=0x1122 in devices
            for profile,selected in [('pheonix',phx),('shimada',shimada),('gpt',gpt)]:
                if selected:
                    expected.update({s['object']:s['value'] for s in fixture['profiles'][profile]})
            if phx:expected['GLOBAL_IS_PHX']=1
            expected.update(GLOBAL_IS_GRANITE=int(granite),GLOBAL_IS_SHIMADA=int(shimada),GLOBAL_IS_GPT=int(gpt))
            if r['outcome']!='constructor-join':raise AssertionError('constructor region escaped')
        else:raise ValueError('unsupported initialization experiment')
        actual=m.state()
        if actual!=expected:raise AssertionError('global profile state differs from decoded assignments')
        rows.append({'symbol':symbol,'inputs':inputs,'outcome':r['outcome'],'return_al':r['return_al'] if symbol==FAMILY else None,
                     'steps':r['steps'],'pci_queries':m.pci_queries,'cpuid_queries':m.cpuid_queries,
                     'object_store_count':len(m.writes),'state_sha256':hashlib.sha256(encoded(actual)).hexdigest(),
                     'flags':{k:v for k,v in actual.items() if k.startswith('GLOBAL_')}})

    for extended in range(256):run(FAMILY,{'eax':(extended<<20)|0xfffff})
    # The low 20 and high 4 bits are discarded by the original SHR + AL use.
    for extended,other in itertools.product((0,6,8,10,11,26,255),(0,0xf0000000)):
        run(FAMILY,{'eax':(extended<<20)|other})
    for extended,bits in itertools.product((0,6,8,10,11,26,255),range(16)):
        devices=[d for n,d in enumerate((0x14e8,0x14d8,0x153a,0x1122)) if bits & (1<<n)]
        run(STARTUP,{'eax':(extended<<20)|0xfffff,'pci_devices':devices})
    for extended in (0,6,11):run(STARTUP,{'eax':extended<<20,'pci_devices':[],'initial_phx':1})
    return {'schema':1,'source_elf_sha256':base.SOURCE_SHA,'fixture_sha256':hashlib.sha256(encoded(fixture)).hexdigest(),
            'scope':fixture['scope'],'environment':dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
            'cases_executed':len(rows),'observations':rows,
            'initial_object_values':{n:int.from_bytes(bytes.fromhex(r['source_initial_hex']),'little') for n,r in fixture['objects'].items()},
            'profile_assignments':fixture['profiles']}


def main():
    p=argparse.ArgumentParser(__doc__);src=p.add_mutually_exclusive_group(required=True)
    src.add_argument('--binary',type=Path);src.add_argument('--fixture',type=Path)
    p.add_argument('--export-fixture',type=Path);p.add_argument('--static-output',type=Path)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.binary:
        report,fixture=extract(args.binary)
        for path,value in ((args.static_output,report),(args.export_fixture,fixture)):
            if path:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(encoded(value))
        print('fixture SHA {}; {} original code bytes'.format(report['fixture_sha256'],report['fixture_code_bytes']))
    else:
        if args.export_fixture or args.static_output:raise ValueError('export requires original ELF')
        fixture=load_fixture(args.fixture)
    result=investigate(fixture);args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(encoded(result))
    print('{} AMD initialization cases; no hardware, actual CPUID or GUI executed'.format(result['cases_executed']))


if __name__=='__main__':main()
