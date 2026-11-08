#!/usr/bin/env python3
"""Byte-checked Granite Curve Shaper saved-offset write/read contract.

This proves saved SMU bytes, not successful MP5 application or an electrical
range. Baseline bytes, completion replies and concurrency are explicit inputs.
No hardware access is performed and no host atomic compare/write is inferred.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
C = runpy.run_path(str(HERE/'firmware-granite-curve-shaper.py'))
B = runpy.run_path(str(HERE/'firmware-granite-config-route.py'))
T, H, S = C['T'], C['H'], C['S']
Slice = H['Slice']
BASELINE, STORED = C['BASELINE'], C['STORED']
signed8 = lambda v: struct.unpack('<b',bytes([v & 255]))[0]
pack = lambda tier, triple: tier | sum((n & 255) << (8+8*j) for j,n in enumerate(triple))


def run_setter(body,code,prior,baseline,tier,triple,route=None):
    vm=Slice(body,code);vm.r.update(a1=0x50000,a2=0x1234)
    vm.put(0x817e,1,1);vm.put(0x8170,0,1)
    for i in range(15):
        vm.put(STORED+i,prior[i],1);vm.put(BASELINE+i,baseline[i],1)
    calls=[];replies=[];argument=pack(tier,triple)
    def input_word(v):assert v.r['a10']==0x1234;v.r['a10']=argument
    def permission(v):
        assert v.r['a10']==2;v.r['a10']=S['capability'](body,code,vm,4)
    def response(v):assert v.r['a10']==0x1234;replies.append(v.r['a11'])
    def rebuild(v):
        child=Slice(body,code);child.ram=v.ram;child.r['a1']=0x4ff00
        def broadcast(c):
            pair=(c.r['a10'],c.r['a11']);calls.append(pair)
            if route:route(pair,len(calls)-1)
        child.run(0x2ab74,None,hooks={0x1d760:broadcast})
        vm.trace.extend(child.trace)
    stalled=False
    try:
        vm.run(0x2abcc,None,hooks={0x15dc:input_word,0x2c910:permission,
                                  0x2ab74:rebuild,0x14ec:response})
    except ValueError as e:
        if str(e)!='Bounded instruction budget exceeded':raise
        stalled=True
    actual=[vm.get(STORED+i,1) for i in range(15)]
    assert [vm.get(BASELINE+i,1) for i in range(15)]==[n & 255 for n in baseline]
    # This is an offline observation of synthetic RAM even for stalled cases.
    # It does not assert that a busy real mailbox accepts another command.
    readback=C['getter'](body,code,vm.ram)[:5]
    assert readback==[pack(i,actual[3*i:3*i+3]) for i in range(5)]
    return dict(argument=argument,stored=actual,raw=readback,calls=calls,
                replies=replies,stalled=stalled)


def cell_domains(body,code):
    """All raw baseline/runtime pairs through the actual scalar clamp/add slice."""
    trace=hashlib.sha256();cases=legal=wraps=invalid_changes=0
    for baseline,raw in itertools.product(range(256),range(256)):
        vm=Slice(body,code)
        vm.r.update(a11=baseline,a12=raw,a5=(-50)&0xffffffff,a4=30,
                    a9=STORED-0x86,a10=0x50000)
        vm.run(0x2ab9a,0x2abb1)
        count=signed8(raw);normalized=max(-50,min(30,count))
        assert vm.get(STORED,1)==normalized & 255
        assert vm.get(0x50000,1)==(signed8(baseline)+normalized)&255
        in_range=-50<=count<=30
        assert (vm.get(STORED,1)==raw)==in_range
        legal+=in_range;invalid_changes+=not in_range
        wraps+=in_range and not -128<=signed8(baseline)+count<=127
        trace.update(bytes([baseline,raw,vm.get(STORED,1),vm.get(0x50000,1)]));cases+=1
    return dict(cases=cases,legal_pair_cases=legal,legal_offset_baseline_wraps=wraps,
                out_of_range_pair_changes=invalid_changes,legal_saved_values=81,
                rejected_saved_values=175,clamp_identity_exactly_on=[-50,30],
                raw_signed8_oracle='struct.unpack <b',trace_sha256=trace.hexdigest())


def in_range_tables(body,code):
    trace=hashlib.sha256();cases=unchanged=0
    priors=((0,)*15,tuple((-50,30,-1,0,1)[i%5] for i in range(15)))
    baselines=((0,)*15,(127,)*15,(128,)*15)
    for tier,column,count,prior,baseline in itertools.product(range(5),range(3),range(-50,31),priors,baselines):
        triple=[-50,0,30];triple[column]=count
        result=run_setter(body,code,prior,baseline,tier,triple)
        expected=[v&255 for v in prior];expected[3*tier:3*tier+3]=[v&255 for v in triple]
        assert result['stored']==expected and result['replies']==[1] and not result['stalled']
        assert result['argument']&255==tier
        assert result['raw']==[pack(i,expected[3*i:3*i+3]) for i in range(5)]
        assert all(expected[i]==(prior[i]&255) for i in range(15) if i//3!=tier)
        forwarded=[pack(i,[(signed8(baseline[3*i+j])+signed8(expected[3*i+j]))&255
                           for j in range(3)]) for i in range(5)]
        assert result['calls']==[(0x47,v) for v in forwarded]+[(0x47,0x80)]
        trace.update(struct.pack('<6I',result['argument'],*result['raw']))
        unchanged+=12;cases+=1
    return dict(cases=cases,unselected_cells_checked=unchanged,
                each_legal_integer_each_cell=True,complete_saved_readback_matches=True,
                hidden_baseline_required_for_saved_value_proof=False,
                baseline_required_for_effective_payload_prediction=True,
                trace_sha256=trace.hexdigest())


def unsafe_prior_examples(body,code):
    trace=hashlib.sha256();cases=changed_unselected=0
    # A hypothetical competing writer changes one prior cell after a caller's
    # valid all-zero snapshot. The firmware does not compare the prior table.
    for tier,position,value in itertools.product((0,4),range(15),(-51,31)):
        prior=[0]*15;prior[position]=value
        r=run_setter(body,code,prior,[0]*15,tier,(-1,0,1))
        expected=[max(-50,min(30,n)) for n in prior]
        expected[3*tier:3*tier+3]=[-1,0,1]
        assert r['stored']==[n&255 for n in expected] and r['replies']==[1]
        if position//3!=tier:
            assert expected[position]!=value;changed_unselected+=1
        trace.update(struct.pack('<4I',tier,position,value&0xffffffff,*r['replies']));cases+=1
    return dict(cases=cases,changed_unselected_cases=changed_unselected,
                competing_change_is_fixture=True,firmware_atomic_compare_write=False,
                post_readback_must_compare_entire_table=True,trace_sha256=trace.hexdigest())


def routed_completions(body,code):
    trace=hashlib.sha256();cases=failed_ack=empty=0
    for tier,mask,response,delay in itertools.product(range(5),range(4),(1,2,0xfd,0xff),(0,2)):
        events=[]
        def route(pair,index):
            command,payload=pair;assert command==0x47
            child=B['Broadcast'](body,code,mask^3,mask,payload,response,delay)
            child.r['a2']=command;locks=[]
            child.run(0x1d760,None,hooks={0x3bb64:lambda v:locks.append(v.r['a10']),
                                         0x3bc38:lambda v:locks.append(v.r['a10'])})
            expected=[]
            for target in range(2):
                if mask&(1<<target):
                    port=0xf8010600+(target<<20)
                    expected.extend([(child.ports[target],0),(port+0x398,payload),(port+0x300,command)])
            assert child.writes==expected and locks==[3,3]
            assert child.polls==[delay+1 if mask&(1<<i) else 0 for i in range(2)]
            events.append((index,len(expected)))
        r=run_setter(body,code,[0]*15,[127]*15,tier,(-50,0,30),route)
        expected=[0]*15;expected[3*tier:3*tier+3]=[-50,0,30]
        assert r['raw']==[pack(i,expected[3*i:3*i+3]) for i in range(5)]
        assert r['replies']==[1] and len(events)==6
        failed_ack+=bool(mask) and response!=1;empty+=not mask;cases+=1
        trace.update(struct.pack('<4I',tier,mask,response,delay))
    stalled=0
    for packet,target in itertools.product(range(6),range(2)):
        def route(pair,index):
            child=B['Broadcast'](body,code,0,3,pair[1],1,stall=target if index==packet else None)
            child.r['a2']=pair[0]
            child.run(0x1d760,None,budget=256,hooks={0x3bb64:lambda v:None,0x3bc38:lambda v:None})
        r=run_setter(body,code,[0]*15,[0]*15,4,(-50,0,30),route)
        assert r['stalled'] and not r['replies'] and len(r['calls'])==packet+1
        # Saved bytes precede all broadcast replies, including the first one.
        assert [signed8(n) for n in r['stored'][12:]]==[-50,0,30]
        assert all(n==0 for n in r['stored'][:12]);stalled+=1
    return dict(cases=cases,failed_nonzero_completions_still_acknowledged=failed_ack,
                no_target_cases_still_saved_and_acknowledged=empty,stalled_cases=stalled,
                broadcasts_per_write=6,mask_address='0x6816',
                timeout_does_not_imply_saved_bytes_unchanged=True,
                stalled_getter_is_offline_ram_observation_only=True,
                device_effects_verified=False,trace_sha256=trace.hexdigest())


def identical_value_writes(body,code):
    cases=0
    prior=[-50,0,30]*5
    for tier,baseline in itertools.product(range(5),(0,127,128)):
        r=run_setter(body,code,prior,[baseline]*15,tier,prior[3*tier:3*tier+3])
        assert r['stored']==[n&255 for n in prior] and len(r['calls'])==6 and r['replies']==[1]
        cases+=1
    return dict(cases=cases,firmware_elides_identical_write=False,
                application_should_skip_message_for_identical_saved_values=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('smu','study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();body=a.smu.read_bytes();code=T['instructions'](a.study,body,T['SMU_SHA'])
    assert struct.unpack_from('<II',body,0x6fa4+8*0xa6)==(0x2abcc,6)
    assert struct.unpack_from('<II',body,0x6fa4+8*0x84)==(0x2ac14,6)
    report=dict(schema=1,source_sha256=T['SMU_SHA'],hardware_execution=False,
        cell_domains=cell_domains(body,code),in_range_tables=in_range_tables(body,code),
        unsafe_prior_examples=unsafe_prior_examples(body,code),routed_completions=routed_completions(body,code),
        identical_value_writes=identical_value_writes(body,code),
        saved_value_contract_verified=True,firmware_atomic_compare_write=False,
        physical_device_application_verified=False,electrical_limits_verified=False,
        hidden_baseline_recovered=False,precise_frequency_temperature_thresholds_verified=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__=='__main__':main()
