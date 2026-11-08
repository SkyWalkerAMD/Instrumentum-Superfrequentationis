#!/usr/bin/env python3
"""Byte-checked common configuration mirrors; synthetic inputs, no physical units.

The two firmware-target positions are not compact PM slots or physical CCDs.
The local configuration mirror can be retained after an equal-code request;
none of these paths supplies an independently verified applied-setting getter.
"""
import argparse,hashlib,itertools,json,runpy,struct
from pathlib import Path

HERE=Path(__file__).parent
H=runpy.run_path(str(HERE/'firmware-granite-semantics.py'))
T=runpy.run_path(str(HERE/'firmware-granite-topology.py'))
Slice,f32=H['Slice'],H['f32']
ACC,OUT,CONFIG=0x15320,0x15b40,0xc270


def helper(body,code,entry,integer=0,value=None):
    v=Slice(body,code);v.r['a2']=integer
    if value is not None:v.f['f0']=value
    v.run(entry,None,hooks={})
    return v.r['a2'] if entry==0x1ca44 else v.f['f0']


def selected(body,code,vm,record,frequency):
    child=Slice(body,code);child.ram=vm.ram.copy();child.r['a2']=record
    child.putf(0x96a0,frequency);child.run(0x29678,None,hooks={})
    first,second,tag=[vm.get(record+i,1) for i in range(3)]
    expected=f32((second if frequency>1600-50*tag else first)*f32(0.006640640087425709))
    assert child.f['f0']==expected
    return expected


def selection_grid(body,code):
    trace=hashlib.sha256();cases=0
    for first,tag,side in itertools.product(range(256),range(8),(-1,0,1)):
        v=Slice(body,code);record=0xa558
        for i,x in enumerate((first,255-first,tag)):v.put(record+i,x,1)
        x=f32(1600-50*tag+side*.125);y=selected(body,code,v,record,x)
        trace.update(struct.pack('<3If',first,tag,side&0xffffffff,y));cases+=1
    # The helper itself accepts every tag byte; only the SMN acquisition masks
    # it to three bits. MP5 76h supplies an unmasked byte in the other record.
    for tag in range(256):
        v=Slice(body,code)
        for i,x in enumerate((3,201,tag)):v.put(0xa568+i,x,1)
        selected(body,code,v,0xa568,float(1600-50*tag))
    return dict(cases=cases,additional_tag_bytes=256,scale_bits='0x3bd999ba',
        second_selected='reference frequency strictly above 1600 - 50 * tag',
        equality_selects_first=True,physical_unit_verified=False,trace_sha256=trace.hexdigest())


def acquisition(body,code):
    cases=0;trace=hashlib.sha256()
    for target,seed in itertools.product(range(2),(0,1,37,127,255)):
        v=Slice(body,code);v.r['a2']=target;reads=[];messages=[]
        replies=[(0xa5000000 | ((seed^0x5a)<<8) | 0xff), (0x5a000000 | (seed<<8) | 0xf8 | (seed&7))]
        registers=[0x18a5a816,0x18a5a818] if target else [0x1895a816,0x1895a818]
        def read(w):
            reads.append((w.r['a10'],w.r['a11'],w.r['a12']))
            w.r['a10']=replies[len(reads)-1]
        reply=0xa5000000 | (seed<<16) | ((seed^0x33)<<8) | (seed^0xcc)
        def request(w):
            messages.append((w.r['a10'],w.r['a11'],w.r['a12']));w.r['a10']=reply
        v.run(0x2960c,None,hooks={0x2b70:read,0x1d488:request})
        assert reads==[(0,p,1) for p in registers],reads
        assert messages==[(target,0x76,0)]
        expected=(seed^0x5a,seed,seed&7,seed^0xcc,seed^0x33,seed)
        actual=tuple(v.get(p+8*target+i,1) for p in (0xa558,0xa568) for i in range(3))
        assert actual==expected,(actual,expected)
        trace.update(bytes(actual));cases+=1
    return dict(cases=cases,firmware_targets=[0,1],mp5_command='0x76',
        smn_tag_mask=7,mp5_tag_mask=255,physical_target_mapping_verified=False,
        read_results_are_fixtures=True,trace_sha256=trace.hexdigest())


