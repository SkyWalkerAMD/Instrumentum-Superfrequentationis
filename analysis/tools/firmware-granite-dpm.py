#!/usr/bin/env python3
"""Byte-checked four-state common PM policy table, with synthetic inputs only.

Reference frequencies use the firmware's nominal 100 MHz arithmetic. Fuses,
reciprocal results and configuration inputs are fixtures; these are policy
values, not measured clocks, physical rail identity or device readback.
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
T=runpy.run_path(str(HERE/'firmware-granite-topology.py'))
Slice,f32=H['Slice'],H['f32']
BASE,TABLE,ACC,OUT=0x96a0,0x9844,0x15320,0x15b40


def divider(body,code,value):
    vm=Slice(body,code);vm.r['a2']=value
    vm.run(0x1cb1c,None,hooks={})
    expected=value/4 if value<=64 else (2*value-64)/4 if value<=96 else (4*value-256)/4 if value<=126 else 128.0
    assert vm.f['f0']==expected
    return expected


def clocks(body,code,vm,state,first,second,da,db,mode,recompute=False):
    p=TABLE+state*0x50;vm.r.update(a1=0x50000,a2=state)
    for off,val,size in ((0xb0,first>>16,1),(0xb1,second>>16,1),(0xb4,first&65535,2),
                         (0xb6,second&65535,2),(0xb2,da,1),(0xb3,db,1),(0xb8,mode,1)):
        vm.put(p+off,val,size)
    vm.putf(0x1115c,-999);vm.putf(p+0x98,-999 if recompute else 1.125)
    divisions=[]
    def helper(v):v.f['f0']=divider(body,code,v.r['a10'])
    def reciprocal(v):
        assert v.f['f0']>0
        divisions.append(v.f['f0']);v.f['f0']=f32(1/v.f['f0'])
    vm.run(0x25090,0x25130,hooks={0x1cb1c:helper,0x3f870:reciprocal})
    a=f32(f32(f32(float(first))*100)*f32(1/65536));a=f32(a*f32(1/divider(body,code,da)))
    b=f32(f32(f32(float(second))*100)*f32(1/65536));b=f32(b*f32(1/divider(body,code,db)))
    c=float(((round(b)&65535)*(1 if mode else 2))&65535)
    assert [vm.getf(p+off) for off in (0x84,0x88,0x8c)]==[a,b,c]
    assert len(divisions)==2 and vm.get(p+0x90,2)==round(a)&65535
    return [a,b,c]


def clock_grid(body,code):
    trace=hashlib.sha256();cases=0
    for value in range(256):divider(body,code,value)
    for state,packed,div,mode in itertools.product(range(4),(0,1,65535,65536,0x123456,0xffffff),
                                                  (1,2,4,63,64,65,95,96,97,126,127,255),(0,1,2,255)):
        vm=Slice(body,code);values=clocks(body,code,vm,state,packed,packed^0xffffff,div,div,mode)
        vm.run(0x25130,0x25194,hooks={})
        assert vm.getf(TABLE+state*80+0x98)==1.125
        trace.update(struct.pack('<4I3f',state,packed,div,mode,*values));cases+=1
    return dict(cases=cases,divider_codes=256,clock_fields_per_state=3,nominal_reference_mhz=100,
        frequency_is_measured=False,zero_divider_not_used_for_reciprocal=True,
        third_clock_uses_rounded_low16_and_low16_product=True,reciprocal_results_are_fixtures=True,
        trace_sha256=trace.hexdigest())


def fuse_tables(body,code,seed):
    u=lambda a:struct.unpack_from('<I',body,a)[0]
    vm=Slice(body,code);vm.r.update(a1=0x50000);vm.ram.update(enumerate(body))
    expected={};calls=[]
    for table,(freq,volt) in enumerate(((0x11c10,0x11c14),(0x11c24,0x11c28))):
        assert u(volt)-u(freq)==36
        for i in range(6):
            expected[(u(freq)+6*i,6)]=i*10+1
            expected[(u(volt)+9*i,9)]=(seed+17*i+31*table)&511
    def fuse(v):
        key=(v.r['a10'],v.r['a11']);assert key in expected
        calls.append(key);v.r['a10']=expected[key]
    vm.run(0x2531c,0x253ae,hooks={0x1e22c:fuse})
    assert len(calls)==24 and set(calls)==set(expected)
    for table,(freq,volt) in enumerate(((0x11c10,0x11c14),(0x11c24,0x11c28))):
        fp=(0x97d4,0x981c)[table];vp=fp+24
        for i in range(6):
            assert vm.getf(fp+4*i)==100*expected[(u(freq)+6*i,6)]
            assert vm.getf(vp+4*i)==f32(f32(.245)+expected[(u(volt)+9*i,9)]*f32(.005))
    return vm


def interpolation(body,code,vm,x,table):
    child=Slice(body,code);child.ram=vm.ram.copy();child.r.update(a1=0x52000,a2=6,a3=table,a4=table+24)
    child.f['f0']=x
    def reciprocal(v):assert v.f['f0']!=0;v.f['f0']=f32(1/v.f['f0'])
    child.run(0x24db0,None,hooks={0x3f870:reciprocal})
    # Independent piecewise interpolation with the firmware's endpoint minimum.
    xs=[vm.getf(table+4*i) for i in range(6)];ys=[vm.getf(table+24+4*i) for i in range(6)]
    result=ys[0]
    for i in range(4,-1,-1):
        if xs[i]<x:
            delta=f32(xs[i+1]-xs[i])
            if delta:result=f32(ys[i]+f32(f32(ys[i+1]-ys[i])*f32(x-xs[i]))*f32(1/delta))
            break
    expected=min(result,ys[-1])
    assert child.f['f0']==expected,(x,child.f['f0'],expected)
    return child.f['f0']


def voltage_grid(body,code):
    trace=hashlib.sha256();cases=0
    for seed in range(0,512,17):
        vm=fuse_tables(body,code,seed)
        for table,x in itertools.product((0x97d4,0x981c),(-1,0,100,101,599,600,1100,1600,5100,6000)):
            value=interpolation(body,code,vm,float(x),table)
            trace.update(struct.pack('<IIf',seed,table,value));cases+=1
    retained=0;overrides=0
    for state,value,mode in itertools.product(range(4),(0,1,2,255,256,511,512,65535),(0,1,2,255)):
        vm=Slice(body,code);vm.r['a2']=state;vm.put(BASE+0x7e,value,2)
        vm.put(BASE+0x80,0,2);vm.put(BASE+0x82,0,2);vm.put(0xc5ce,mode,1)
        for slot in range(4):vm.putf(BASE+slot*80+0x23c,-123-slot)
        vm.run(0x24fbc,None,hooks={})
        for slot in range(4):
            expected=f32(f32(.245)+value*f32(.005)) if slot==state and value else -123-slot
            assert vm.getf(BASE+slot*80+0x23c)==expected
        retained+=value==0;overrides+=1
    return dict(fuse_table_cases=31,fuse_returns=31*24,interpolation_cases=cases,override_cases=overrides,
        zero_code_retentions=retained,unit='V',physical_rail_identity_verified=False,
        input_code_range_is_not_electrical_range=True,trace_sha256=trace.hexdigest())


def exported(body,code):
    trace=hashlib.sha256();cases=0
    # Sampling count, not elapsed-time residency. Invalid states are excluded
    # from the four numerators but still counted in the denominator.
    for states in ((0,),(3,),(4,),(255,),(0,1),(0,1,2,3),(0,0,1,255)):
        vm=fuse_tables(body,code,17);values=[]
        for address in range(ACC,ACC+0x340):vm.put(address,0,1)
        for state in range(4):
            values.append(clocks(body,code,vm,state,(20+state)*65536,(30+state)*65536,4,4,state&1,True))
            replies=[]
            def interpolate(v):
                assert v.r['a10']==6 and v.r['a12']==v.r['a11']+24
                v.f['f0']=interpolation(body,code,v,v.f['f0'],v.r['a11']);replies.append(v.f['f0'])
            vm.run(0x25130,0x25169,hooks={0x24db0:interpolate})
            assert len(replies)==2
            v=vm.getf(BASE+state*80+0x23c);assert v==max(replies);values[-1].append(v)
            vm.put(ACC+0x2a0+4*state,0)
        vm.r['a11']=ACC
        for state in states:
            vm.r['a9']=state;vm.run(0x2cd41,0x2cd4f)
        # The accumulator's common sample counter is incremented once per call.
        vm.put(ACC+0x33c,0)
        for _ in states:
            vm.r['a11']=ACC;vm.run(0x2ce0b,0x2ce13)
        assert vm.get(ACC+0x33c)==len(states)
        vm.r.update(a1=0x50000,a5=ACC)
        def reciprocal(v):
            assert v.f['f0']==len(states);v.f['f0']=f32(1/v.f['f0'])
        vm.run(0x2ce80,0x2cfac,hooks={0x3f870:reciprocal})
        assert vm.getf(0x50028)==f32(f32(1/len(states))*100)
        vm.run(0x2d00c,0x2d061)
        before=[]
        for state in range(4):
            share=f32(states.count(state)*f32(f32(1/len(states))*100))
            assert vm.getf(OUT+4*(67+state))==share
            for kind in range(4):assert vm.getf(OUT+4*(71+4*kind+state))==values[state][kind]
        # The generic pointer copy must not replace these direct export stores.
        for index in range(67,87):
            assert struct.unpack_from('<I',body,0xb340+4*index)[0]==0
            before.append(vm.get(OUT+4*index))
        for index in range(301):
            ptr=struct.unpack_from('<I',body,0xb340+4*index)[0]
            if ptr:vm.putf(ptr,0)
        vm.r.update(a12=OUT,a13=301);vm.run(0x2d13a,0x2d14e)
        assert [vm.get(OUT+4*i) for i in range(67,87)]==before
        trace.update(struct.pack('<20I',*before));cases+=1
    return dict(cases=cases,words=cases*20,state_slots=4,share_indices=list(range(67,71)),
        clock_indices=list(range(71,83)),voltage_indices=list(range(83,87)),
        sample_share_not_time_residency=True,policy_values_not_averaged=True,
        enabled_states_not_identified=True,physical_or_pm_core_mapping_verified=False,
        trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('smu','study','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();body=a.smu.read_bytes();code=T['instructions'](a.study,body,T['SMU_SHA'])
    result=dict(schema=1,hardware_execution=False,source_sha256=T['SMU_SHA'],
        clock_policy=clock_grid(body,code),voltage_policy=voltage_grid(body,code),export=exported(body,code),
        physical_clock_identity_verified=False,physical_rail_identity_verified=False,
        runtime_write_enabled=False,independent_second_rail_verified=False,new_core_or_ccd_units={})
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()
