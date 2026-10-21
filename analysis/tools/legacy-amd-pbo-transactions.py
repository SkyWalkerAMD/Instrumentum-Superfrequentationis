#!/usr/bin/env python3
"""REA-guided original PBO/MP1 instruction replay; no target hardware executes."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SHA = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
FIXTURE = ROOT / 'analysis/fixtures/legacy-amd-pbo-transactions.json'
FIXTURE_SHA = '4af8d9f173b4daabdc93d9f56945cc389eb59f3ef9300a8e05abd5d8c6d3316c'
CTOR = '_ZN7AMD_PBOC2Ev'
FMAX = '_ZN7AMD_PBO8set_fmaxEj'
RAILS = '_ZN7AMD_PBO16set_core01_railsEii'
MP1 = '_Z10MP1_C2PMSGjjRj'
FUNCTIONS = (CTOR, FMAX, RAILS, MP1)
U32 = 0xffffffff


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec); spec.loader.exec_module(result); return result


base = module('pbo_base', 'emulate-legacy-dispatch.py')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA: raise ValueError('original ELF identity mismatch')
    elf = ELFFile(io.BytesIO(data)); symbols = {s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    audit = module('pbo_audit','elf-runtime-audit.py').inspect(path,re.compile('^(?:'+'|'.join(FUNCTIONS)+')$'),True)
    functions = []
    for name in FUNCTIONS:
        s=symbols[name]; address,size=s['st_value'],s['st_size']
        seg=next(p for p in elf.iter_segments() if p['p_type']=='PT_LOAD' and p['p_vaddr']<=address and address+size<=p['p_vaddr']+p['p_filesz'])
        offset=seg['p_offset']+address-seg['p_vaddr']; code=data[offset:offset+size]
        row=next(f for f in audit['selected_functions'] if name in f['symbols'])
        functions.append(dict(symbol=name,address=address,size=size,code_hex=code.hex(),code_sha256=hashlib.sha256(code).hexdigest(),calls=row['calls']))
    objects={name:dict(address=symbols[name]['st_value'],size=symbols[name]['st_size']) for name in ('GLOBAL_IS_SHIMADA','GLOBAL_IS_GPT','GLOBAL_IS_GRANITE')}
    return dict(schema=1,source_elf_sha256=SOURCE_SHA,functions=functions,objects=objects,literals=[])


def load_fixture():
    data=FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest()!=FIXTURE_SHA: raise ValueError('PBO fixture identity mismatch')
    return json.loads(data)


class Machine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture,inputs)
        self.this=self.alloc(256); self.pci=[]; self.index=0; self.locked=False; self.output_word=self.alloc(4)
        self.delays=[]
        for name in self.objects:
            self.uc.mem_write(self.objects[name],bytes([bool(inputs.get(name,name=='GLOBAL_IS_SHIMADA'))]))
        # Explicit synthetic RSMU object for standalone setters. Constructor
        # cases overwrite these bytes with the complete original constructor.
        for offset,value in ((4,0x3b10970),(8,0x3b10924),(12,0x3b10a40),(20,2000),(24,0)):
            self.put32(self.this+offset,value)
        self.uc.mem_write(self.this+0x11,bytes([inputs.get('flag11',1)]))

    def prepare(self,symbol):
        self.reg('rdi',self.this)
        if symbol==FMAX:self.reg('rsi',self.inputs.get('frequency',5000))
        elif symbol==RAILS:
            self.reg('rsi',self.inputs.get('rail0',1000)&U32);self.reg('rdx',self.inputs.get('rail1',1000)&U32)
        elif symbol==MP1:
            self.reg('rdi',0xa8);self.reg('rsi',0);self.reg('rdx',self.output_word)

    def external(self,name):
        self.calls.append(name); p=self.inputs
        if 'FindPciDeviceById2' in name:
            query=[self.reg('rdi'),self.reg('rsi'),self.reg('rdx')];self.searches.append(query)
            self.ret(0 if query[:2]==[0x1022,p.get('device',0x153a)] else U32)
        elif 'find_pci_dev2' in name:self.ret(p.get('root_id',0x153a))
        elif 'FamilyType' in name:self.ret(0)
        elif name in ('_Z14lock_amd_mutexv','_Z17release_amd_mutexv'):
            self.locked=name=='_Z14lock_amd_mutexv';self.ret()
        elif name=='usleep@plt':self.delays.append(self.reg('rdi'));self.ret()
        elif name in ('_Z19WritePciConfigDwordjjj','_Z18ReadPciConfigDwordjj'):
            write=name.startswith('_Z19Write');bdf,offset=self.reg('rcx'),self.reg('rdx')
            assert bdf==0 and offset in (0xb8,0xbc,0xf8,0xfc),(bdf,offset)
            value=self.reg('r8')&U32 if write else p.get('response',1)
            if not write and self.index==p.get('argument_address',0x3b10a40):value=p.get('returned',0x634)
            fail=len(self.pci)==p.get('fail_at',-1)
            self.pci.append(dict(write=write,offset=offset,index=self.index,value=value,locked=self.locked,injected_failure=fail))
            if write and offset in (0xb8,0xf8):self.index=value
            self.ret((0 if fail else 1) if write else (U32 if fail else value))
        else:raise ValueError('Unexpected original boundary '+name)

    def result(self,symbol):
        self.outcome=None;outcome=self.run(symbol)
        assert outcome['outcome']=='returned'
        return dict(inputs=self.inputs,symbol=symbol,pci=self.pci,delays=self.delays,searches=self.searches,
                    returned=self.reg('rax')&U32,output=struct.unpack('<I',self.uc.mem_read(self.output_word,4))[0],
                    object_hex=bytes(self.uc.mem_read(self.this,0x34)).hex())


def investigate(fixture):
    constructors=[]
    for device in (0x153a,0x1480,0x14d8,0x14e8):
        m=Machine(fixture,dict(device=device,flag11=0));row=m.result(CTOR)
        row['fmax_after_constructor']=m.result(FMAX);constructors.append(row)
    setters=[]
    for flag in (0,1):
        for frequency in (0,1,4999,5000,1000000,U32):
            setters.append(Machine(fixture,dict(flag11=flag,frequency=frequency)).result(FMAX))
    for a,b in ((0,0),(0,1000),(1000,0),(1000,1000),(700,1250),(244,245),(246,249),(-1,3000),(2147483647,-2147483648)):
        setters.append(Machine(fixture,dict(rail0=a,rail1=b)).result(RAILS))
    mp1=[]
    for response in (0,1,0xfe,U32):
        for returned in (0,0x634,U32):
            mp1.append(Machine(fixture,dict(response=response,returned=returned)).result(MP1))
    faults=[]
    for symbol in (FMAX,RAILS,MP1):
        baseline=Machine(fixture,{}).result(symbol)
        for at in range(len(baseline['pci'])):
            faults.append(Machine(fixture,dict(fail_at=at)).result(symbol))
    return dict(schema=1,source_elf_sha256=SOURCE_SHA,hardware_execution=False,
                scope='Full pinned PBO constructor/setter and MP1 instructions; synthetic PCI lookup/access, globals, locks, delay and family. Neither firmware nor hardware behavior is validated.',
                constructors=constructors,setters=setters,mp1=mp1,faults=faults)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--extract',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.extract:
        fixture=extract(a.extract);FIXTURE.write_bytes((json.dumps(fixture,indent=2)+'\n').encode())
    else:fixture=load_fixture()
    report=investigate(fixture);report['fixture_sha256']=hashlib.sha256(FIXTURE.read_bytes()).hexdigest()
    a.output.write_bytes((json.dumps(report,indent=2)+'\n').encode())
    print(json.dumps(dict(fixture_sha256=report['fixture_sha256'],counts={k:len(report[k]) for k in ('constructors','setters','mp1','faults')})))
