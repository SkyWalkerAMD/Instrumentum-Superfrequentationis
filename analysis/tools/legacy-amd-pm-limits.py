#!/usr/bin/env python3
"""Characterize original Shimada monitor labels/offsets using synthetic memory.

Only the pinned constructor and the common-field part of get_pm_patch execute.
All transport, topology, allocation and C++ library boundaries are synthetic.
Calibration is explicitly assumed complete; the per-core decoder never runs.
This proves original-program mappings, not firmware units or hardware behavior.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_RAX, UC_X86_REG_RDI, UC_X86_REG_RSI,
    UC_X86_REG_RDX, UC_X86_REG_RSP, UC_X86_REG_RIP, UC_X86_REG_FS_BASE)

FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-amd-pm-limits.json'
FIXTURE_SHA = '9936d25c9a2ffb61def0035529223cea0710a04ffeb2a067c9356897880f3475'
SOURCE_SHA = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
OBJ, STACK, HEAP, TLS, STOP = 0x61000000, 0x62000000, 0x63000000, 0x64000000, 0x65000000
REGS = dict(rax=UC_X86_REG_RAX, rdi=UC_X86_REG_RDI, rsi=UC_X86_REG_RSI,
            rdx=UC_X86_REG_RDX, rsp=UC_X86_REG_RSP, rip=UC_X86_REG_RIP)
VERSIONS = (0x730204,0x730404,0x730604,0x730804,0x730a04,0x730c04,0x730e04,0x731004)
NAMES = ('Fast PPT Limit','Fast PPT Value','TDC0 Limit','TDC0 Current Value',
    'TDC1 Limit','TDC1 Current Value','THM0 Limit','THM0 Current Value',
    'THM1 Limit','THM1 Current Value','FIT Limit','FIT Current Value',
    'VID0 Limit','VID0 Current Value','VID1 Limit','VID1 Current Value',
    'EDC_LIMIT_0','PEAK_TELEMETRY_CURRENT_0','PCC_LIMIT_0','PCC_RESIDENCY_0',
    'EDC_LIMIT_1','PEAK_TELEMETRY_CURRENT_1','PCC_LIMIT_1','PCC_RESIDENCY_1')
OFFSETS = tuple(range(0,64,4)) + tuple(range(0x1c8,0x1e8,4))


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('PM monitor fixture hash mismatch')
    fixture = json.loads(raw)
    assert fixture['source_sha256'] == SOURCE_SHA
    return fixture


class Machine:
    def __init__(self, fixture, version, ccds=4, pattern=0):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.pages, self.instructions, self.calls = set(), {}, set()
        self.heap, self.steps, self.phase = HEAP, 0, 'constructor'
        self.max_steps = 30000
        self.version, self.ccds, self.pattern = version, ccds, pattern
        self.reads, self.events = [], []
        for fn in fixture['functions']:
            code = bytes.fromhex(fn['code_hex'])
            assert len(code) == fn['size'] and hashlib.sha256(code).hexdigest() == fn['sha256']
            self.map(fn['address'],len(code)); self.uc.mem_write(fn['address'],code)
            self.instructions.update({pc:mn for pc,_,mn,_ in Cs(CS_ARCH_X86,CS_MODE_64).disasm_lite(code,fn['address'])})
        for item in fixture['data']:
            data = bytes.fromhex(item['hex']); self.map(item['address'],len(data)); self.uc.mem_write(item['address'],data)
        for address in fixture['boundaries']:
            self.calls.add(address); self.map(address,1)
            if address not in self.instructions: self.uc.mem_write(address,b'\xc3')
        for name,address in fixture['globals'].items():
            self.map(address,1); self.uc.mem_write(address,bytes([name == 'GLOBAL_IS_SHIMADA']))
        for address,size in ((OBJ,0x10000),(STACK,0x10000),(HEAP,0x100000),(TLS,4096),(STOP,4096)):
            self.uc.mem_map(address,size)
        self.uc.reg_write(UC_X86_REG_FS_BASE,TLS)
        self.uc.hook_add(UC_HOOK_CODE,self.hook)

    def map(self,address,size):
        for page in range(address & ~4095,(address+size+4095)&~4095,4096):
            if page not in self.pages: self.uc.mem_map(page,4096); self.pages.add(page)

    def reg(self,name,value=None):
        if value is None: return self.uc.reg_read(REGS[name])
        self.uc.reg_write(REGS[name],value & ((1<<64)-1))

    def u64(self,address): return struct.unpack('<Q',self.uc.mem_read(address,8))[0]
    def p64(self,address,value): self.uc.mem_write(address,struct.pack('<Q',value & ((1<<64)-1)))

    def alloc(self,size):
        assert 0 <= size <= 65536
        address = self.heap; self.heap += (max(1,size)+15)&~15
        assert self.heap < HEAP+0x100000
        return address

    def string(self,address,text):
        data = text.encode(); ptr = address+16 if len(data)<16 else self.alloc(len(data)+1)
        self.p64(address,ptr); self.p64(address+8,len(data)); self.uc.mem_write(ptr,data+b'\0')
        if ptr != address+16: self.p64(address+16,len(data))

    def cppstr(self,address):
        length = self.u64(address+8); assert length < 128
        return bytes(self.uc.mem_read(self.u64(address),length)).decode()

    def vector(self,address,items,strings=True):
        width = 32 if strings else 4; ptr = self.alloc(max(16,len(items)*width))
        for index,value in enumerate(items):
            if strings: self.string(ptr+index*width,value)
            else: self.uc.mem_write(ptr+index*width,struct.pack('<I',value))
        for index,value in enumerate((ptr,ptr+len(items)*width,ptr+len(items)*width)):
            self.p64(address+index*8,value)

    def ret(self,value=0):
        sp = self.reg('rsp'); dest = self.u64(sp)
        self.reg('rsp',sp+8); self.reg('rax',value); self.reg('rip',dest)

    def bits(self,offset):
        if self.pattern == 0: return 0x3f800000+offset*0x100
        return (0x80000000,0,0x7f800000,0xff800000,0x7fc12345,0x7f800001,
                0xbf800000,1,0x7f7fffff)[(offset//4)%9]

    def hook(self,uc,address,size,context):
        self.steps += 1; assert self.steps < self.max_steps
        if address == STOP or self.phase == 'decode' and address == 0x2e7b8e:
            uc.emu_stop(); return
        if address in self.instructions:
            assert self.instructions[address] not in ('in','out','rdmsr','wrmsr','syscall','cpuid')
            return
        assert address in self.calls, 'Execution left the pinned instruction set'
        p,s,d = self.reg('rdi'),self.reg('rsi'),self.reg('rdx')
        if address == 0x22eeb0:
            self.string(p,bytes(uc.mem_read(s,128)).split(b'\0')[0].decode())
        elif address in (0x250e80,0x22f970,0x22f5e0):
            count = (d-s)//32 if address == 0x22f5e0 else d; assert count<100
            self.vector(p,[self.cppstr(s+i*32) for i in range(count)])
        elif address == 0x2c55e0:
            self.vector(p,[self.cppstr(q) for q in range(self.u64(p),self.u64(p+8),32)]+[self.cppstr(s)])
        elif address in (0x254770,0x2508d0,0x22f380):
            count = (d-s)//4 if address == 0x22f380 else d; assert count<100
            self.vector(p,struct.unpack('<'+str(count)+'I',uc.mem_read(s,count*4)),False)
        elif address in (0x14dbb0,0x14a2a0,0x230de0,0x22ee60): pass
        elif address in (0x21dbb0,0x14ae40): self.ret(-1); return
        elif address in (0x21db90,0x21e510): self.ret(0); return
        elif address in (0x2365c0,0x236820,0x2254c0): self.events.append(hex(address))
        elif address == 0x236890: self.ret(0x80000000); return
        elif address == 0x236b80: self.ret(self.version); return
        elif address == 0x225480: self.ret(1); return
        elif address == 0x21e770: self.ret(self.ccds); return
        elif address == 0x21e6f0: self.ret(self.ccds*16); return
        elif address == 0x3a2bf0: self.string(p,'Synthetic board')
        elif address == 0x230ea0:
            assert p == OBJ+0xc78 and s%4 == 0 and s<11636
            self.reads.append(s); self.ret(self.bits(s)); return
        elif address in (0x2dad40,0x2daeb0):
            src = d if address == 0x2dad40 else s
            begin,end = self.u64(p),self.u64(p+8)
            contents = bytes(uc.mem_read(begin,end-begin)) if end!=begin else b''
            contents += bytes(uc.mem_read(src,4)); dest = self.alloc(len(contents)+4096)
            uc.mem_write(dest,contents); self.p64(p,dest)
            self.p64(p+8,dest+len(contents)); self.p64(p+16,dest+len(contents)+4096)
        else: raise ValueError('Unhandled synthetic boundary '+hex(address))
        self.ret()

    def run(self):
        self.reg('rsp',STACK+0xfff8); self.p64(STACK+0xfff8,STOP); self.reg('rdi',OBJ)
        self.uc.emu_start(0x2c7910,STOP+1)
        names=[]
        for offset,count in ((0xb8,16),(0x268,8)):
            begin,end=self.u64(OBJ+offset),self.u64(OBJ+offset+8)
            assert end-begin == count*32
            names.extend(self.cppstr(begin+i*32) for i in range(count))
        assert tuple(names)==NAMES
        self.phase='decode'
        # Synthetic prerequisite. Do not execute the original scanning calibration.
        self.uc.mem_write(OBJ+0xcdc,b'\1')
        self.reg('rsp',STACK+0xfff8); self.p64(STACK+0xfff8,STOP)
        self.reg('rdi',OBJ); self.reg('rsi',0); self.uc.emu_start(0x2e7360,STOP+1)
        assert self.reg('rip') in (STOP,0x2e7b8e), 'Must stop before per-core processing'
        values=[]
        for offset,count in ((0xd0,16),(0x280,8)):
            begin,end=self.u64(OBJ+offset),self.u64(OBJ+offset+8)
            assert end-begin == count*4
            values.extend(struct.unpack('<'+str(count)+'I',self.uc.mem_read(begin,end-begin)))
        assert values==[self.bits(offset) for offset in OFFSETS]
        assert all(offset in self.reads for offset in OFFSETS)
        return dict(version=hex(self.version),synthetic_ccds=self.ccds,pattern=self.pattern,
                    steps=self.steps,common_reads=len(self.reads),monitor_bits=values)


def research(fixture):
    cases=[Machine(fixture,version,ccds,pattern).run() for version in VERSIONS
           for ccds in (1,2,4,8,10,12,14,16) for pattern in (0,1)]
    return dict(schema=1,source_sha256=SOURCE_SHA,fixture_sha256=FIXTURE_SHA,
        hardware_execution=False,case_count=len(cases),cases=cases,
        fields=[dict(name=name,offset=hex(offset),unit_verified=False) for name,offset in zip(NAMES,OFFSETS)],
        scope='Original labels and binary32 storage mappings only. Synthetic transport/topology/library calls; calibration assumed complete; stop before per-core processing. No physical units, rail identity, setter contract or hardware validation established.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); result=research(load_fixture())
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(str(result['case_count'])+' original PM monitor cases passed; no hardware executed')