def update_targets(body,code,vm,mask,sample_count,frequency,seed):
    vm.r.update(a1=0x50000,a5=0xa558,a7=0xa1c0,a15=sample_count,a6=mask)
    vm.putf(0x50000,1);vm.putf(0x50004,2);vm.putf(0xa54c,3)
    before={p:vm.getf(p) for p in (0xa55c,0xa564,0xa56c,0xa574)};calls=[]
    for target in range(2):
        for kind in range(2):
            record=0xa558+8*target+16*kind
            for i,x in enumerate(((seed+31*target+7*kind)&255,(seed+77+target)&255,2+kind)):
                vm.put(record+i,x,1)
    def choose(w):
        calls.append(w.r['a10']);w.f['f0']=selected(body,code,w,w.r['a10'],frequency)
    vm.run(0x27dc4,0x27df7,hooks={0x29678:choose})
    expected_calls=[]
    for target in range(2):
        for kind in range(2):
            record=0xa558+8*target+16*kind;p=record+4
            enabled=bool(sample_count and mask&(1<<target))
            if enabled:expected_calls.append(record)
            expected=selected(body,code,vm,record,frequency) if enabled else before[p]
            assert vm.getf(p)==expected
    assert calls==expected_calls
    return not sample_count or mask!=3


def export_grid(body,code):
    trace=hashlib.sha256();cases=0;retained=0
    for mask,count,frequency in itertools.product(range(4),(1,2,4),(1400,1450,1500)):
        vm=Slice(body,code);vm.ram.update(enumerate(body))
        for p in range(ACC,ACC+0x340,4):vm.put(p,0)
        for i,p in enumerate((0xa55c,0xa564,0xa56c,0xa574)):vm.putf(p,-10-i)
        totals=[0.0]*4
        # Zero sample count must retain all four previous sources.
        retained+=update_targets(body,code,vm,mask,0,frequency,1)
        for sample in range(count):
            retained+=update_targets(body,code,vm,mask,1,frequency,11+sample)
            for i,p in enumerate((0xa55c,0xa564,0xa56c,0xa574)):totals[i]=f32(totals[i]+vm.getf(p))
            vm.r['a11']=ACC;vm.run(0x2ccad,0x2cd14)
        vm.r.update(a5=ACC);vm.f['f0']=1/count
        vm.run(0x2ce9a,0x2cef0)
        vm.r.update(a12=OUT,a13=301);vm.run(0x2d13a,0x2d14e)
        for i in range(4):
            index=259+i;source=ACC+0x278+4*i
            assert struct.unpack_from('<I',body,0xb340+4*index)[0]==source
            assert vm.getf(OUT+4*index)==f32(totals[i]/count)
        trace.update(struct.pack('<4I',*[vm.get(OUT+4*i) for i in range(259,263)]));cases+=1
    return dict(cases=cases,output_words=4*cases,retention_cases=retained,indices=[259,260,261,262],
        averaging=True,present_mask_exported_with_fields=False,absent_targets_still_accumulated=True,
        positions_are_firmware_targets_not_compact_pm_slots=True,trace_sha256=trace.hexdigest())


