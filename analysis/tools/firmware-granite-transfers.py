#!/usr/bin/env python3
"""Byte-checked Granite descriptor preparation and producer source coordinates.

All addresses below belong to the MP5 reference image/address space. MMIO is
synthetic RAM: no controller, DMA copy, completion, interrupt or MSR is emulated.
Descriptor coordinates are not physical CCD/APIC/OS identities or unit anchors.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

H=runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))
T=runpy.run_path(str(Path(__file__).with_name('firmware-granite-topology.py')))
Slice,f32,CORE=H['Slice'],H['f32'],H['CORE']


class DescriptorVM(Slice):
    """Integer-only slices, with a single Xtensa zero-overhead loop controller.

    A branch can leave a loop before LEND; a later LOOP replaces the old loop.
    Interrupt-mask operations are recorded but deliberately have no concurrency
    semantics. No CALL or peripheral completion operation is accepted.
    """
    def run(self,pc,stop=None,budget=10000):
        loop=None;val=lambda x:self.r[x] if x.startswith('a') else int(x,0)
        signed=lambda x:x-(1<<32) if x&(1<<31) else x
        for _ in range(budget):
            if pc==stop:return
            i=self.code[pc];raw=bytes.fromhex(i['bytes'])
            assert self.body[pc:pc+len(raw)]==raw
            op,_,rest=i['text'].partition(' ');op=op.removesuffix('.n')
            a=[x.strip() for x in rest.split(',') if x.strip()]
            nxt=pc+len(raw);self.trace.append(pc)
            if op=='retw':return
            if op in ('entry','memw','wsr'):pass
            elif op=='rsil':self.r[a[0]]=0 # PS is an explicit opaque fixture.
            elif op in ('mov','movi'):self.r[a[0]]=val(a[1])&0xffffffff
            elif op=='l32r':self.r[a[0]]=struct.unpack_from('<I',self.body,val(a[1]))[0]
            elif op in ('l8ui','l32i'):self.r[a[0]]=self.get(val(a[1])+val(a[2]),1 if op=='l8ui' else 4)
            elif op in ('s8i','s32i'):self.put(val(a[1])+val(a[2]),val(a[0]),1 if op=='s8i' else 4)
            elif op in ('add','addi','addmi','addx4'):
                self.r[a[0]]=((4 if op=='addx4' else 1)*val(a[1])+val(a[2]))&0xffffffff
            elif op=='or':self.r[a[0]]=val(a[1])|val(a[2])
            elif op=='ssl':self.shift=val(a[0])&31
            elif op=='sll':self.r[a[0]]=(val(a[1])<<self.shift)&0xffffffff
            elif op=='slli':self.r[a[0]]=(val(a[1])<<val(a[2]))&0xffffffff
            elif op=='extui':self.r[a[0]]=(val(a[1])>>val(a[2]))&((1<<val(a[3]))-1)
            elif op=='beqz':
                if not val(a[0]):nxt=val(a[1])
            elif op=='bnez':
                if val(a[0]):nxt=val(a[1])
            elif op=='bne':
                if val(a[0])!=val(a[1]):nxt=val(a[2])
            elif op=='beqi':
                if val(a[0])==val(a[1]):nxt=val(a[2])
            elif op=='blti':
                if signed(val(a[0]))<val(a[1]):nxt=val(a[2])
            elif op=='bany':
                if val(a[1])&val(a[2]):nxt=val(a[0])
            elif op=='j':nxt=val(a[0])
            elif op=='loopgtz':
                count,end=val(a[0]),val(a[1]);loop=[nxt,end,count] if signed(count)>0 else None
                if loop is None:nxt=end
            else:raise ValueError(f'Unmodeled descriptor instruction {pc:x}: {i["text"]}')
            if loop and nxt==loop[1]:
                loop[2]-=1
                if loop[2]:nxt=loop[0]
                else:loop=None
            pc=nxt
        raise ValueError('Descriptor instruction budget exceeded')


def prepare(body,code):
    cases=disabled=markers=0;trace=hashlib.sha256()
    for enabled,mask,count,parity in itertools.product((0,1),range(256),(1,3),(0,1)):
        vm=DescriptorVM(body,code)
        table=(0x16080 if count==1 else 0x15c00)+parity*(0x80 if count==1 else 0x180)
        pointer=(0x12e64 if count==1 else 0x12e58)+4*parity
        vm.r.update(a1=0x50000,a2=pointer,a3=count,a4=parity,a5=0)
        vm.put(pointer,table);vm.put(0x7820,enabled,1);vm.put(0x7840,mask)
        for core in range(8):vm.put(0x782d+core,(mask>>core)&1,1)
        for j in range(4*count):vm.put((table+32*j)|0x3400000,struct.unpack_from('<I',body,table+32*j)[0])
        vm.run(0x30350)
        selected=[j for j in range(4) if enabled and mask&(1<<(parity+2*j))]
        expected_pointer=table+32*count*selected[0] if selected else 0
        assert vm.get(pointer)==expected_pointer
        for j in range(4*count):
            expected=0x08500000
            if selected:
                if j//count not in selected:expected|=0x01f00000;disabled+=1
                elif j==count*(selected[-1]+1)-1:expected|=1;markers+=1
            actual=vm.get((table+32*j)|0x3400000);assert actual==expected
            trace.update(struct.pack('<I',actual))
        trace.update(struct.pack('<5I',enabled,mask,count,parity,expected_pointer));cases+=1
    # Third channel has its own enable gate and termination bit.
    for enabled,count in itertools.product((0,1),(1,12)):
        table=0x16180 if count==1 else 0x15f00;pointer=0x12e6c if count==1 else 0x12e60
        vm=DescriptorVM(body,code);vm.r.update(a2=pointer,a3=count)
        vm.put(0x7820,enabled,1);vm.put(pointer,table)
        for j in range(count):vm.put((table+32*j)|0x3400000,struct.unpack_from('<I',body,table+32*j)[0])
        vm.run(0x3040c)
        assert vm.get(pointer)==(table if enabled else 0)
        assert vm.get((table+32*(count-1))|0x3400000)==(0x08500001 if enabled or count==1 else 0x08500000)
    return dict(cases=cases,third_channel_cases=4,disabled_descriptor_markers=disabled,
        end_markers=markers,trace_sha256=trace.hexdigest(),descriptor_effects_emulated=False)


def submission(body,code):
    cases=writes=0;trace=hashlib.sha256()
    for pointers in itertools.product((0,0x15c00),(0,0x15d80),(0,0x15f00)):
        vm=DescriptorVM(body,code);vm.r['a2']=0x12e58
        for channel,pointer in enumerate(pointers):vm.put(0x12e58+4*channel,pointer)
        vm.run(0x317b8)
        expected={}
        for channel,pointer in enumerate(pointers):
            if not pointer:continue
            base=0x3270e00+0x1000*channel
            for address,value,size in ((base+0x208,pointer,4),(base+0x200,0x79,4),
                    (0x1b2a8+channel,2,1),(0x1b2d8+4*channel,pointer,4)):
                assert vm.get(address,size)==value
                for j in range(size):expected[address+j]=(value>>(8*j))&255
            writes+=2
        assert {a:v for a,v in vm.ram.items() if not 0x12e58<=a<0x12e64}==expected
        trace.update(struct.pack('<3I',*pointers));cases+=1
    return dict(cases=cases,synthetic_mmio_writes=writes,channels=3,
        null_pointer_skips_channel=True,completion_emulated=False,interrupts_emulated=False,
        trace_sha256=trace.hexdigest())


def coordinates(body,code):
    u=lambda at:struct.unpack_from('<I',body,at)[0]
    core=[]
    for slot in range(8):
        table=0x15c00+(slot%2)*0x180+(slot//2)*0x60
        flags,length,source,dest=struct.unpack_from('<4I',body,table)
        assert flags==0x08500000 and length==0x78
        assert source==0x160010c+slot*0x20000 and dest==CORE+slot*0x1bc+0x138
        energy=0x16080+(slot%2)*0x80+(slot//2)*0x20
        ef,el,es,ed=struct.unpack_from('<4I',body,energy)
        assert (ef,el,es,ed)==(0x08500000,8,CORE+slot*0x1bc+0xe0,0x19010c0+slot*0x20000)
        core.append(dict(local_slot=slot,producer_addresses={str(row):hex(CORE+slot*0x1bc+offset)
            for row,offset in ((0,0x10),(4,0xf4),(12,0xf8))},
            counter_addresses={'4':hex(source+0x28),'12':hex(source+0x30)},
            counter_descriptor=hex(table),integrator_descriptor=hex(energy),integrator_destination=hex(ed)))
    assert struct.unpack_from('<4I',body,0x15f20)==(0x08500000,16,0x187f170,0x128ec)
    # CCD row 6 is acquired directly; row 8 uses the second CCD descriptor.
    assert u(0x18e58)==0x187ee00 and u(0x194e0)==0x12518
    assert code[0x2d5c8]['text']=='l32i a14,a4,0x334'
    assert code[0x2d63a]['text']=='s32i a14,a2,0x388'
    ccd=[dict(row=row,producer_address=hex(address),counter_addresses=[hex(v) for v in counters])
        for row,address,counters in ((0,0x12804,()),(1,0x12848,()),(2,0x8030,()),
            (6,0x128a8,(0x187f134,)),(8,0x12888,(0x187f170,0x187f174,0x187f17c)))]
    return dict(address_space='mp5_reference_local',core=core,ccd=ccd,
        dma_copy_emulated=False,counter_event_identity_verified=False,
        physical_or_os_mapping_verified=False)


def aggregate(body,code):
    cases=0;trace=hashlib.sha256()
    for seed in range(32):
        vm=Slice(body,code)
        values=[(seed+1)*(slot+1)/8 for slot in range(8)]
        a,b=seed/4,seed/2
        for at in (0x12800,0x12808,0x1280c,0x12d44,0x12b10,0x12b18):vm.putf(at,0)
        vm.putf(0x12804,a);vm.putf(0x12848,b)
        for slot,value in enumerate(values):
            for offset in (0xc,0x1c,0x20,0x24):vm.putf(CORE+0x1bc*slot+offset,0)
            vm.putf(CORE+0x1bc*slot+0x10,value)
        vm.run(0x2ce90,0x2cf35)
        expected={0x12a2c:sum(values),0x12a30:sum(values)+a+b,0x12a6c:sum(values)+a,0x12a70:b}
        for at,value in expected.items():assert vm.getf(at)==value;trace.update(struct.pack('<If',at,value))
        cases+=1
    assert struct.unpack_from('<4I',body,0x15960)==(0x08500001,0xe4,0x12a2c,0)
    destinations=[]
    for target in (0,1):
        vm=Slice(body,code);vm.r.update(a3=0x19d20,a5=0xfffff,a6=0x1300000,a11=0x3415960)
        vm.put(0x19d54,0xa1c0+target*0xe4)
        vm.run(0x2d209,0x2d216)
        expected=0x130a1c0+target*0xe4;assert vm.get(0x341596c)==expected
        destinations.append(hex(expected))
    return dict(cases=cases,descriptor='0x15960',bytes=0xe4,source='0x12a2c',
        destinations_by_firmware_target=destinations,
        startup_pointer_is_fixture=True,copy_or_smu_consumer_emulated=False,
        trace_sha256=trace.hexdigest(),physical_units_verified=False)


def integrator(body,code):
    scale=struct.unpack_from('<f',body,0x12e1c)[0]
    assert struct.unpack_from('<I',body,0x12e1c)[0]==0x4283126f
    cases=wraps=0;trace=hashlib.sha256()
    for slot,value,low,high in itertools.product(range(8),(0,.5,1,16),(0,0xfffffff0),(0,0xffffffff)):
        vm=Slice(body,code);state=CORE+slot*0x1bc;vm.r['a2']=state
        vm.f.update(f3=value,f5=scale);vm.put(state+0xe0,low);vm.put(state+0xe4,high)
        vm.run(0x2c872,0x2c88b)
        inc=round(f32(value*scale));new=(low+inc)&0xffffffff
        carry=int(new<low);expectedhigh=(high+carry)&0xffffffff
        assert vm.get(state+0xe0)==new and vm.get(state+0xe4)==expectedhigh
        trace.update(struct.pack('<4I',slot,inc,new,expectedhigh));cases+=1;wraps+=carry
    return dict(cases=cases,carry_cases=wraps,initial_scale=scale,
        low_word_offset='0xe0',high_word_offset='0xe4',trace_sha256=trace.hexdigest(),
        coefficient_is_initial_image_value=True,period_seconds_verified=False,
        energy_msr_binding_verified=False,physical_unit_verified=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('mp5','study','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();body=a.mp5.read_bytes();code=T['instructions'](a.study,body,H['SHA'])
    report=dict(schema=1,source_sha256=H['SHA'],hardware_execution=False,
        descriptor_preparation=prepare(body,code),submission=submission(body,code),
        coordinates=coordinates(body,code),aggregate=aggregate(body,code),integrator=integrator(body,code),
        new_physical_units={},runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='coordinates'}))


if __name__=='__main__':main()
