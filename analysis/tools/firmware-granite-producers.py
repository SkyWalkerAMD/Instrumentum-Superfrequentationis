#!/usr/bin/env python3
"""Resolve eight Granite PM producer paths without inventing physical units.

Byte-checked slices run on synthetic RAM. Peripheral reads, interprocessor
transport, transcendental model helpers and reciprocal approximations are not
executed. An arithmetic identity alone does not establish W, V, GHz or Hz.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

H=runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))
Slice,f32,CORE=H['Slice'],H['f32'],H['CORE']


def model_tails(body,code):
    cases=retained=0;trace=hashlib.sha256()
    # Model-helper results are independent inputs; the coefficient scales and
    # the helpers' exponential/power approximations are deliberately unresolved.
    for voltage,ceiling,leak,activity,a,b in itertools.product(
            (.5,1,1.5),(.75,1.25),(-1,0,.5),(0,.25,2),(.125,1),(.25,2)):
        vm=Slice(body,code);vm.r['a2']=CORE
        vm.f.update(f1=0,f2=b,f3=0,f5=1,f6=b,f7=a,f8=ceiling)
        for off,value in ((0,voltage),(8,leak),(0xd0,activity),(0x10,19)):
            vm.putf(CORE+off,value)
        vm.run(0x2c83c,0x2c86f)
        dynamic=f32(activity*f32(voltage*f32(b+voltage*a)))
        current=f32(leak+dynamic)
        expected=f32(max(0,f32(f32(ceiling-voltage)*current))+current*voltage)
        assert vm.getf(CORE+0x10)==expected and vm.getf(CORE+0x18)==19
        # The same table/helper output appears in the CCD 0 model tail.
        vm.r.update(a2=0x12634,a3=0x12914);vm.f['f1']=leak
        for address,value in ((0x12b10,voltage),(0x12634+0x338,a),
                (0x12634+0x33c,b),(0x12634+0x238,activity)):vm.putf(address,value)
        vm.run(0x2ccf0,0x2cd14)
        ccd0=f32(current*voltage);assert vm.getf(0x12804)==ccd0
        # CCD 1 has an additional nonnegative correction. Do not simplify it
        # to current*max(voltage,ceiling): synthetic negative model inputs
        # distinguish that incorrect shortcut.
        vm.r.update(a2=0x12638,a3=0x7e34);vm.f['f0']=leak
        for address,value in ((0x8030,voltage),(0x12894+0x168,a),
                (0x12894+0x16c,b),(0x12638+0x228,activity),(0x12894+0x294,ceiling)):
            vm.putf(address,value)
        vm.run(0x2cde9,0x2ce28)
        ccd1=f32(f32(current*voltage)+max(0,f32(current*f32(ceiling-voltage))))
        assert vm.getf(0x12848)==ccd1
        trace.update(struct.pack('<3f',expected,ccd0,ccd1));cases+=1
    for value in (-3,0,17):
        vm=Slice(body,code);vm.put(0x7820,0,1);vm.putf(0x12848,value)
        vm.run(0x2cdc8,0x2ce28);assert vm.getf(0x12848)==value;retained+=1
    return dict(cases=cases,ccd1_disabled_retained_cases=retained,core_rows=[0],ccd_rows=[0,1],
        source_addresses=['0x11a28 + 0x1bc * local_slot','0x12804','0x12848'],
        model_helper_outputs_are_fixtures=True,coefficient_units_verified=False,
        physical_units_verified=False,trace_sha256=trace.hexdigest())


def configuration(body,code):
    cases=updates=decreases=unchanged=0;trace=hashlib.sha256()
    step,base=(struct.unpack_from('<f',body,at)[0] for at in (0x18e74,0x18e78))
    for value in range(256):
        vm=Slice(body,code);vm.r.update(a3=0x7fac,a5=0x7820)
        vm.put(0x802a,value,1);vm.run(0x25844,0x25864)
        expected=f32(base+value*step)
        assert vm.getf(0x8030)==expected
        trace.update(struct.pack('<If',value,expected));cases+=1
    for low,high in itertools.product((500,750,1000,1250),(0,500,1000,1250)):
        vm=Slice(body,code);vm.r['a10']=low|(high<<16)
        vm.run(0x2bebc,0x2bee8)
        first=f32(low*f32(.001));second=f32((high or low)*f32(.001))
        assert vm.getf(0x119ac)==first and vm.getf(0x119b0)==second
        # Later selection copies the float and code into a 216-byte record.
        # Test the true current state: a changed float with an unchanged byte
        # does not refresh 0x8030 on either the increase or decrease branch.
        for old,new,incoming in ((8,9,first),(9,8,second),(9,9,first),(9,9,second)):
            vm.r.update(a4=0x1171c,a3=0x11644,a5=0x7820,a7=0x78c)
            vm.put(0x11725,new,1);vm.put(0x1164d,old,1);vm.putf(0x11728,incoming)
            vm.putf(0x8030,17)
            vm.run(0x27c6f,0x27c78 if new>old else 0x27ca3)
            if new>old:
                # Stop after the peripheral command has been issued: its
                # completion/success is not inferred from this RAM update.
                vm.run(0x27c8f,0x27ca0)
                assert vm.getf(0x8030)==incoming and vm.get(0x802a,1)==new
            else:assert vm.getf(0x8030)==17
            vm.run(0x27fa4,0x27fad if new<old else 0x27fd3)
            if new<old:
                vm.run(0x27fc4,0x27fd3)
                assert vm.getf(0x8030)==incoming and vm.get(0x802a,1)==new
                decreases+=1
            if new==old:
                assert vm.getf(0x8030)==17;unchanged+=1
            updates+=1
    return dict(initial_code_cases=cases,update_gate_cases=updates,decrease_cases=decreases,
        unchanged_code_retained_cases=unchanged,ccd_row=2,source='0x8030',
        initial_formula='float32(base + byte[0x802a] * step)',base=base,step=step,
        incoming_scale=.001,mp5_command='0x75',
        update='record + 12 copied when record byte + 9 changes; unchanged code retains old float',
        complete_transition_sequence_replayed=False,physical_unit_verified=False,measurement=False,
        command_units_verified=False,peripheral_writes_executed=False,trace_sha256=trace.hexdigest())


def core_rates(body,code):
    cases=wrapped=0;trace=hashlib.sha256()
    for elapsed,d4,d12,old in itertools.product((16,256,4096),(0,1,64,4096),(0,2,512),(0,0xfffffff0)):
        vm=Slice(body,code);vm.r.update(a4=CORE,a13=elapsed,a2=0,a12=0)
        vm.f.update(f0=0,f1=1,f7=100,f9=f32(.001))
        vm.put(CORE+0x154,0);vm.run(0x2c4c2,0x2c4c8)
        vm.f['f13']=1/elapsed  # exact dyadic reciprocal fixture
        vm.run(0x2c4cb,0x2c4d4)
        for prior,current,delta in ((0x110,0x160,d4),(0x114,0x168,d12),
                (0x10c,0x158,0),(0x118,0x170,0),(0x11c,0x188,0)):
            vm.put(CORE+prior,old);vm.put(CORE+current,(old+delta)&0xffffffff)
        vm.put(CORE+0x1a8,0);vm.put(CORE+0x1ac,0)
        vm.run(0x2c54b,0x2c56f if d4 else 0x2c578)
        expected4=f32(f32(d4*(100/elapsed))*f32(.001))
        assert vm.getf(CORE+0xf4)==expected4
        if d4:
            vm.f['f3']=f32(1/vm.f['f3']);vm.run(0x2c572,0x2c578)
        vm.run(0x2c578,0x2c5ed)
        expected12=f32(d12*(100/elapsed));assert vm.getf(CORE+0xf8)==expected12
        assert vm.get(CORE+0x110)==(old+d4)&0xffffffff
        assert vm.get(CORE+0x114)==(old+d12)&0xffffffff
        trace.update(struct.pack('<3If',elapsed,d4,d12,expected4));cases+=1
        wrapped+=int(old+d4>0xffffffff or old+d12>0xffffffff)
    # Existing byte-checked acquisition identifies the elapsed-counter address,
    # not its absolute tick duration. Its value here is synthetic memory.
    for previous,current in ((1,17),(0xfffffff0,0x10),(42,42)):
        vm=Slice(body,code);vm.r.update(a12=CORE,a7=0x31ffe00)
        vm.put(CORE+0xc8,previous);vm.put(0x32001c0,current)
        vm.run(0x2c1f4,0x2c203)
        assert vm.get(CORE+0xb4)==(current-previous)&0xffffffff
    gated=overflow=0
    for flag1,flag2,carry in itertools.product((0,1),(0,1),(0,1)):
        vm=Slice(body,code);vm.r.update(a4=CORE,a10=0);vm.f['f0']=0
        for off,value in ((0x87,flag1),(0x89,flag2)):vm.put(CORE+off,value,1)
        vm.put(CORE+0xbc,carry);vm.putf(CORE+0xf4,17);vm.putf(CORE+0xf8,19)
        stop=0x2c61a if flag1 or flag2 else 0x2c734 if carry else 0x2c4b0
        vm.run(0x2c490,stop)
        if flag1 or flag2:
            vm.run(0x2c61a,0x2c638)
            assert vm.getf(CORE+0xf4)==vm.getf(CORE+0xf8)==0;gated+=1
        else:
            assert vm.getf(CORE+0xf4)==17 and vm.getf(CORE+0xf8)==19
            if carry:assert vm.get(CORE+0xbc)==0;overflow+=1
    return dict(cases=cases,wrapped_cases=wrapped,timer_delta_cases=3,gated_zero_cases=gated,
        overflow_retained_cases=overflow,core_rows=[4,12],
        timer_address='0x32001c0',row4_formula='float32(float32(delta160 * (100 / elapsed)) * 0.001f)',
        row12_formula='float32(delta168 * (100 / elapsed))',
        counter_event_identities_verified=False,timer_hz_verified=False,
        physical_units_verified=False,trace_sha256=trace.hexdigest())


def ccd_rates(body,code):
    cases=0;trace=hashlib.sha256()
    coefficient=f32(struct.unpack_from('<f',body,0x7cac)[0])
    for shift,elapsed,low,high,tag,old in itertools.product((1,8,16,31),(16,256),
            (0,0x12345678,0xfffffff0),(0,1),(0,17,63),(0,0xfffffff0)):
        vm=Slice(body,code)
        # Initializers: the same K is used with reciprocal and direct scales.
        vm.r.update(a2=0,a3=0x12f6c);vm.putf(0x7cac,coefficient)
        vm.run(0x2d818,0x2d835);factor=vm.getf(0x1302c)
        assert factor==f32(coefficient*f32(.05))
        vm.r.update(a4=0x12d84,a3=0x19d20,a5=0xfffff,a6=0x1300000,a7=1,a2=0x3400000)
        vm.f['f0']=f32(1/coefficient)  # bounded reciprocal fixture
        vm.put(0x19d20+0x38,0);vm.put(0x12e3b,shift,1)
        vm.run(0x2d1dc,0x2d207)
        # float.s is signed: shift 31 deliberately checks the negative case.
        power=1<<shift;power=power-(1<<32) if shift==31 else power
        scale=f32(power*f32(1/coefficient));assert vm.getf(0x12e18)==scale
        vm.r.update(a5=0x12518,a7=0x12dac,a3=shift,a4=elapsed,a9=1)
        for off,val in ((0x3d4,low),(0x3d8,high),(0x3e0,tag<<4),
                (0x388,13),(0x38c,0xfffffff0),(0x36c,old),(0x3c4,0),(0x3d0,0),(0x3c8,0)):
            vm.put(0x12518+off,val)
        vm.run(0x2cb0e,0x2cb33);vm.f['f2']=1/elapsed
        vm.run(0x2cb36,0x2cba8)
        q=(tag-shift-1)&0xffffffff;q=q if q<32 else 0
        reconstructed=((high<<((32-shift)&31))+(low>>(shift&31))+(1<<q))&0xffffffff
        delta=(reconstructed-old)&0xffffffff
        expected6=f32(f32(f32(29)*f32(.1))/elapsed)
        expected8=f32(f32(delta)*f32(f32(scale*100)/elapsed))
        assert vm.getf(0x128a8)==expected6 and vm.getf(0x12888)==expected8
        assert vm.get(0x12884)==reconstructed
        trace.update(struct.pack('<2f',expected6,expected8));cases+=1
    return dict(cases=cases,ccd_rows=[6,8],shared_parameter='float[0x7cac]',
        counter_scale='float32(signed32(1 << shift) * reciprocal(K))',
        export_factor='float32(K * 0.05f) / 65536',
        mathematical_cancellation='Ignoring rounding: signed32(1 << shift) * 5 / 65536',
        cancellation_is_not_a_replacement_algorithm=True,tag_underflow_or_overflow_uses_bit0=True,
        physical_units_verified=False,shift31_is_supported_setting=False,
        peripheral_acquisition_executed=False,trace_sha256=trace.hexdigest())


def bridge(body,code):
    cases=words=0;trace=hashlib.sha256()
    for width,ordinal in ((1,0),(2,0),(2,1)):
        for samples in (1,2,4):
            vm=Slice(body,code);H['seed_accumulation'](vm)
            expected={}
            for sample in range(samples):
                for row,off in ((0,0x10),(4,0xf4),(12,0xf8)):
                    for core in range(8):
                        value=1+row+core/4+sample/8
                        vm.putf(CORE+core*0x1bc+off,value)
                        expected[row*8+core]=expected.get(row*8+core,0)+value/samples
                for row,at in ((0,0x12804),(1,0x12848),(2,0x8030),(6,0x128a8),(8,0x12888)):
                    value=2+row+sample/8;vm.putf(at,value)
                    # Non-unity, changing scale proves the order: the product
                    # is accumulated, not the product of separately averaged operands.
                    factor=(sample+1)/2 if row==8 else 1
                    if row==8:vm.putf(0x1302c,65536*factor)
                    expected[144+row]=expected.get(144+row,0)+value*factor/samples
                vm.run(0x2f370,0x2f536)
            output=H['export_snapshot'](vm,width,ordinal)
            for index,value in expected.items():
                assert output[index]==value,(index,output[index],value)
                trace.update(struct.pack('<If',index,value));words+=1
            cases+=1
    return dict(cases=cases,checked_words=words,core_rows=[0,4,12],ccd_rows=[0,1,2,6,8],
        ccd8_scale_applied_before_accumulation=True,trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('mp5','study','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();body=a.mp5.read_bytes();assert hashlib.sha256(body).hexdigest()==H['SHA']
    study=json.loads(a.study.read_text());assert study['source_sha256']==H['SHA']
    code={int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
    for pc,i in code.items():assert body[pc:pc+len(bytes.fromhex(i['bytes']))]==bytes.fromhex(i['bytes'])
    assert struct.unpack_from('<I',body,0x13298+8*0x75)[0]==0x2beb4
    report=dict(schema=1,source_sha256=H['SHA'],hardware_execution=False,
        models=model_tails(body,code),configuration=configuration(body,code),
        core_rates=core_rates(body,code),ccd_rates=ccd_rates(body,code),export=bridge(body,code),
        new_physical_units={},remaining_core_rows=[0,4,12],remaining_ccd_rows=[0,1,2,6,8],
        physical_mapping_verified=False,runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report))


if __name__=='__main__':main()
