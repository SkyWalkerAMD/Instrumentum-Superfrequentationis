#!/usr/bin/env python3
"""Byte-checked Granite clock models and CCD counter units, synthetic RAM only.

The frequency anchor is the MHz command convention and the existing checked
Fmax conversion. Fuse, transport and reciprocal calls are explicit fixtures.
These experiments do not measure clocks or validate physical slot identities.
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
P=runpy.run_path(str(HERE/'firmware-granite-performance.py'))
R=runpy.run_path(str(HERE/'firmware-granite-registers.py'))
Slice,f32,CORE=H['Slice'],H['f32'],H['CORE']


def ccd_percentage(body,code):
    cases=retained=wrapped=0; trace=hashlib.sha256()
    values=(0,1,2,127,128,129,4096,65535,131072,262143)
    for numerator,denominator,old in itertools.product(values,values,(0,7,262140)):
        vm=Slice(body,code); vm.r.update(a5=0x12518,a6=0x7ab8)
        current=(old+denominator)&0x3ffff
        old_num=(old+13)&0x3ffff; new_num=(old_num+numerator)&0x3ffff
        vm.put(0x12518+0x374,old);vm.put(0x12518+0x378,old_num)
        vm.put(0x12518+0x3dc,current|((new_num&0x3fff)<<18))
        vm.put(0x12518+0x3e0,(new_num>>14)|0xfffffff0)
        vm.putf(0x12894,17.25)
        for off,value in ((0x1f8,2),(0x1fc,3),(0x200,5)):vm.putf(0x7ab8+off,value)
        vm.f.update(f0=7,f2=11)
        vm.run(0x2cc06,0x2cc5f if denominator else 0x2cc61)
        expected=f32(((numerator*12800)//denominator)/128) if denominator else 17.25
        assert vm.getf(0x12894)==expected
        assert vm.get(0x12518+0x374)==current and vm.get(0x12518+0x378)==new_num
        assert vm.getf(0x12518+0x348)==52
        cases+=1;retained+=int(not denominator)
        wrapped+=int(current<old or new_num<old_num)
        trace.update(struct.pack('<3If',numerator,denominator,old,expected))
    gates=0
    for enabled,previous,current in itertools.product((0,1),(7,0xffffffff),(7,0)):
        vm=Slice(body,code);vm.r.update(a2=0x7820,a7=0x116ac)
        vm.put(0x7820,enabled,1);vm.put(0x116ac+0x8f,1,1)
        vm.put(0x12518+0x384,previous);vm.put(0x12518+0x380,current)
        vm.putf(0x12894,17.25)
        vm.run(0x2caf1,0x2cb0e if enabled and current!=previous else 0x2cc61)
        assert vm.getf(0x12894)==17.25
        assert vm.get(0x12518+0x384)==(current if enabled else previous);gates+=1
    return dict(cases=cases,gating_cases=gates,wrapped_counter_cases=wrapped,zero_denominator_retained_cases=retained,
        ccd_row=9,source='0x12894',unit='%',counter_width_bits=18,
        formula='floor(delta_numerator * 12800 / delta_denominator) / 128',
        clamp_to_100=False,counter_event_meanings_verified=False,
        peripheral_counter_acquisition_executed=False,trace_sha256=trace.hexdigest())


def policy_selection(body,code):
    cases=0;trace=hashlib.sha256()
    for upper,lower in ((6,1.2),(5.5,.6),(1.2,.6),(6,7)):
        for values in ((0,.3,.6,1.2,2.4,3.6,4.8,6),(7,6,5,4,3,2,1,0)):
            vm=Slice(body,code);vm.r['a1']=0x50000
            vm.putf(0x119a0,upper);vm.putf(0x119a4,lower)
            vm.run(0x295f8,0x29655)
            assert all(vm.getf(0x11440+4*i)==f32(upper) for i in range(4))
            for core,value in enumerate(values):vm.putf(0x11440+0x3c*core+0x38,value)
            vm.run(0x29468,0x294e2)
            for core,value in enumerate(values):
                expected=min(max(f32(value),f32(lower)),f32(upper))
                assert vm.getf(0x11480+0x3c*core)==expected
                trace.update(struct.pack('<I3f',core,upper,lower,expected));cases+=1
    return dict(cases=cases,initializer='0x295f8',selector='0x29468',
        input_scale='0x119a0',selected_policy='0x11480 + 0x3c * local_slot',unit='GHz',
        all_policy_overrides_replayed=False,trace_sha256=trace.hexdigest())


def expected_model(values,mask,feature,local_flags):
    codes=[max(int(f32(f32(v)*200+f32(.0002))),120) for v in values]
    maximum=max([codes[i] for i in range(8) if mask&(1<<i)],default=0)
    pll_div=1 if maximum>239 else 2
    peak=max(maximum,240)
    common=f32(f32(((peak*1000000)&0xffffffff)//pll_div)*f32(5e-9))
    bound=1 if peak<=479 else 2 if peak<=719 else 3 if peak<=959 else 4
    result=[]
    for core,code in enumerate(codes):
        if not mask&(1<<core):result.append(None);continue
        if pll_div==2:code=120
        if feature and local_flags[core] and pll_div==1:
            div=min(bound,peak//code)
            frac=max(0,min(63,div*64+64-((code*(div+1)*div*64)//peak))) if div<4 else 0
        else:div=min(bound,(peak+code-1)//code);frac=0
        first=f32(1/div);second=f32(1/(div+1))
        weighted=f32(f32((64-frac)*first)+frac*second)
        modeled=f32(weighted*f32(common/64))
        result.append((modeled,f32(common*first),div,frac))
    return common,pll_div,result


def clock_models(body,code):
    assert struct.unpack_from('<f',body,0x18b7c)[0]==1/64
    cases=active=disabled=0;divisors=set();fractions=set();trace=hashlib.sha256()
    vectors=[(v,)*8 for v in (0,.595,.6,1.195,1.2,2.395,2.4,3.595,3.6,4.795,4.8,6,12)]
    vectors += [(6,5.375,4.7,3.7,2.8,2.1,1.4,.6),(12,11,9,7,5,3,2,1),
                (6,3.005,2.005,1.505,3,2,1.5,.6)]
    masks=(0,1,2,4,8,16,32,64,128,0x55,0xaa,255)
    for values,mask,feature in itertools.product(vectors,masks,(0,1)):
        flags=[1 if core%3 else 0 for core in range(8)]
        vm=Slice(body,code);vm.r['a1']=0x50000
        vm.put(0x7820+0x20,mask);vm.put(0x1192c+0x96,feature,1)
        vm.put(0x31ffe00+0x3c0,0) # staged read only; no actual MMIO
        vm.putf(0x18b7c,1/64)
        for core,value in enumerate(values):
            vm.putf(0x11480+0x3c*core,value)
            vm.put(0x11410+0x3c*core+0x8f,flags[core],1)
            vm.putf(CORE+0x1bc*core+0x68,17.25+core)
            vm.putf(CORE+0x1bc*core+0x6c,33.25+core)
        vm.run(0x2a244,0x2a256)
        vm.r['a10']=0x60000 # unused pointer returned by helper 26820
        vm.run(0x2a259,0x2a355)
        common,pll_div,expected=expected_model(values,mask,feature,flags)
        assert vm.getf(0x12878)==common and vm.get(0x116ac+0x78,1)==pll_div
        vm.run(0x2a355,0x2a35a)
        for core,row in enumerate(expected):
            assert vm.r['a2']==core
            if row is not None:
                vm.run(0x2a35a,0x2a3e5)
                div=int(vm.f['f0']);assert div==row[2]
                vm.f['f0']=f32(1/div)
                vm.run(0x2a3e8,0x2a3f0)
                assert vm.f['f0']==div+1;vm.f['f0']=f32(1/(div+1))
                vm.run(0x2a3f3,0x2a42b)
                assert vm.getf(CORE+0x1bc*core+0x68)==row[0]
                assert vm.getf(CORE+0x1bc*core+0x6c)==row[1]
                assert vm.get(0x116b8+0x18*core+0x7f,1)==row[3]
                active+=1;divisors.add(div);fractions.add(row[3])
            else:
                vm.run(0x2a35a,0x2a42b)
                assert vm.getf(CORE+0x1bc*core+0x68)==17.25+core
                assert vm.getf(CORE+0x1bc*core+0x6c)==33.25+core;disabled+=1
            vm.run(0x2a42b,0x2a35a if core<7 else 0x2a43c)
        # Caller copies the model into the CCD accumulator's source.
        vm.run(0x2a20a,0x2a215)
        assert vm.getf(0x1287c)==common
        for core in range(8):trace.update(struct.pack('<If',core,vm.getf(CORE+0x1bc*core+0x68)))
        trace.update(struct.pack('<If',mask,common));cases+=1
    assert divisors=={1,2,3,4} and {0,63}.issubset(fractions)
    return dict(cases=cases,active_core_cases=active,disabled_retained_cases=disabled,
        divisors=sorted(divisors),fraction_codes=sorted(fractions),
        core_row=3,ccd_row=5,unit='GHz',core_source='0x11a80 + 0x1bc * local_slot',
        ccd_source='0x1287c',measurement=False,reciprocal_is_exact_fixture=True,
        reciprocal_numeric_accuracy_verified=False,peripheral_programming_executed=False,
        input_scope='finite nonnegative synthetic policy values 0..12 GHz; not a supported operating range',
        trace_sha256=trace.hexdigest())


def fused_policy(smu,sc,mp5,mc):
    cases=0;trace=hashlib.sha256()
    initial=0
    for mhz,fuse in itertools.product((0,600,1200,4500,5700,6000,6375,65535),(48,200,228,240)):
        vm=Slice(smu,sc);vm.r.update(a1=0x50000,a3=0x134c4,a10=fuse)
        vm.put(0x134c4+0x8a,mhz,2);vm.put(0x8164+0x33,0,1)
        vm.run(0x29bb0,0x29c98)
        baseline=f32(fuse*f32(.025))
        selected=max(f32(baseline-1),min(f32(baseline+f32(.2)),f32(f32(mhz)*f32(.001)))) if mhz else 0
        offset=f32(selected-baseline) if selected else 0
        assert vm.getf(0x50020)==offset
        assert vm.getf(0x5001c)==(selected if selected else baseline)
        vm.run(0x29da5,0x29db1)
        assert vm.getf(0x50014)==offset;initial+=1
    for mhz,smu_fuse,mp5_fuse in itertools.product((0,600,1200,4500,5700,6000,6375,65535),(48,200,228,240),(0,120,1140,1200,2047)):
        vm=Slice(smu,sc);vm.r['a3']=mhz
        vm.run(0x29e66,0x29e86)
        vm.r['a3']=smu_fuse;vm.f.update(f0=0,f1=-1,f2=f32(.025),f3=f32(.2))
        vm.run(0x29ef3,0x29f23)
        baseline=f32(smu_fuse*f32(.025))
        selected=max(f32(baseline-1),min(f32(baseline+f32(.2)),f32(f32(mhz)*f32(.001))))
        offset=f32(selected-baseline) if selected else 0
        assert vm.f['f3']==offset
        assert vm.f['f0']==(selected if selected else baseline)
        vm.r['a1']=0x50000;vm.putf(0xae30+0xd4,3);vm.putf(0xae30+0xb8,1.2);vm.put(0xb8a8+2,100,1)
        vm.run(0x29f23,0x29f74)
        assert vm.getf(0x50014)==offset
        rx=Slice(mp5,mc);rx.r.update(a1=0x60000,a2=0x50000)
        # Unused record padding is zero-filled by the fixture, not read by
        # the selected MP5 receiver slice.
        for off in range(24):rx.put(0x50000+off,vm.ram.get(0x50000+off,0),1)
        rx.run(0x2ad20,0x2ad34)
        rx.r['a10']=mp5_fuse;rx.run(0x2ad37,0x2ad3f)
        rx.r['a10']=48;rx.run(0x2ad42,0x2ad5a)
        # Caller a3 survives call8's register window; use another register
        # frame with shared RAM to replay the initializer, not a flattened call.
        init=Slice(mp5,mc);init.ram=rx.ram;init.run(0x295f8,0x29655)
        rx.run(0x2ad5d,0x2ad75)
        expected=f32(offset+mp5_fuse*f32(.005))
        assert rx.getf(0x11440)==expected
        trace.update(struct.pack('<3If',mhz,smu_fuse,mp5_fuse,expected));cases+=1
    return dict(cases=cases,initial_record_cases=initial,ccd_row=10,source='0x11440',unit='GHz',
        meaning='fused frequency policy plus accepted Fmax policy offset; averaged',
        smu_policy_branch='normal fuse branch 0x29ef3; alternate lookup-table branch excluded',
        fuse_calls_stubbed=True,transport_executed=False,measurement=False,setting_readback_verified=False,
        trace_sha256=trace.hexdigest())


def accumulator_bridge(body,code):
    cases=words=0;trace=hashlib.sha256()
    for width in (1,2):
        for ordinal in range(width):
            for samples in (1,2,4):
                vm=Slice(body,code);H['seed_accumulation'](vm)
                expected=[0.0]*8;ccd=[0.0]*3
                for sample in range(samples):
                    for core in range(8):
                        value=1+core/4+sample/8
                        vm.putf(CORE+0x1bc*core+0x68,value);expected[core]+=value/samples
                    for j,(source,value) in enumerate(((0x1287c,5+sample/4),(0x12894,150+sample/8),(0x11440,5.5+sample/4))):
                        vm.putf(source,value);ccd[j]+=value/samples
                    vm.run(0x2f370,0x2f536)
                result=H['export_snapshot'](vm,width,ordinal)
                for core,value in enumerate(expected):assert result[3*8+core]==value
                for row,value in zip((5,9,10),ccd):assert result[144+row]==value
                for value in expected+ccd:trace.update(struct.pack('<f',value));words+=1
                cases+=1
    return dict(cases=cases,new_unit_words=words,core_rows=[3],ccd_rows=[5,9,10],
        samples=[1,2,4],additional_scale_applied=False,trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('smu','mp5','smu-study','mp5-study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    smu=a.smu.read_bytes();assert hashlib.sha256(smu).hexdigest()==R['SHA']
    ss=json.loads(a.smu_study.read_text());assert ss['source_sha256']==R['SHA']
    sc={int(i['address'],16):i for f in ss['functions'] for i in f['instructions']}
    mp5=a.mp5.read_bytes();assert hashlib.sha256(mp5).hexdigest()==H['SHA']
    study=json.loads(a.mp5_study.read_text());assert study['source_sha256']==H['SHA']
    mc={int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
    report=dict(schema=1,source_sha256=H['SHA'],smu_sha256=hashlib.sha256(smu).hexdigest(),hardware_execution=False,
        frequency_anchor=P['scale_sources'](smu,sc,mp5,mc),
        policy_selection=policy_selection(mp5,mc),clock_models=clock_models(mp5,mc),
        ccd_percentage=ccd_percentage(mp5,mc),fused_policy=fused_policy(smu,sc,mp5,mc),
        accumulator_bridge=accumulator_bridge(mp5,mc),new_core_units={'3':'GHz'},new_ccd_units={'5':'GHz','9':'%','10':'GHz'},
        remaining_core_rows=[0,4,12],remaining_ccd_rows=[0,1,2,6,8],
        physical_core_mapping_verified=False,live_validity_verified=False,runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__=='__main__':main()
