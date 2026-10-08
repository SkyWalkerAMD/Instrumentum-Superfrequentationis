#!/usr/bin/env python3
"""Characterize original SMU/PCI transport in Unicorn with synthetic I/O only.

Original code executes through pci_read/write_long. Their backend callbacks,
PCI allocation/initialization, port I/O, TSC helper and sleep are synthetic.
No host device, syscall, I/O instruction, MSR, firmware or complete GUI runs.
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
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
import unicorn


def module(name, filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result


base=module('dispatch','emulate-legacy-dispatch.py')
profiles=module('amd_profiles','legacy-amd-initialization.py')
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-amd-transport.json'
FIXTURE_SHA='11b9e60fa785bb4f5c37c9a2f1be40579e4c1aaf58703551b3eb44e4f29231dc'
CMD='_Z8smu_cmd2hmRj'
RETRIEVE='_Z16retrieve_messagev'
RETRIEVE2='_Z17retrieve_message2Rj'
LOCK='_Z14lock_amd_mutexv'
UNLOCK='_Z17release_amd_mutexv'
PACK='_Z13find_pci_dev2jjj'
WRITE='_Z10Wr_SMU_argjj'
WRITE_BDF='_Z10Wr_SMU_argjjj'
FUNCTIONS=(CMD,RETRIEVE,RETRIEVE2,LOCK,UNLOCK,PACK,WRITE,WRITE_BDF,
           '_Z14reset_responsev','_Z19WritePciConfigDwordjjj','_Z18ReadPciConfigDwordjj',
           '_Z18libpci_write_dwordiiiij','_Z17libpci_read_dwordiiii',
           'pci_get_dev','pci_read_long','pci_write_long')
TSC='_Z20lock_tsc_all_threadsv'
READ_BOUNDARY,WRITE_BOUNDARY=0x64000000,0x64000010
U32=0xffffffff


def encoded(value):return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8')


def extract(path):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=base.SOURCE_SHA:raise ValueError('original ELF hash mismatch')
    elf=ELFFile(io.BytesIO(data));symbols=list(elf.get_section_by_name('.symtab').iter_symbols())
    byname={s.name:s for s in symbols}
    objects={s['st_value']:s for s in symbols if s['st_info']['type']=='STT_OBJECT' and s['st_size']}

    def read(address,size):
        for seg in elf.iter_segments():
            rel=address-seg['p_vaddr']
            if seg['p_type']!='PT_LOAD' or rel<0:continue
            if rel+size<=seg['p_filesz']:return data[seg['p_offset']+rel:seg['p_offset']+rel+size]
            if seg['p_filesz']<=rel and rel+size<=seg['p_memsz']:return bytes(size)
        raise ValueError('unmapped source bytes')

    audit=module('audit','elf-runtime-audit.py')
    report=audit.inspect(path,re.compile('^('+'|'.join(map(re.escape,FUNCTIONS+(TSC,)))+')$'),True)
    report.pop('imports')
    fixture={'schema':1,'source_elf_sha256':base.SOURCE_SHA,
             'scope':'Original SMU/PCI wrappers including libpci typed access; synthetic PCI backend, port, TSC, allocation and sleep boundaries',
             'profile_fixture_sha256':profiles.FIXTURE_SHA,'functions':[],'objects':{},'literals':[]}
    dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True
    for name in FUNCTIONS:
        f=next(f for f in report['selected_functions'] if name in f['symbols'])
        code=read(f['address'],f['size']);ins=list(dec.disasm(code,f['address']))
        if sum(i.size for i in ins)!=len(code):raise ValueError('incomplete decode')
        fixture['functions'].append(dict(symbol=name,address=f['address'],size=len(code),
                                        code_hex=code.hex(),code_sha256=hashlib.sha256(code).hexdigest(),calls=f['calls']))
        for i in ins:
            for op in i.operands:
                if op.type!=X86_OP_MEM or op.mem.base!=X86_REG_RIP:continue
                address=i.address+i.size+op.mem.disp
                if address not in objects:raise ValueError('unrecognized RIP object')
                s=objects[address]
                fixture['objects'][s.name]={'address':address,'size':s['st_size'],
                                           'source_initial_hex':read(address,s['st_size']).hex()}
    report.update(fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
                  fixture_code_bytes=sum(f['size'] for f in fixture['functions']),
                  scope=fixture['scope'],static_only_functions=[TSC])
    return report,fixture


def load_fixture(path=FIXTURE):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=FIXTURE_SHA:raise ValueError('SMU transport fixture hash mismatch')
    value=json.loads(data)
    if (value['source_elf_sha256']!=base.SOURCE_SHA or value['schema']!=1 or
        value['profile_fixture_sha256']!=profiles.FIXTURE_SHA):raise ValueError('SMU transport source/schema/profile mismatch')
    return value


class TransportMachine(base.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs)
        for row in fixture['objects'].values():self.uc.mem_write(row['address'],bytes.fromhex(row['source_initial_hex']))
        pf=profiles.load_fixture()
        profile=inputs.get('profile','source')
        if profile!='source':
            for row in pf['profiles'][profile]:
                if row['object'] in self.objects:self.put64(self.objects[row['object']],row['value'])
        self.config={n:self.u64(a) & U32 for n,a in self.objects.items() if n.startswith('SMU_')}
        self.uc.mem_write(self.objects['MY_PCI_LIB_INIT'],bytes([not inputs.get('needs_init',False)]))
        self.pacc=self.alloc(32);self.put64(self.objects['pacc_pci'],self.pacc)
        self.put64(self.objects['_ZL14current_device'],0)
        self.backend=self.alloc(0x60)
        self.put64(self.backend+0x40,READ_BOUNDARY);self.put64(self.backend+0x48,WRITE_BOUNDARY)
        for address in (READ_BOUNDARY,WRITE_BOUNDARY):
            self.map(address,1);self.uc.mem_write(address,b'\xc3')
        self.trace=[];self.pci=[];self.port_reads=0;self.pci_reads=0;self.device_count=0
        self.selected=None

    def prepare(self,symbol):
        p=self.inputs
        self.put32(self.output,0xa5a5a5a5)
        if symbol==CMD:
            self.reg('rdi',p.get('command',0x57));self.reg('rsi',p.get('argument',0x1234567887654321));self.reg('rdx',self.output)
        elif symbol in (WRITE,WRITE_BDF):
            self.reg('rdi',p.get('address',0x12345678));self.reg('rsi',p.get('value',0x89abcdef));self.reg('rdx',p.get('bdf',0))
        elif symbol==PACK:
            for reg,key in zip(('rdi','rsi','rdx'),('bus','device','function')):self.reg(reg,p[key])

    def hook(self,uc,pc,size,unused):
        if pc in (READ_BOUNDARY,WRITE_BOUNDARY):
            self.steps+=1;self.pci_backend(pc==WRITE_BOUNDARY)
        else:super().hook(uc,pc,size,unused)

    def pci_backend(self,write):
        dev,offset,buffer,size=(self.reg(r) for r in ('rdi','rsi','rdx','rcx'))
        if size!=4 or offset not in (0xf8,0xfc):raise ValueError('unexpected synthetic PCI access')
        domain=struct.unpack('<I',self.uc.mem_read(dev+0xf0,4))[0]
        bdf=list(self.uc.mem_read(dev+0xa,3))
        mode=self.inputs.get('mode','success');result=1
        if write:
            value=struct.unpack('<I',self.uc.mem_read(buffer,4))[0]
            if mode=='write-error':result=0
            # Track attempted index, not a claim of device accepting a failed write.
            if offset==0xf8:self.selected=value
        else:
            self.pci_reads+=1
            if offset!=0xfc or self.pci_reads>2:raise ValueError('unexpected read sequence')
            status_read=self.pci_reads==1
            value=self.inputs.get('status',1) if status_read else self.inputs.get('reply',0xdeadbeef)
            if mode=='read-error' or mode==('status-read-error' if status_read else 'argument-read-error'):
                result=0;value=None
            else:self.put32(buffer,value)
        row=dict(op='write' if write else 'read',domain=domain,bdf=bdf,offset=offset,
                 attempted_index=self.selected,value=value,backend_result=result)
        self.pci.append(row);self.trace.append({'pci':row});self.ret(result)

    def external(self,name):
        self.calls.append(name);p=self.inputs;value=0
        if name=='_Z8lock_tscv':self.trace.append({'synthetic_boundary':'lock_tsc; body not executed'})
        elif name=='_Z17libpci_initializev':
            self.uc.mem_write(self.objects['MY_PCI_LIB_INIT'],b'\x01')
            self.trace.append({'synthetic_boundary':'libpci_initialize; successful initialization only'})
        elif name=='pci_alloc_dev':
            if self.reg('rdi')!=self.pacc:raise ValueError('wrong synthetic pci_access pointer')
            value=self.alloc(0x1b0);self.put64(value+0x180,self.backend)
            self.put32(value+0x190,0)  # no configuration cache, hence backend path
            self.device_count+=1
        elif name=='ReadIoPortByte':
            if self.reg('rcx')!=0x4d0:raise ValueError('unexpected port read')
            self.port_reads+=1
            value=0 if self.port_reads>p.get('busy_reads',0) else p.get('busy_value',1)
            self.trace.append({'port_read':0x4d0,'value':value})
        elif name=='WriteIoPortByte':
            if self.reg('rcx')!=0x4d0 or self.reg('rdx') not in (0,1):raise ValueError('unexpected port write')
            self.trace.append({'port_write':0x4d0,'value':self.reg('rdx')})
        elif name=='usleep@plt':
            delay=self.reg('rdi')
            if delay not in (1000,2000):raise ValueError('unexpected sleep duration')
            value=-1 if p.get('mode')=='sleep-error' else 0
            self.trace.append({'sleep_us':delay,'result':value})
        else:
            self.calls.pop();return super().external(name)
        self.ret(value)


def investigate(fixture):
    rows=[]

    def run(symbol,inputs):
        m=TransportMachine(fixture,inputs);r=m.run(symbol)
        if r['outcome']!='returned':raise AssertionError('unexpected bounded outcome')
        value=m.reg('rax') & U32
        output=struct.unpack('<I',m.uc.mem_read(m.output,4))[0]
        mode=inputs.get('mode','success')
        status=U32 if mode in ('read-error','status-read-error') else inputs.get('status',1)
        reply=U32 if mode in ('read-error','argument-read-error') else inputs.get('reply',0xdeadbeef)
        if symbol in (CMD,RETRIEVE2,RETRIEVE):
            if m.pci_reads!=2 or value!=(reply if symbol==RETRIEVE else status):raise AssertionError('retrieval result differs')
            if output!=(0xa5a5a5a5 if symbol==RETRIEVE else reply):raise AssertionError('output differs')
            if m.pci[-4]['value']!=m.config['SMU_IOPORT'] or m.pci[-2]['value']!=m.config['SMU_ARG0']:
                raise AssertionError('response/ARG0 ordering differs')
        writes=[(x['attempted_index'],x['value']) for x in m.pci if x['op']=='write' and x['offset']==0xfc]
        sleeps=[x['sleep_us'] for x in m.trace if 'sleep_us' in x]
        port_writes=[x['value'] for x in m.trace if 'port_write' in x]
        if symbol==CMD:
            arg=inputs.get('argument',0x1234567887654321)
            expected=[(m.config['SMU_IOPORT'],0)]+[(m.config['SMU_ARG'+str(i)],v) for i,v in enumerate([arg & U32,(arg>>32)&U32,0,0,0,0])]+[(m.config['SMU_DATAPORT'],inputs.get('command',0x57)&255)]
            if writes!=expected or len(m.pci)!=20 or sleeps.count(1000)!=19 or port_writes!=[1,0]:
                raise AssertionError('command order/width/cleanup differs')
            if any(x['domain'] or x['bdf']!=[0,0,0] for x in m.pci):raise AssertionError('default PCI target differs')
        if symbol in (LOCK,CMD):
            busy=inputs.get('busy_reads',0)
            if m.port_reads!=min(busy+1,11) or sleeps.count(2000)!=max(0,min(busy-1,10)):
                raise AssertionError('lock retry budget differs')
            if port_writes!=( [1,0] if symbol==CMD else [1]):raise AssertionError('lock exit differs')
        if symbol==UNLOCK and port_writes!=[0]:raise AssertionError('release differs')
        if symbol in (WRITE,WRITE_BDF):
            b=inputs.get('bdf',0) if symbol==WRITE_BDF else 0
            if writes!=[(inputs.get('address',0x12345678)&U32,inputs.get('value',0x89abcdef)&U32)]:
                raise AssertionError('write pair differs')
            if any(x['domain'] or x['bdf']!=[(b>>8)&255,(b>>3)&31,b&7] for x in m.pci) or sleeps!=[1000,1000]:
                raise AssertionError('explicit BDF or sleep differs')
        if symbol==PACK:
            expected=((inputs['bus']&255)<<8)|((inputs['device']&31)<<3)|(inputs['function']&7)
            if value!=expected or m.trace or m.calls:raise AssertionError('BDF packer is not pure')
        rows.append(dict(symbol=symbol,inputs=inputs,return_u32=value,output_u32=output,config=m.config,
                         pci_device_allocations=m.device_count,trace=m.trace,**r))

    for status in list(range(256))+[0x100,0x10000,0x80000000,U32]:run(RETRIEVE2,{'status':status})
    for status in (0,1,0xfc,0xfd,0xfe,0xff,0x100,U32):run(RETRIEVE,{'status':status})
    for profile,arg,command in itertools.product(('source','pheonix','shimada','gpt'),
            (0,1,0x1234567887654321,0xffffffffffffffff),(0,1,255,511)):
        run(CMD,dict(profile=profile,argument=arg,command=command))
    for profile,mode in itertools.product(('source','pheonix','shimada','gpt'),
            ('write-error','read-error','status-read-error','argument-read-error','sleep-error','success')):
        run(CMD,dict(profile=profile,mode=mode,busy_reads=100,busy_value=255,needs_init=True,status=0))
    for busy,value in itertools.product((0,1,2,5,9,10,11,100),(1,255)):run(LOCK,dict(busy_reads=busy,busy_value=value))
    run(UNLOCK,{})
    for bdf,value in itertools.product((0,1,0x1234,0xffff,0xffffffff),(0,U32)):
        run(WRITE_BDF,dict(bdf=bdf,value=value))
    run(WRITE,{})
    for bus,device,function in itertools.product((0,255,256,U32),(0,31,32,U32),(0,7,8,U32)):
        run(PACK,dict(bus=bus,device=device,function=function))
    return dict(schema=1,source_elf_sha256=base.SOURCE_SHA,fixture_sha256=FIXTURE_SHA,
                profile_fixture_sha256=profiles.FIXTURE_SHA,
                scope=fixture['scope']+'; no original TSC body, full initialization, hardware or GUI executed',
                environment=dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
                cases_characterized=len(rows),observations=rows)


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--fixture',type=Path,default=FIXTURE)
    parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--analysis-output',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.binary:
        report,fixture=extract(args.binary)
        if not args.export_fixture or not args.analysis_output:parser.error('binary needs both extraction output paths')
        args.export_fixture.write_bytes(encoded(fixture));args.analysis_output.write_bytes(encoded(report))
        print('fixture SHA '+report['fixture_sha256'])
    else:fixture=load_fixture(args.fixture)
    result=investigate(fixture);args.output.write_bytes(encoded(result))
    print(str(result['cases_characterized'])+' original AMD transport cases; synthetic I/O only')


if __name__=='__main__':main()
