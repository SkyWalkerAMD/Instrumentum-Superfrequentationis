#!/usr/bin/env python3
"""Replay APOB enumeration through the pinned UEFI APIC record consumer.

The constructor, sorter and provider getters execute their original bytes.
Boot-service lookup, fabric counts, proximity domains and configuration are
explicit fixtures. Nothing binds a PM slot or certifies the runtime provider.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

import pefile
import unicorn as u
from unicorn import x86_const as x

H = runpy.run_path(str(Path(__file__).with_name('firmware-granite-topology-protocol.py')))
PBASE, DATA, STACK, STOP = H['PROVIDER_BASE'], H['DATA'], H['STACK'], H['STOP']
OBJECT, RECORD, OUT = H['OBJECT'], H['RECORD'], H['OUT']
FABRIC, NODE, CONFIG, BOOT, OUTPUT_POINTER = (DATA+i for i in (0x5000,0x6000,0x7000,0x8000,0x9000))
LOOKUP, SOCKETS, DIES, PROXIMITY, GET_CONFIG = (0xe000+i*16 for i in range(5))


def replay(provider, topology, functions, outer, middle, cores, flags, smt, type3, x2):
    vm=u.Uc(u.UC_ARCH_X86,u.UC_MODE_64)
    for address in (0,PBASE,DATA,0x100000): vm.mem_map(address,0x20000)
    vm.mem_write(0,topology);vm.mem_write(PBASE,provider)
    put=lambda a,v:vm.mem_write(a,struct.pack('<Q',v))
    get=lambda a:struct.unpack('<Q',vm.mem_read(a,8))[0]
    put(0x7c98,BOOT);put(BOOT+0x140,LOOKUP)
    put(FABRIC,SOCKETS);put(FABRIC+8,DIES);put(NODE+0x10,PROXIMITY);put(CONFIG+8,GET_CONFIG)
    for i,fn in enumerate(functions[:5]):put(OBJECT+0x20+i*8,PBASE+fn)
    for off,value in zip((0x50,0x58,0x60),(1,1,RECORD)):put(OBJECT+off,value)
    data=H['records'](outer,middle,cores,flags);vm.mem_write(RECORD,bytes(data))
    put(OUTPUT_POINTER,OUT);put(STACK,STOP)
    vm.reg_write(x.UC_X86_REG_RSP,STACK);vm.reg_write(x.UC_X86_REG_R8,OUTPUT_POINTER)
    queries=[];coordinates=[];diagnostics=[];seen=set();services=[]
    reg=lambda r:vm.reg_read(r)

    def returned(value):
        sp=reg(x.UC_X86_REG_RSP);ret=get(sp)
        vm.reg_write(x.UC_X86_REG_RAX,value);vm.reg_write(x.UC_X86_REG_RSP,sp+8)
        vm.reg_write(x.UC_X86_REG_RIP,ret)

    def instruction(uc,address,size,_):
        sp=reg(x.UC_X86_REG_RSP)
        if address==LOOKUP:
            guid=reg(x.UC_X86_REG_RCX)
            target={0x7140:NODE,0x7320:FABRIC,0x7370:OBJECT}[guid]
            put(reg(x.UC_X86_REG_R8),target);services.append(guid);returned(0);return
        if address==SOCKETS:put(reg(x.UC_X86_REG_RDX),1);returned(0);return
        if address==DIES:put(reg(x.UC_X86_REG_R8),1);returned(0);return
        if address==PROXIMITY:
            # Fixture only: unrelated to APIC or physical proximity validity.
            pointer=get(sp+48)
            vm.mem_write(pointer,struct.pack('<I',100+reg(x.UC_X86_REG_R9)))
            returned(0);return
        if address==0x3b7c:returned(CONFIG);return
        if address==GET_CONFIG:
            assert reg(x.UC_X86_REG_RCX)==0x431
            returned(2 if x2 else 0);return
        if address==0x3acc:
            diagnostics.append(reg(x.UC_X86_REG_ECX));returned(0);return
        assert (0x2d24<=address<0x30de or 0x54e0<=address<0x569a or
                0x526c<=address<0x53f9 or 0x3a0<=address<0x3e0 or
                PBASE+functions[0]<=address<PBASE+functions[5]),hex(address)
        assert bytes(uc.mem_read(address,size)) not in (b'\x0f\x32',b'\x0f\x30')
        if address==0x526c:
            coordinates.append(tuple(reg(r) for r in (x.UC_X86_REG_RCX,x.UC_X86_REG_RDX,
                                                     x.UC_X86_REG_R8,x.UC_X86_REG_R9))+
                               (get(sp+40),get(sp+48)))
        seen.add(address)

    def cpuid(uc,_):
        leaf,sub=reg(x.UC_X86_REG_EAX),reg(x.UC_X86_REG_ECX);queries.append((leaf,sub))
        if leaf==0xb:assert sub==0;words=(smt,1<<smt,1<<8,0)
        else:
            assert leaf==0x80000026 and sub<4
            levels=((1,smt),(2,3+smt),(3,4+smt),(4,7+smt)) if type3 else (
                    (1,smt),(2,3+smt),(4,5+smt),(0,0))
            kind,shift=levels[sub];words=(shift,1<<shift if kind else 0,kind<<8|sub,0)
        for r,val in zip((x.UC_X86_REG_EAX,x.UC_X86_REG_EBX,x.UC_X86_REG_ECX,x.UC_X86_REG_EDX),words):
            uc.reg_write(r,val)
        return True

    def write(uc,access,address,size,value,_):
        assert (0x100000<=address and address+size<=0x120000 or
                OUT<=address and address+size<=OUT+0x2000 or
                OUTPUT_POINTER<=address and address+size<=OUTPUT_POINTER+8),hex(address)

    vm.hook_add(u.UC_HOOK_CODE,instruction);vm.hook_add(u.UC_HOOK_MEM_WRITE,write)
    vm.hook_add(u.UC_HOOK_INSN,cpuid,None,1,0,x.UC_X86_INS_CPUID)
    vm.emu_start(0x2d24,STOP,count=300000)
    assert reg(x.UC_X86_REG_RIP)==STOP and reg(x.UC_X86_REG_RAX)==0
    assert services==[0x7140,0x7320,0x7370] and not diagnostics
    count=0
    for flag in flags:
        if not flag:break
        count+=1
    order=sorted(range(len(outer)),key=lambda i:H['ORDER'].index(outer[i]))
    expected_coordinates=[(0,0,g,s,c,t) for g in order for s in range(len(middle))
                          for c in range(len(cores)) for t in range(count)]
    assert coordinates==expected_coordinates
    expected=[(g<<(4+smt) if type3 else g)+(s<<(3+smt))+(c<<smt)+(t if smt else 0)
              for _,_,g,s,c,t in coordinates]
    assert queries==([(0xb,0)]+[(0x80000026,i) for i in range(4)])*len(coordinates)
    width=24 if x2 else 16
    selected=[i for i,a in enumerate(expected) if x2 or a<255]
    size=get(OUTPUT_POINTER)-OUT;assert size==len(selected)*width
    payload=bytes(vm.mem_read(OUT,size));apics=[]
    for i,source in enumerate(selected):
        record=payload[i*width:(i+1)*width]
        assert record[:2]==(b'\x02\x18' if x2 else b'\x00\x10')
        apic=struct.unpack_from('<I',record,8)[0] if x2 else record[3]
        flags_word=struct.unpack_from('<I',record,12 if x2 else 4)[0]
        assert apic==expected[source] and flags_word==1
        apics.append(apic)
    return dict(coordinates=coordinates,apics=apics,record_bytes=size,
                duplicate_apics=len(apics)-len(set(apics)),trace=hashlib.sha256(payload).hexdigest(),
                instructions=len(seen))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('provider-a','provider-b','topology','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();assert u.__version__=='2.1.4' and pefile.__version__=='2024.8.26'
    topology=H['image'](a.topology,H['TOPOLOGY_SHA']);reports={}
    for key,path in (('a',a.provider_a),('b',a.provider_b)):
        sha,functions=H['PROVIDERS'][key];provider=H['image'](path,sha)
        trace=hashlib.sha256();cases=records=collisions=0;example=None
        for outer,middle,n,flags,smt,type3,x2 in itertools.product(
                ((0,),(8,4),(4,0,12,8)),((0,),(1,0)),(1,4,8),((1,1),(1,0),(0,1)),
                (0,1),(False,True),(False,True)):
            # Reverse non-contiguous stored core IDs, retaining enumeration
            # positions. The constructor receives ordinals, not these IDs.
            cores=tuple(range(15,15-n,-1))
            result=replay(provider,topology,functions,outer,middle,cores,flags,smt,type3,x2)
            # Providers have different instruction bodies. Compare observable
            # coordinates and output bytes, not their instruction counts.
            observable={k:v for k,v in result.items() if k!='instructions'}
            trace.update(json.dumps([outer,middle,n,flags,smt,type3,x2,observable],sort_keys=True).encode())
            cases+=1;records+=len(result['apics']);collisions+=bool(result['duplicate_apics'])
            if (outer,middle,n,flags,smt,type3,x2)==((8,4),(0,),1,(1,1),1,False,True):
                example=result
        assert example['apics']==[1,2,0,1]
        reports[key]=dict(source_sha256=sha,cases=cases,output_records=records,
            synthetic_duplicate_apic_cases=collisions,absent_type3_example=example,
            trace_sha256=trace.hexdigest())
        print('Completed provider',key,':',cases,'enumeration cases',flush=True)
    assert reports['a']['trace_sha256']==reports['b']['trace_sha256']
    report=dict(schema=1,hardware_execution=False,topology_sha256=H['TOPOLOGY_SHA'],providers=reports,
        entry='0x2d24',sorter='0x54e0',constructor='0x526c',constructor_receives_enumeration_ordinals=True,
        stored_ids_only_reorder_outer_ordinals=True,cpuid_responses_are_fixtures=True,
        physical_or_os_core_mapping_verified=False,pm_storage_binding_verified=False,runtime_write_enabled=False,
        limitations=['Fabric counts, proximity domains, configuration and protocol lookup are stubs.',
          'Both topology shapes are synthetic. The fixture does not prove which provider or inputs run on the target.',
          'Duplicate APICs in arbitrary inputs refute an unconditional mapping assumption, not correct BIOS behavior.'])
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__=='__main__':main()
