#!/usr/bin/env python3
"""Original UI->Wrmsr/Rdmsr->synthetic libc; no host files or hardware access."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import platform
import struct

import unicorn
from unicorn import UC_HOOK_MEM_READ

spec=importlib.util.spec_from_file_location('dispatch',Path(__file__).with_name('emulate-legacy-dispatch.py'))
base=importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-msr-slots.json'
FIXTURE_SHA='8fa956c195a5afe65dd63284d796520adc70519a3e23b1ab2d13b81b4d4f8574'
SLOTS={'_ZN10intel_ctl610gt_clickedEv':0x80000110,'_ZN10intel_ctl611npu_clickedEv':0x80000810}
LIMIT=2000


def load_fixture(path=FIXTURE):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=FIXTURE_SHA:
        raise ValueError('MSR fixture hash mismatch')
    value=json.loads(data)
    if value['source_elf_sha256']!=base.SOURCE_SHA or value['schema']!=1:
        raise ValueError('MSR fixture source/schema mismatch')
    return value


class MsrMachine(base.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs)
        self.io=[]
        self.reads=0
        self.flag_reads=0
        self.write_buffers=[]
        self.read_buffers=[]
        self.mode=inputs.get('io_mode','success')
        self.uc.hook_add(UC_HOOK_MEM_READ,self.on_read)

    def on_read(self,uc,access,address,size,value,unused):
        if hasattr(self,'output') and address<=self.output+0x40<address+size:
            self.flag_reads+=1

    def prepare(self,symbol):
        if symbol not in SLOTS:
            raise ValueError('unexpected slot')
        ui=self.alloc(0x100); nested=self.alloc(0x1000)
        self.put64(self.output+0x30,ui)
        self.put64(ui+0x30,nested)
        self.uc.mem_write(self.output+0x40,bytes([self.inputs.get('constructor_flag',0)]))
        for offset in (0x88,0x478,0x480,0x488,0x490,0x498):
            self.put64(nested+offset,self.alloc(32))

    def cstring(self,address):
        data=bytearray()
        for n in range(128):
            value=bytes(self.uc.mem_read(address+n,1))
            if value==b'\0':return data.decode('ascii')
            data.extend(value)
        raise ValueError('unterminated synthetic string')

    def external(self,name):
        p=self.inputs
        self.calls.append(name)
        value=0
        if name=='_ZNK9QLineEdit4textEv':
            value=self.alloc(32)
            self.put32(value,-1)
            self.put32(value+4,0 if p.get('empty') else 1)
            self.put64(self.reg('rdi'),value)
        elif name=='_ZNK7QString6toUIntEPbi':
            if self.reg('rsi')!=0 or self.reg('rdx')!=10:
                raise ValueError('changed QString conversion arguments')
            value=p.get('parsed_uint',37)
        elif name=='__sprintf_chk@plt':
            if self.cstring(self.reg('rcx'))!='/dev/cpu/%d/msr' or self.reg('r8')!=0 or self.reg('rdx')!=64:
                raise ValueError('unexpected MSR path format/CPU/size')
            data=b'/dev/cpu/0/msr'
            self.uc.mem_write(self.reg('rdi'),data+b'\0')
            value=len(data)
        elif name=='open@plt':
            path=self.cstring(self.reg('rdi'))
            if path!='/dev/cpu/0/msr' or self.reg('rsi') not in (0,1):
                raise ValueError('unexpected synthetic open')
            value=-1 if self.mode=='open-error' else 700
            self.io.append(dict(op='open',path=path,flags=self.reg('rsi'),result=value))
        elif name=='lseek@plt':
            if self.reg('rsi')!=0x150 or self.reg('rdx')!=0:
                raise ValueError('unexpected MSR seek')
            value=-1 if self.mode in ('open-error','seek-error') else 0x150
            self.io.append(dict(op='lseek',fd=self.reg('rdi') & 0xffffffff,offset=0x150,result=value))
        elif name=='write@plt':
            if self.reg('rdx')!=8:
                raise ValueError('unexpected MSR write size')
            address=self.reg('rsi')
            low,high=struct.unpack('<II',self.uc.mem_read(address,8))
            value=-1 if self.mode in ('open-error','write-error') else 4 if self.mode=='short-write' else 8
            self.write_buffers.append(address)
            self.io.append(dict(op='write',fd=self.reg('rdi') & 0xffffffff,low=low,high=high,result=value,buffer=address))
        elif name=='read@plt':
            if self.reg('rdx')!=8:
                raise ValueError('unexpected MSR read size')
            self.reads+=1
            address=self.reg('rsi')
            before=bytes(self.uc.mem_read(address,8))
            high=p.get('reply_high',0)
            if self.mode=='busy-forever' or (self.mode=='delayed' and self.reads<=3):high=0x80000000
            reply=struct.pack('<II',p.get('reply_low',0x1234),high)
            value=8
            if self.mode in ('open-error','read-error'):value=-1
            elif self.mode=='read-eof':value=0
            elif self.mode=='short-read-4':value=4
            elif self.mode=='short-read-7':value=7
            if value>0:self.uc.mem_write(address,reply[:value])
            self.read_buffers.append(address)
            self.io.append(dict(op='read',fd=self.reg('rdi') & 0xffffffff,result=value,buffer=address,
                                before_hex=before.hex(),after_hex=bytes(self.uc.mem_read(address,8)).hex()))
        elif name=='close@plt':
            value=-1 if self.mode in ('open-error','close-error') else 0
            self.io.append(dict(op='close',fd=self.reg('rdi') & 0xffffffff,result=value))
        elif name=='_ZN11QMessageBox11informationEP7QWidgetRK7QStringS4_6QFlagsINS_14StandardButtonEES6_':
            self.events.append({'information_boundary':True})
        else:
            self.calls.pop()
            return super().external(name)
        self.ret(value)


def investigate(path=FIXTURE):
    fixture=load_fixture(path); rows=[]

    def run(symbol,label,inputs,hang=False):
        machine=MsrMachine(fixture,inputs)
        result=machine.run(symbol,instruction_limit=LIMIT,allow_instruction_bound=True)
        writes=[r for r in machine.io if r['op']=='write']
        reads=[r for r in machine.io if r['op']=='read']
        expected='instruction-limit' if hang else 'returned'
        if result['outcome']!=expected or machine.flag_reads:
            raise AssertionError((symbol,label,result,machine.flag_reads))
        if inputs.get('empty'):
            if writes or reads or 'Nothing to Write!' not in result['qt_texts']:raise AssertionError('empty field wrote')
        else:
            if writes[0]['high']!=SLOTS[symbol] or writes[0]['low']!=0:raise AssertionError('query differs')
            if not reads or reads[0]['buffer']!=writes[0]['buffer']:raise AssertionError('stack reuse differs')
            # Both original leaves use the same-sized frame. This is an observed
            # command value written by Wrmsr, not a fabricated read buffer seed.
            if reads[0]['before_hex']!=struct.pack('<II',0,SLOTS[symbol]).hex():
                raise AssertionError('initial read buffer was not the previous write command')
            if hang:
                if len(writes)!=1 or machine.reads<2 or 'Applied!' in result['qt_texts']:
                    raise AssertionError('unexpected hang path')
            else:
                low=inputs.get('reply_low',0x1234) & 0xffffff00
                low=(low if low else 0x26600) | (inputs.get('parsed_uint',37) & 0xff)
                if len(writes)!=2 or writes[1]['high']!=0x80000111 or writes[1]['low']!=low:
                    raise AssertionError('programmed payload differs')
                if len(reads)!=(4 if inputs.get('io_mode')=='delayed' else 1):
                    raise AssertionError('unexpected read count or final write verification')
                if 'Applied!' not in result['qt_texts']:raise AssertionError('missing original success label')
        rows.append({'case':label,'symbol':symbol,'inputs':inputs,'object_flag_read_count':machine.flag_reads,
                     'libc_io':machine.io,**result})

    for symbol,flag,parsed,reply in itertools.product(SLOTS,[0,1],[0,1,255,256,257,0xffffffff],[0,0xef,0x1234,0xffffffff]):
        run(symbol,'value-{}-{}-{:x}'.format(flag,parsed,reply),
            dict(constructor_flag=flag,parsed_uint=parsed,reply_low=reply))
    for symbol,flag in itertools.product(SLOTS,[0,1]):
        run(symbol,'empty-'+str(flag),dict(constructor_flag=flag,empty=True))
        for mode in ('open-error','read-error','read-eof','short-read-4','short-read-7','busy-forever',
                     'write-error','short-write','seek-error','close-error','delayed'):
            run(symbol,mode+'-flag-'+str(flag),dict(constructor_flag=flag,io_mode=mode),
                mode in ('open-error','read-error','read-eof','short-read-4','short-read-7','busy-forever'))
        run(symbol,'nonbusy-status-bits-'+str(flag),dict(constructor_flag=flag,reply_high=0x7fffffff))
    return {'schema':1,'source_elf_sha256':base.SOURCE_SHA,'fixture_sha256':FIXTURE_SHA,
            'scope':'Original UI and MSR leaf instructions with synthetic Qt/libc; no host syscall, MSR, kernel or GUI execution',
            'environment':dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
            'instruction_limit_per_case':LIMIT,'cases_passed':len(rows),
            'expected_bounded_loops':sum(r['outcome']=='instruction-limit' for r in rows),'observations':rows}


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--fixture',type=Path,default=FIXTURE)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=investigate(args.fixture)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
    print('{} cases characterized; {} expected bounded busy loops; no hardware executed'.format(
        result['cases_passed'],result['expected_bounded_loops']))


if __name__=='__main__':main()
