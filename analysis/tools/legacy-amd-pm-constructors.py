#!/usr/bin/env python3
"""Compare original PM constructor state with synthetic C++/MMIO boundaries.
Requires the hash-verified reference ELF; never launches it on the host.
"""
import argparse
from pathlib import Path
import importlib.util,hashlib,io,json,struct
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_MEM,X86_OP_IMM,X86_REG_RIP
parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
spec=importlib.util.spec_from_file_location('limits',Path(__file__).with_name('legacy-amd-pm-limits.py'));pm=importlib.util.module_from_spec(spec);spec.loader.exec_module(pm)
path=args.binary;raw=path.read_bytes();assert hashlib.sha256(raw).hexdigest()==pm.SOURCE_SHA
elf=ELFFile(io.BytesIO(raw));syms={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
def read(a,n):
 for seg in elf.iter_segments():
  r=a-seg['p_vaddr']
  if seg['p_type']=='PT_LOAD' and 0<=r and r+n<=seg['p_memsz']:
   data=seg.data()[r:r+n];return data+bytes(n-len(data))
 raise ValueError(hex(a))
fixture=pm.load_fixture();refs=set();calls=set(fixture['boundaries']);cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True
for name in ('_ZN10AMD_PM_LOGC2Ei','_ZN10AMD_PM_LOGC2Eiij'):
 s=syms[name];addr=s['st_value'];code=read(addr,s['st_size'])
 fixture['functions'].append(dict(symbol=name,address=addr,size=len(code),sha256=hashlib.sha256(code).hexdigest(),code_hex=code.hex()))
 for i in cs.disasm(code,addr):
  for op in i.operands:
   if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:refs.add(i.address+i.size+op.mem.disp)
   if i.mnemonic=='call' and op.type==X86_OP_IMM:calls.add(op.imm)
fixture['data'] += [dict(address=a,hex=read(a,512).hex()) for a in sorted(refs)]
fixture['boundaries']=sorted(calls)
class Machine(pm.Machine):
 def __init__(self,*args):super().__init__(*args);self.max_steps=150000;self.mmio=[]
 def hook(self,uc,address,size,context):
  if address in (0x230e90,0x250a90):self.ret();return
  if address==0x230ea0:
   p,s=self.reg('rdi'),self.reg('rsi');assert s%4==0 and s<65536,(hex(p),hex(s))
   self.mmio.append(s);self.ret(self.bits(s));return
  if address==0x14c6b0:self.ret(self.alloc(self.reg('rdi')));return
  if address==0x14a460:
   p,s,n=self.reg('rdi'),self.reg('rsi'),self.reg('rdx');assert n<=65536
   uc.mem_write(p,bytes(uc.mem_read(s,n)));self.ret(p);return
  if address in (0x28cb30,0x28c870):
   p,s=self.reg('rdi'),self.reg('rdx') if address==0x28c870 else self.reg('rsi');begin,last=self.u64(s),self.u64(s+8);assert 0<=last-begin<=4096
   data=bytes(uc.mem_read(begin,last-begin));dest=self.alloc(len(data));uc.mem_write(dest,data)
   start,row,end=self.u64(p),self.u64(p+8),self.u64(p+16)
   if row+24>end:
    contents=bytes(uc.mem_read(start,row-start));start=self.alloc(len(contents)+32*24);uc.mem_write(start,contents)
    row=start+len(contents);self.p64(p,start);self.p64(p+16,start+len(contents)+32*24)
   for off,v in ((0,dest),(8,dest+len(data)),(16,dest+len(data))):self.p64(row+off,v)
   self.p64(p+8,row+24);self.ret();return
  if address in (0x21cfd0,):raise ValueError('Unmodelled '+hex(address))
  super().hook(uc,address,size,context)
 def construct(self,addr,param):
  from unicorn.x86_const import UC_X86_REG_RCX
  self.uc.mem_write(pm.OBJ+0xcf0,struct.pack('<I',0xdeadbeef))
  self.reg('rsp',pm.STACK+0xfff8);self.p64(pm.STACK+0xfff8,pm.STOP)
  self.reg('rdi',pm.OBJ);self.reg('rsi',param);self.reg('rdx',0);self.uc.reg_write(UC_X86_REG_RCX,0)
  self.uc.emu_start(addr,pm.STOP+1)
  return dict(entry=hex(addr),param=param,version=hex(self.version),input=self.ccds,
    members={hex(o):hex(struct.unpack('<I',self.uc.mem_read(pm.OBJ+o,4))[0]) for o in (0x34,0x60,0x68,0x6c,0x70,0xcf0)},steps=self.steps,mmio=self.mmio)
rows=[]
for version in pm.VERSIONS:
 for addr in (0x2c7910,0x257550,0x2df0e0):
  for param in ((0,2) if addr==0x2df0e0 else (0,)):
   for n in (2,6,12,16):
    m=Machine(fixture,version,n)
    try:r=m.construct(addr,param)
    except Exception as e:r=dict(entry=hex(addr),param=param,version=hex(version),input=n,error=repr(e),rip=hex(m.reg('rip')),mmio=m.mmio)
    rows.append(r)
 print('Version',hex(version),'complete:',len(rows),'constructor cases',flush=True)
assert all('error' not in r for r in rows), 'An original path is incomplete'
assert len(rows)==128
for row in rows:
 expected= row['version'] if row['entry']=='0x2c7910' else '0xdeadbeef'
 assert row['members']['0xcf0']==expected, row
 if row['entry']=='0x2df0e0':
  assert all(row['members'][hex(off)]=='0x0' for off in (0x68,0x6c,0x70))
  assert len(row['mmio'])==(63 if row['param']==0 else 320)
 else:
  assert not row['mmio']
  assert int(row['members']['0x70'],16)==(2 if row['input']==6 else row['input'])
args.output.write_text(json.dumps(dict(source_sha256=pm.SOURCE_SHA,functions=[{k:v for k,v in x.items() if k!='code_hex'} for x in fixture['functions']],hardware_execution=False,scope='Synthetic family 0/Shimada, zero-initialized object except CF0 sentinel, library and transport boundaries; constructor arguments do not certify a physical topology or a caller lifetime. No byte offsets are inferred from sentinel values.',cases=rows),indent=2)+'\n')
