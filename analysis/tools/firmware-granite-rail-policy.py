"""Byte-checked Granite automatic channel 1/2 policy versus override.

Original-byte local policy and direct-writer slices; packet submission, locks,
physical effects and physical rail identity remain explicit boundaries.
"""
from pathlib import Path
import argparse,hashlib,itertools,json,runpy,struct
HERE=Path(__file__).parent
H=runpy.run_path(str(HERE/'firmware-granite-semantics.py'))
T=runpy.run_path(str(HERE/'firmware-granite-topology.py'))
Slice=H['Slice'];BODY=None;CODE=None
CURRENT={1:0x8c84,2:0x8c9c};PENDING={1:0x8c86,2:0x8c9e}
OVERRIDE={1:0x8c88,2:0x8ca0};MIRROR={1:0x8c8c,2:0x8ca4}


def child(parent,entry,args,hooks=None):
    c=Slice(BODY,CODE);c.ram=parent.ram;c.r['a1']=parent.r['a1']-0x100
    for i,n in enumerate(args,2):c.r['a'+str(i)]=n
    c.run(entry,None,hooks={} if hooks is None else hooks)
    return c


def initialized(channel,mode,bypass,current,override,baseq,peakq,slot):
    v=Slice(BODY,CODE);v.r['a1']=0x50000
    v.put(0xc5ce,mode,1);v.put(0xae11,bypass,1)
    v.put(CURRENT[channel],current,2);v.put(OVERRIDE[channel],override,2)
    v.putf(MIRROR[channel],-9)
    first,count=(0x8cc4,14) if channel==1 else (0x8cfc,4)
    v.putf(0x8c90 if channel==1 else 0x8ca8,baseq/4)
    for i in range(count):v.putf(first+4*i,peakq/4 if i==slot else -1)
    if channel==1:
        for i in range(14):v.putf(0x8d5c+4*i,i/8)
    return v


def apply_phase(v,channel,entry,packets,events):
    def lock(c):assert c.r['a10']==0x2000;events.append('lock')
    def unlock(c):assert c.r['a10']==0x2000;events.append('unlock')
    def direct(c):
        def packet(w):packets.append((w.r['a10'],w.r['a11']))
        child(c,0x20f10 if channel==1 else 0x20f3c,[c.r['a10']],{0x32d6c:packet})
    v.run(entry,None,hooks={0x3bb64:lock,0x3bc38:unlock,
                            (0x20f10 if channel==1 else 0x20f3c):direct})


def policy():
    count=raised=lowered=blocked=capped=0;trace=hashlib.sha256()
    for channel in (1,2):
        for mode,bypass,current,override,baseq,peakq,slot in itertools.product(
                (0,) if channel==1 else (0,1), (0,1),(0,512),(0,1),(0,2,6),(0,1,3,6),
                range(14 if channel==1 else 4)):
            v=initialized(channel,mode,bypass,current,override,baseq,peakq,slot)
            # Quarter-unit fixtures: independent integer oracle avoids relying
            # on the interpreter's binary32 clamp/ceil implementation for values.
            scale=25 if channel==2 and mode==1 else 50
            raw=max(0,scale*(baseq+peakq)-49)
            cap=211 if channel==1 else 91 if mode==1 else 231
            expected=raw if bypass else min(raw,cap)
            packets=[];events=[]
            apply_phase(v,channel,0x20c10 if channel==1 else 0x20cb0,packets,events)
            assert v.get(PENDING[channel],2)==expected
            should_raise=not override and expected>current
            assert packets==([(channel,expected)] if should_raise else [])
            assert v.get(CURRENT[channel],2)==(expected if should_raise else current)
            if channel==1:assert v.getf(0x8d94)==13/8
            apply_phase(v,channel,0x20c90 if channel==1 else 0x20d4c,packets,events)
            should_lower=not override and expected<current
            assert packets==([(channel,expected)] if should_raise or should_lower else [])
            assert v.get(CURRENT[channel],2)==(current if override else expected)
            assert events==['lock','unlock']*2
            assert v.get(OVERRIDE[channel],2)==override
            if override:assert v.getf(MIRROR[channel])==-9
            trace.update(struct.pack('<9I',channel,mode,bypass,current,override,baseq,peakq,slot,expected))
            count+=1;raised+=should_raise;lowered+=should_lower
            blocked+=bool(override);capped+=raw>expected
    return dict(cases=count,raised=raised,lowered=lowered,override_blocked=blocked,capped=capped,
                independent_integer_oracle_on_quarter_unit_inputs=True,
                channel_1_request_slots=14,channel_2_request_slots=4,
                physical_rail_identity_verified=False,trace_sha256=trace.hexdigest())


def overrides_exceed_automatic_caps():
    examples=[]
    for channel,mode,value in ((1,0,212),(1,0,511),(2,0,232),(2,0,511),(2,1,92),(2,1,511)):
        v=initialized(channel,mode,0,0,0,0,0,0);packets=[]
        def direct(c):
            child(c,0x20f10 if channel==1 else 0x20f3c,[c.r['a10']],
                  {0x32d6c:lambda w:packets.append([w.r['a10'],w.r['a11']])})
        v.r['a2']=value
        v.run(0x20de4 if channel==1 else 0x20e18,None,
              hooks={0x3bb64:lambda c:None,0x3bc38:lambda c:None,
                     (0x20f10 if channel==1 else 0x20f3c):direct})
        assert packets==[[channel,value]] and v.get(CURRENT[channel],2)==value
        assert v.get(OVERRIDE[channel],2)==1 and v.get(0xae11,1)==0
        examples.append(dict(channel=channel,mode=mode,input_code=value,sent_code=packets[0][1]))
    return dict(cases=len(examples),examples=examples,
                automatic_cap_not_enforced_by_override=True,packets_are_recorded_service_calls=True,
                electrical_safe_range_established=False)


def release_and_resume():
    cases=0
    for channel,mode in ((1,0),(2,0),(2,1)):
        v=initialized(channel,mode,0,512,1,2,0,0);packets=[];events=[]
        apply_phase(v,channel,0x20c10 if channel==1 else 0x20cb0,packets,events)
        pending=v.get(PENDING[channel],2)
        assert not packets and v.get(CURRENT[channel],2)==512
        child(v,0x20e08 if channel==1 else 0x20e3c,[])
        assert not packets and v.get(CURRENT[channel],2)==512 and v.get(OVERRIDE[channel],2)==0
        apply_phase(v,channel,0x20c90 if channel==1 else 0x20d4c,packets,events)
        assert packets==[(channel,pending)] and v.get(CURRENT[channel],2)==pending
        cases+=1
    return dict(cases=cases,release_does_not_restore_immediately=True,
                later_policy_phase_can_apply_pending_target=True,actual_scheduling_not_replayed=True)


def main():
    global BODY,CODE
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('smu','study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();BODY=a.smu.read_bytes();CODE=T['instructions'](a.study,BODY,T['SMU_SHA'])
    report=dict(schema=1,source_sha256=T['SMU_SHA'],hardware_execution=False,
                runtime_writer_enabled=False,automatic_policy=policy(),
                override_examples=overrides_exceed_automatic_caps(),release_resume=release_and_resume(),
                independent_second_rail_protocol_verified=False,device_setting_readback_verified=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report))


if __name__=='__main__':main()