def local_mirror(body,code):
    decode=[helper(body,code,0x1ca2c,i) for i in range(256)]
    base=f32(.4974510073661804)
    # Use the reference literal rather than a rounded decimal approximation.
    step=struct.unpack_from('<f',body,0x1143c)[0]
    assert all(y==f32(base+i*step) for i,y in enumerate(decode))
    init_cases=0
    for seed in (0,1,37,127,255):
        v=Slice(body,code);calls=[]
        def read(w):
            record,selector=w.r['a10'],w.r['a11'];assert record in (CONFIG,CONFIG+20,CONFIG+40)
            calls.append((record,selector));w.r['a10']=(seed+(record-CONFIG)//20+selector)&255
        def convert(w):w.f['f0']=helper(body,code,0x1ca2c,w.r['a10'])
        v.run(0x321e0,None,hooks={0x1c9ec:read,0x1ca2c:convert})
        assert calls==[(CONFIG+20*i,s) for i in range(3) for s in (3,4,0)]
        for i in range(3):
            assert v.getf(CONFIG+20*i+12)==decode[(seed+i)&255]
            assert v.getf(CONFIG+20*i+16)==decode[(seed+i)&255]
        init_cases+=1
    trace=hashlib.sha256();cases=equal=down=writes=0
    for old,new in itertools.product((0,1,127,254,255),repeat=2):
        v=Slice(body,code);v.put(CONFIG+1,old,1);v.putf(CONFIG+12,-999)
        v.f['f0']=f32(base+(new+.5)/f32(337.989013671875));calls=[];waits=[]
        def quantize(w):w.r['a10']=helper(body,code,0x1ca44,value=w.f['f0']);assert w.r['a10']==new
        def write(w):calls.append((w.r['a10'],w.r['a11'],w.r['a12']))
        def convert(w):w.f['f0']=helper(body,code,0x1ca2c,w.r['a10'])
        def wait(w):waits.append(w.r['a10'])
        v.run(0x32220,None,budget=20000,hooks={0x1ca44:quantize,0x1c9c4:write,0x1ca2c:convert,0x2f1c:wait})
        sequence=list(range(old+(1 if new>old else -1),new+(1 if new>old else -1),1 if new>old else -1)) if new!=old else []
        assert calls==[(CONFIG,0,x) for x in sequence] and waits==[25]*len(sequence)
        assert v.get(CONFIG+1,1)==new and v.getf(CONFIG+12)==(decode[new] if sequence else -999)
        equal+=new==old;down+=new<old;writes+=len(calls);cases+=1
        trace.update(struct.pack('<IIf',old,new,v.getf(CONFIG+12)))
    assert struct.unpack_from('<I',body,0xb340+269*4)[0]==CONFIG+12
    for bits in (0x80000000,1,0xbf800000,0x3f800000,0x7f800001):
        v=Slice(body,code);v.ram.update(enumerate(body));v.put(CONFIG+12,bits)
        v.r.update(a12=OUT,a13=301);v.run(0x2d13a,0x2d14e)
        assert v.get(OUT+269*4)==bits
    return dict(decode_codes=256,decode_base_bits='0x3efeb1e6',decode_step_bits='0x3b41e659',
        initializer_cases=init_cases,initializer_records=3*init_cases,transition_cases=cases,
        descending_cases=down,equal_code_retentions=equal,synthetic_write_calls=writes,
        raw_read_during_step_setter=False,peripheral_effects_verified=False,delay_units_verified=False,
        averaged=False,pm_index=269,direct_copy_words=5,trace_sha256=trace.hexdigest())


def density_immediates():
    # ISA-generated fixtures, explicitly separate from firmware-byte replays.
    # Cadence MOVI.N RI7: imm[6:4] at bits 6:4; imm[3:0] at bits 15:12.
    for reg,imm in itertools.product(range(16),range(128)):
        raw=bytes([0x0c|((imm>>4)<<4),reg|((imm&15)<<4)])
        expected=imm-128 if imm>=96 else imm
        ins={0:dict(bytes=raw.hex(),text=f'movi.n a{reg},0x{expected&255:x}')}
        v=Slice(raw,ins);v.run(0,2);assert v.r['a'+str(reg)]==expected&0xffffffff
    return dict(synthetic_isa_cases=2048,range=[-32,95],firmware_execution=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('smu','study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();body=a.smu.read_bytes();code=T['instructions'](a.study,body,T['SMU_SHA'])
    result=dict(schema=1,source_sha256=T['SMU_SHA'],hardware_execution=False,
        selection=selection_grid(body,code),acquisition=acquisition(body,code),
        export=export_grid(body,code),local_mirror=local_mirror(body,code),
        movi_n=density_immediates(),
        physical_units_verified=False,physical_ccd_mapping_verified=False,
        independent_second_rail_verified=False,runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()
