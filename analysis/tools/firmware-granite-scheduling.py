#!/usr/bin/env python3
"""Check update scheduling gates before assigning an integrator time unit.

Pinned SMU/MP5 instruction slices run with scripted queue returns, locks and
completion ports. No scheduler, interrupt controller or clock is emulated.
The tests distinguish enqueue/acknowledgement from completed model updates.
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
R=runpy.run_path(str(HERE/'firmware-granite-config-route.py'))
Slice=H['Slice']


def enqueue(smu,code):
    word=lambda a:struct.unpack_from('<I',smu,a)[0]
    control,stats=word(0xc10),word(0xbdc)
    assert word(0xc14)==0x1dcf4 and word(0x11568)==0x1ddd0
    cases=attempts=accepted=overruns=inhibited=0;trace=hashlib.sha256()
    for disabled,pending,status,missed in itertools.product((0,1,2,255),range(256),(0,1,2,0xffffffff),(0,0xffffffff)):
        vm=Slice(smu,code);vm.put(control+0x15,disabled,1);vm.put(control+0x14,pending,1);vm.put(stats+0x68,missed)
        calls=[]
        def queue(v):
            args=[v.r['a'+str(i)] for i in range(10,16)]
            assert args==[0x1dcf4,4,0,0,0,0]
            calls.append('queue');v.r['a10']=status
        vm.run(0x11e8,None,hooks={0x2810:queue,0x25f4:lambda v:calls.append('tail-service')})
        attempt=disabled==0 and pending<2;accept=attempt and status==0;overrun=disabled==0 and pending>=2
        assert calls==(['queue','tail-service'] if attempt else ['tail-service'])
        assert vm.get(control+0x14,1)==pending+int(accept)
        assert vm.get(stats+0x68)==(missed+int(overrun))&0xffffffff
        assert vm.get(control+0x15,1)==disabled
        trace.update(struct.pack('<7I',disabled,pending,status,missed,vm.get(control+0x14,1),vm.get(stats+0x68),int(attempt)))
        cases+=1;attempts+=attempt;accepted+=accept;overruns+=overrun;inhibited+=bool(disabled)
    return dict(cases=cases,queue_attempts=attempts,accepted=accepted,pending_gate_skips=overruns,
        inhibited_cases=inhibited,entry='0x11e8',pending_byte=hex(control+0x14),inhibit_byte=hex(control+0x15),
        skipped_counter=hex(stats+0x68),queued_dispatcher='0x1dcf4',trace_sha256=trace.hexdigest())


def broadcast(smu,code):
    cases=non_success=0;trace=hashlib.sha256()
    for mask,response,delay in itertools.product(range(4),(1,2,0xfd,0xff),(0,1,3)):
        child=R['Broadcast'](smu,code,mask,mask^3,0,response,delay);child.r['a2']=0xd
        locks=[]
        def lock(v):assert v.r['a10']==3;locks.append('lock')
        def unlock(v):assert v.r['a10']==3;locks.append('unlock')
        child.run(0x1d62c,None,hooks={0x3bb64:lock,0x3bc38:unlock})
        targets=[t for t in range(2) if mask&(1<<t)]
        expected=[]
        for target in targets:expected += [(child.ports[target],0),(0xf8010900+(target<<20),0xd)]
        assert child.writes==expected and locks==['lock','unlock']
        assert child.polls==[delay+1 if t in targets else 0 for t in range(2)]
        trace.update(json.dumps([mask,response,delay,child.writes,child.polls],sort_keys=True).encode())
        cases+=1;non_success+=bool(mask) and response!=1
    return dict(cases=cases,non_success_nonzero_completion_cases=non_success,entry='0x1d62c',
        command='0x0d',presence_not_active_mask=True,checks_success_code=False,
        trace_sha256=trace.hexdigest())


def ack_before_queue(mp5,code):
    word=lambda a:struct.unpack_from('<I',mp5,a)[0]
    assert word(0x13300)==0x2d7b8 and word(0x19634)==0x2d78c
    assert word(0x195f8)==0x2c798
    trace=hashlib.sha256();cases=0
    for status in (0,1,2,0xfd,0xff,0xffffffff):
        vm=Slice(mp5,code);calls=[];vm.put(0x19d98,0x51000);vm.put(0x51000,0)
        def acknowledge(v):
            # Execute the actual store helper, with an explicit result boundary.
            child=Slice(mp5,code);child.put(0x19d98,0x51000);child.put(0x51000,0)
            child.run(0x2e834,0x2e843);assert child.get(0x51000)==1
            v.put(0x51000,child.get(0x51000));calls.append('completion-1')
        def queue(v):
            assert v.get(0x51000)==1
            assert [v.r['a'+str(i)] for i in range(10,16)]==[0x2d78c,4,0,0,1,0]
            calls.append('queue');v.r['a10']=status
        vm.run(0x2d7b8,None,hooks={0x2e834:acknowledge,0x1e6c:queue})
        assert calls==['completion-1','queue'] and vm.get(0x51000)==1
        trace.update(struct.pack('<2I',status,vm.get(0x51000)));cases+=1
    return dict(cases=cases,entry='0x2d7b8',queued_dispatcher='0x2d78c',model_callback='0x2c798',
        completion_precedes_queue=True,queue_return_checked=False,model_update_completed=False,
        trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('smu','mp5','smu-study','mp5-study','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();smu=a.smu.read_bytes();mp5=a.mp5.read_bytes()
    sc=T['instructions'](a.smu_study,smu,T['SMU_SHA']);mc=T['instructions'](a.mp5_study,mp5,T['MP5_SHA'])
    result=dict(schema=1,hardware_execution=False,source_sha256=dict(smu=T['SMU_SHA'],mp5=T['MP5_SHA']),
        enqueue=enqueue(smu,sc),broadcast=broadcast(smu,sc),ack_before_queue=ack_before_queue(mp5,mc),
        queue_services_executed=False,interrupt_service_executed=False,absolute_update_period_verified=False,
        counter_event_identity_verified=False,integrator_msr_alias_verified=False,new_physical_units={},
        runtime_write_enabled=False,pm_slot_mapping_verified=False,independent_second_rail_verified=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))


if __name__=='__main__':main()
