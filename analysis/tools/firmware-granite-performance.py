#!/usr/bin/env python3
"""Trace Granite performance-request policy rows through synthetic RAM only.

The mailbox, fuse and synchronization boundaries are explicit fixtures. The
frequency unit follows the externally documented MHz Fmax command convention
and the byte-checked x0.001 / x1000 data path. These are policy values, not
measured clocks, physical core identities, or fresh setting readback.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

HERE=Path(__file__).parent
H=runpy.run_path(str(HERE/'firmware-granite-semantics.py'))
R=runpy.run_path(str(HERE/'firmware-granite-registers.py'))
Slice,f32=H['Slice'],H['f32']
STATE=0x15884
SCALE=0x119a0
OUTPUT=0x11278
bits=lambda v:struct.unpack('<I',struct.pack('<f',v))[0]


def scale_sources(smu,sc,mp5,mc):
    u=lambda body,a:struct.unpack_from('<I',body,a)[0]
    assert u(smu,0x6fa4+8*0x70)==0x29e54
    assert u(smu,0x6fa4+8*0x6e)==0x2a7c8
    assert u(smu,0x6fa4+8*0xe7)==0x29fc4
    # Queue 1 dispatch base and bound, as consumed by 0x10c8.
    assert u(mp5,0x13558+0x1fc)==0x13298 and u(mp5,0x13558+0x204)==0x97
    for command,entry in ((0x32,0x2b33c),(0x90,0x2b53c),(0x72,0x2b558)):
        assert u(mp5,0x13298+8*command)==entry
    assert mc[0x2b3dc]['text']=='call8 0x0002ad20'
    assert mc[0x2b54d]['text']=='call8 0x0002ad20'
    assert mc[0x2b55d]['text']=='call8 0x000011b4'
    assert sc[0x29dab]['text']=='ssi f8,a1,0x8'  # initial command 32 record
    cases=0; trace=hashlib.sha256()
    for mhz in (1,25,600,1200,3000,4500,5000,5500,5750,6000,6375,65535,1048575):
        vm=Slice(smu,sc);vm.r.update(a1=0x50000,a10=mhz)
        vm.putf(0xae30+0xd4,3);vm.putf(0xae30+0xb8,1.2);vm.put(0xb8a8+2,100,1)
        # E7 handler after its mailbox-argument helper, before broadcast.
        vm.run(0x29fcc,0x2a022)
        expected=f32(f32(mhz)*f32(.001))
        conversion=Slice(smu,sc);conversion.r['a3']=mhz
        conversion.run(0x29e66,0x29e86)  # 70 input conversion, before policy clipping
        assert conversion.f['f4']==expected
        assert vm.getf(0xae30+0xb4)==expected
        assert vm.r['a10']==0x72 and vm.r['a11']==bits(expected)
        assert all(vm.getf(0xae88+4*i)==expected for i in range(18))
        rx=Slice(mp5,mc);rx.r['a10']=vm.r['a11']
        rx.run(0x2b560,0x2b569)
        assert rx.getf(SCALE)==expected
        # 6E reads the very same shared SMU float; round is nearest-even for
        # these finite fixtures. It reads storage, not a device frequency.
        vm.run(0x2a7cb,0x2a7df)
        assert vm.r['a11']==mhz
        # 70's accepted-policy branch packages f0 at record+8. The earlier
        # policy clipping is outside this slice; use the selected GHz fixture.
        vm.r.update(a1=0x50000);vm.f.update(f0=expected,f3=.125)
        vm.run(0x29f23,0x29f74)
        assert vm.r['a10']==0x90 and vm.r['a11']==0x50000 and vm.r['a12']==6
        assert vm.getf(0x50008)==expected
        rx.r.update(a1=0x60000,a2=0x50000)
        for offset in range(24):rx.put(0x50000+offset,vm.ram.get(0x50000+offset,0),1)
        rx.run(0x2ad20,0x2ad34)
        assert rx.getf(0x60004)==expected
        rx.r['a10']=48  # explicit fuse return, not a live fuse access
        rx.run(0x2ad42,0x2ad5a)
        assert rx.getf(SCALE)==expected
        rx.run(0x295f8,0x29655)
        assert all(rx.getf(0x11440+4*i)==expected for i in range(4))
        assert rx.getf(0x11458)==expected
        trace.update(struct.pack('<If',mhz,expected));cases+=1
    return dict(cases=cases,shared_smu_float='0xaee4',mp5_scale_float=hex(SCALE),
        rsmu_commands={'0x70':'policy-clipped Fmax setter','0x6e':'stored Fmax getter','0xe7':'direct scale handler'},
        mp5_commands={'0x32':'initial record','0x90':'replacement record','0x72':'direct float argument'},
        smu_to_mp5_transport_executed=False,mailbox_and_fuse_helpers_stubbed=True,
        prior_fmax_policy_clipping_replayed=False,unit='GHz',
        unit_basis='MHz Fmax command convention; binary32 x0.001 on input and x1000 on stored getter',
        measurement=False,runtime_write_enabled=False,trace_sha256=trace.hexdigest())


def pack(values):
    return sum(v<<(8*i) for i,v in enumerate(values))


def expected_request(first,second,threads,policy,floor,scale):
    maximum,minimum,desired,preference=first
    if threads>=2:
        maximum=max(maximum,second[0]);minimum=max(minimum,second[1])
        desired=max(desired,second[2]);preference=min(preference,second[3])
    if policy:
        maximum=min(maximum,120);minimum=max(minimum,30)
        preference=max(min(preference,150),50)
    upper=min(maximum,166);minimum=max(minimum,floor)
    selected=min(max(desired,minimum),maximum,166) if desired else 0
    high,low=(selected,selected) if selected else (upper,minimum)
    conv=lambda code:f32(scale*f32(f32(code)*f32(1/166)))
    return conv(high),conv(low),preference,upper,minimum,selected


def seed_request(vm,core,scale,threads,policy,floor,first,second,missing=0,cached=None):
    vm.r.update(a1=0x50000,a2=core)
    for offset in range(0x80):vm.put(STATE+offset,0,1)
    vm.put(STATE+1,threads,1);vm.put(STATE+2,floor,1);vm.put(STATE+6,policy,1)
    for i,value in enumerate((120,30,150,50)):vm.put(STATE+7+i,value,1)
    vm.putf(SCALE,scale)
    # Missing current words have independent cached inputs. Both-zero is a
    # separate early-return condition, even when nonzero caches are available.
    for j,values in enumerate((first,second)):
        vm.put(0x1603038+0x20000*core+4*j,0 if missing&(1<<j) else pack(values))
        vm.put(STATE+8*core+0x34+4*j,pack(cached[j] if cached else values))
    for offset in (0x1f4,0x204):vm.putf(OUTPUT+0x3c*core+offset,17+core)
    vm.put(STATE+0x2c+core,231,1)


def request_paths(body,code):
    cases=retained=equal=unbounded_low=0;trace=hashlib.sha256()
    for core,value,threads,policy,missing in itertools.product(range(8),range(256),(1,2),(0,1),range(4)):
        vm=Slice(body,code)
        first=[value,(value*3)&255,0 if value%4==0 else value,255-value]
        second=[255-value,value//2,0 if value%5==0 else (value+83)&255,value]
        cached=[[166,1,0,33],[100,10,0,222]]
        scale=f32(5.5 if core%2 else 6);floor=1 if core<4 else 83
        seed_request(vm,core,scale,threads,policy,floor,first,second,missing,cached)
        vm.run(0x2f930,0x2fa44 if missing==3 else 0x2fa21)
        high=vm.getf(OUTPUT+0x3c*core+0x1f4);low=vm.getf(OUTPUT+0x3c*core+0x204)
        preference=vm.get(STATE+0x2c+core,1)
        if missing==3:
            assert high==low==17+core and preference==231;retained+=1
        else:
            effective=[cached[j] if missing&(1<<j) else values for j,values in enumerate((first,second))]
            expected=expected_request(*effective,threads,policy,floor,scale)
            assert (high,low,preference)==expected[:3],(core,value,threads,policy,missing,high,low,expected)
            for offset,want in zip((0x24,0x1c,0x14),expected[3:]):assert vm.get(STATE+core+offset,1)==want
            for j,values in enumerate(effective):assert vm.get(STATE+8*core+0x34+4*j)==pack(values)
            equal+=high==low;unbounded_low+=low>high
        trace.update(struct.pack('<5IffI',core,value,threads,policy,missing,high,low,preference));cases+=1
    return dict(cases=cases,both_zero_retains_old_cases=retained,equal_policy_value_cases=equal,
        lower_exceeds_upper_cases=unbounded_low,local_core_slots=8,input_codes_checked=list(range(256)),
        source_registers='0x1603038/0x160303c + 0x20000 * local core slot',
        source_identity='performance request candidate; MMIO-to-MSR alias not established',
        smt_merge='max of first three bytes; min of fourth byte',
        zero_word='use that thread cached word; both current words zero skip the update',
        scaling='binary32(selected code * binary32(1/166)) * Fmax scale',
        desired_nonzero='both outputs use clipped selected code, unless clipping produces zero',
        runtime_clamp_added=False,freshness_verified=False,physical_core_mapping_verified=False,
        trace_sha256=trace.hexdigest())


def pm_export(body,code):
    words=cases=0;trace=hashlib.sha256()
    for width,ordinal in ((1,0),(2,0),(2,1)):
        vm=Slice(body,code);H['seed_accumulation'](vm)
        expected=[[0.0]*8 for _ in range(3)]
        for sample in range(4):
            for core in range(8):
                first=[120+core,20+sample,0 if sample%2 else 83,100+sample]
                second=[166-core,10+sample,0,150-core]
                seed_request(vm,core,5.5,2,sample%2,1,first,second)
                vm.run(0x2f930,0x2fa21)
                values=[vm.getf(OUTPUT+core*0x3c+o) for o in (0x1f4,0x204)]
                values.append(float(vm.get(STATE+0x2c+core,1)))
                # Keep each core's code source after seed_request clears the
                # shared request state for the next independent core replay.
                for row,value in enumerate(values):expected[row][core]=f32(expected[row][core]+value)
            for core in range(8):vm.put(STATE+0x2c+core,100+sample,1)
            vm.run(0x2f370,0x2f536)
        actual=H['export_snapshot'](vm,width,ordinal)
        for row,series in zip((9,10),expected):
            for core,value in enumerate(series):
                want=f32(value*.25);assert actual[row*8+core]==want
                trace.update(struct.pack('<4If',width,ordinal,row,core,want));words+=1
        for core in range(8):assert actual[11*8+core]==101.5
        cases+=1
    return dict(cases=cases,samples=4,policy_words=words,core_rows=[9,10],additional_scaling=False,
        averaging=True,physical_ccd_or_os_mapping_verified=False,trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('smu','mp5','smu-study','mp5-study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();bodies=[];codes=[]
    for component,sha in (('smu',R['SHA']),('mp5',H['SHA'])):
        body=getattr(a,component).read_bytes();assert hashlib.sha256(body).hexdigest()==sha
        study=json.loads(getattr(a,component+'_study').read_text());assert study['source_sha256']==sha
        code={int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
        for address,i in code.items():
            raw=bytes.fromhex(i['bytes']);assert body[address:address+len(raw)]==raw
        bodies.append(body);codes.append(code)
    smu,mp5=bodies;sc,mc=codes
    report=dict(schema=1,hardware_execution=False,smu_sha256=R['SHA'],mp5_sha256=H['SHA'],
        scale=scale_sources(smu,sc,mp5,mc),requests=request_paths(mp5,mc),pm=pm_export(mp5,mc),
        runtime_write_enabled=False,measured_frequency=False,physical_pm_binding_verified=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='requests'}))
    print(json.dumps({k:v for k,v in report['requests'].items() if k!='input_codes_checked'}))


if __name__=='__main__':main()
