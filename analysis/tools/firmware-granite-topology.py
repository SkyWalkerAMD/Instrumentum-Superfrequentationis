#!/usr/bin/env python3
"""Replay Granite startup packets and PM copy order on synthetic memory.

Physical address writes below are captured in an in-memory dictionary; there is
no hardware access. Firmware target IDs are not CPUID CCD/APIC IDs or OS CPUs.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

SMU_SHA='5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596'
MP5_SHA='fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556'


def instructions(path,body,sha):
    assert hashlib.sha256(body).hexdigest()==sha
    s=json.loads(path.read_text()); assert s['source_sha256']==sha
    assert not s['function_selection_truncated']
    code={int(i['address'],16):i for f in s['functions'] for i in f['instructions']}
    for address,i in code.items():
        raw=bytes.fromhex(i['bytes']); assert body[address:address+len(raw)]==raw
    return code


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('smu','mp5','smu-study','mp5-study','output'): p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args(); smu=a.smu.read_bytes();mp5=a.mp5.read_bytes()
    sc=instructions(a.smu_study,smu,SMU_SHA);mc=instructions(a.mp5_study,mp5,MP5_SHA)
    u=lambda address:struct.unpack_from('<I',smu,address)[0]
    assert sc[0x1d7ec]['text']=='entry a1,0xd0'
    assert u(0x791c+3*8)==0x1d7ec
    assert u(0x114ec)==0x6814 and u(0x11500)==0x8164
    assert u(0x1151c)==0x15660 and u(0x114e4)==0xf8000000 and u(0x11430)==0xfffff
    # The transport index 7/8 becomes target 0/1 before the callback pointer is saved.
    assert sc[0x1d7cf]['text']=='addi a2,a2,-0x7'
    assert sc[0x1d7d2]['text']=='extui a2,a2,0x0,0x8'
    assert mc[0x104a]['text']=='movi.n a10,0x3' and mc[0x104e]['text']=='call8 0x0002e848'
    assert struct.unpack_from('<I',mp5,0xbb8)[0]==0x19d20
    here=Path(__file__).parent
    Slice=runpy.run_path(str(here/'firmware-granite-semantics.py'))['Slice']
    copy=runpy.run_path(str(here/'firmware-granite-proof.py'))['replay']
    cases=[];packet_count=copy_words=0;trace=hashlib.sha256()
    for present,first,second in itertools.product((1,2,3),(0,1,0x80,0x81,0xff),(0,1,0x80,0x81,0xff)):
        core_masks=[first if present&1 else 0,second if present&2 else 0]
        active=sum(1<<i for i,mask in enumerate(core_masks) if mask)
        ids=[i for i in range(2) if present&(1<<i)];width=len(ids)
        vm=Slice(smu,sc);vm.r.update(a1=0x50000,a5=0x8164,a6=0x6814)
        for start,size in ((0x8164,0x300),(0xae30,0x200),(0x8ae4,0x40)):
            for offset in range(size):vm.put(start+offset,0,1)
        vm.put(0x50098,present);vm.put(0x5008c,active)
        vm.put(0x8164+0x2a,width,2);vm.put(0x8164+0x2c,active.bit_count(),2)
        for i,mask in enumerate(core_masks):vm.put(0x8164+0x34+2*i,mask,2)
        # Select a supplied configuration byte, avoiding the fuse-reader call.
        vm.put(0x81e4+0xf9,1,1);vm.put(0x81e4+0xfa,7,1)
        for i in range(2):vm.put(0x6814+4+4*i,0x8019d20)
        for i in range(4):vm.putf(0xaf18+4*i,1+i)
        vm.run(0x1d827,0x1da2e)
        expected_addresses=set()
        for slot,target in enumerate(ids):
            base=0xf8000000+(target+2)*0x100000+0x19d20
            expected_addresses.update(range(base,base+0x70))
            packet=bytes(vm.get(base+i,1) for i in range(0x70))
            assert packet[0]==target and packet[0x18]==slot and packet[0x11]==width
            assert packet[2]==sum(bool(active&(1<<i)) for i in range(target))
            assert packet[0x10]==int(bool(active&(1<<target)) and not (active&((1<<target)-1)))
            assert struct.unpack_from('<I',packet,0x3c)[0]==0x15660
            assert packet[0x44:0x54]==struct.pack('<4f',1,2,3,4)
            writes,_=copy(mp5,mc,packet[0x11],packet[0x18])
            for row in range(18):
                for core in range(8):
                    assert writes[0x4b4+4*(row*8*width+slot*8+core)]==0x3f000000+row*8+core
            for row in range(12):
                assert writes[0x4b4+18*8*width*4+4*(row*width+slot)]==0x3f000000+144+row
            packet_count+=1;copy_words+=len(writes);trace.update(packet)
        assert {address for address in vm.ram if address>=0xf8000000}==expected_addresses
        cases.append(dict(present_mask=present,active_mask=active,core_masks=core_masks,
                          firmware_target_ids_by_storage_slot=ids,width=width))
    report=dict(schema=1,hardware_execution=False,source_sha256=dict(smu=SMU_SHA,mp5=MP5_SHA),
        initialization_handler='0x1d7ec',initialization_slice='0x1d827..0x1da2e',
        synthetic_cases=len(cases),copied_packets=packet_count,checked_pm_words=copy_words,
        packet_bytes=0x70,packet_target_offset=0,packet_active_rank_offset=2,
        packet_storage_rank_offset=0x18,packet_width_offset=0x11,packet_pm_destination_offset=0x3c,
        presence_mask='ushort[0x6814]',active_mask='ushort[0x6816]',
        mapping='ascending set bits of presence mask; core availability does not compact storage rank',
        two_slot_consequence='within this two-target firmware, width 2 implies storage IDs [0,1]',
        one_slot_ambiguity='width 1 can mean target 0 or target 1 without the presence mask',
        trace_sha256=trace.hexdigest(),cases=cases,
        complete_startup_emulated=False,mailbox_timing_verified=False,
        per_core_physical_mapping_verified=False,cpuid_ccd_mapping_verified=False,
        physical_hardware_acceptance='not_run')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='cases'}))


if __name__=='__main__':main()
