#!/usr/bin/env python3
"""Byte-checked channel overrides and command mirrors, with scripted MMIO only.

Mailbox and synchronization helpers are explicit stubs. The direct VID helper
is replayed separately at each call boundary. No device voltage is measured,
no physical rail is identified, and no application writer is enabled.
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
SHA=R['SHA']; STATE=0x8c30; ACC=0x15320


def writer(body,code,channel,vid,locked=0,mode=0):
    entry,call,after,stop=((0x20f84,0x20f93,0x20f96,0x20fae),
                          (0x20f10,0x20f1f,0x20f22,0x20f3a),
                          (0x20f3c,0x20f4b,0x20f4e,0x20f81))[channel]
    vm=Slice(body,code);vm.r.update(a1=0x50000,a2=vid)
    for c in range(3):
        vm.put(STATE+0x3c+24*c,0xabcd,2)
        vm.put(STATE+0x40+24*c,locked if c==channel else 7,2)
        vm.putf(STATE+0x44+24*c,17+c)
    vm.put(0xc598+0x36,mode,1)
    vm.run(entry,stop if locked else call)
    packets=[]
    if not locked:
        assert code[call]['text']=='call8 0x00032d6c'
        assert vm.r['a10']==channel and vm.r['a11']==vid
        peripheral=R['VoltageSlice'](body,code,channel,vid,1)
        peripheral.run(0x32d6c,0x32dcb)
        assert not peripheral.responses
        packets=peripheral.writes
        expected=[(address,value&0xffffffff) for address,value in peripheral.expected_writes]
        assert packets==expected
        # Resume the caller's own window; the separately replayed helper does
        # not modify this caller's state base or incoming VID argument.
        vm.run(after,stop)
    for c in range(3):
        assert vm.get(STATE+0x40+24*c,2)==(locked if c==channel else 7)
        if locked or c!=channel:
            assert vm.get(STATE+0x3c+24*c,2)==0xabcd
            assert vm.getf(STATE+0x44+24*c)==17+c
        else:
            scale,offset=(.01,.49) if c==2 and mode==1 else (.005,.245)
            expected=f32(f32(offset)+f32(vid)*f32(scale))
            assert vm.get(STATE+0x3c+24*c,2)==(vid&65535)
            assert vm.getf(STATE+0x44+24*c)==expected
    return vm,packets


def overrides(body,code):
    u=lambda a:struct.unpack_from('<I',body,a)[0]
    routes=((1,0x11,0x2be90,0x2be98,0x20de4,0x20dea,0x20ded,0x20df7,0x20dfa,0x20e02,0x20f10,
             0x12,0x2bea4,0x2bea7,0x20e08,0x20e13),
            (2,0x13,0x2beb4,0x2bebc,0x20e18,0x20e1e,0x20e21,0x20e2b,0x20e2e,0x20e36,0x20f3c,
             0x14,0x2bec8,0x2becb,0x20e3c,0x20e47))
    cases=0;trace=hashlib.sha256();details=[]
    for channel,cmd,handler,hcall,entry,lock,begin,call,after,unlock,direct,release,rhandler,rcall,rentry,rstop in routes:
        assert u(0x6fa4+8*cmd)==handler and u(0x6fa4+8*release)==rhandler
        assert code[hcall]['text']==f'call8 0x{entry:08x}'
        assert code[rcall]['text']==f'call8 0x{rentry:08x}'
        assert code[lock]['text']=='call8 0x0003bb64' and code[unlock]['text']=='call8 0x0003bc38'
        for vid,prior_lock in itertools.product((0,1,128,255,511,512,65535,0xffffffff),(0,1,7,65535)):
            vm=Slice(body,code);vm.r.update(a1=0x50000,a2=vid)
            flag=STATE+0x40+24*channel;vm.put(flag,prior_lock,2)
            vm.run(entry,lock);assert vm.r['a10']==0x2000
            vm.run(begin,call);assert vm.get(flag,2)==0 and vm.r['a10']==vid
            assert code[call]['text']==f'call8 0x{direct:08x}'
            vm.run(after,unlock);assert vm.get(flag,2)==1 and vm.r['a10']==0x2000
            # Release clears only the override flag; it does not restore the
            # previous VID or confirm a peripheral value.
            vm.put(STATE+0x3c+24*channel,0x1234,2)
            vm.putf(STATE+0x44+24*channel,1.25)
            vm.run(rentry,rstop)
            assert vm.get(flag,2)==0
            assert vm.get(STATE+0x3c+24*channel,2)==0x1234 and vm.getf(STATE+0x44+24*channel)==1.25
            cases+=1;trace.update(struct.pack('<4I',channel,vid,prior_lock,vm.get(flag,2)))
        details.append(dict(channel=channel,override_command=hex(cmd),release_command=hex(release),
            handler=hex(handler),writer=hex(direct),override_flag=hex(STATE+0x40+24*channel)))
    return dict(cases=cases,routes=details,mailbox_argument_and_lock_helpers_stubbed=True,
        argument_range_checked_in_selected_path=False,release_restores_previous_voltage=False,
        hardware_voltage_readback=False,trace_sha256=trace.hexdigest())


def command_mirrors(body,code):
    cases=locked_cases=oversized=0;trace=hashlib.sha256()
    for channel,vid,locked,mode in itertools.product(range(3),range(512),(0,1),(0,1,2,255)):
        vm,packets=writer(body,code,channel,vid,locked,mode)
        value=vm.getf(STATE+0x44+24*channel)
        trace.update(struct.pack('<4If',channel,vid,locked,mode,value))
        cases+=1;locked_cases+=bool(locked)
        assert len(packets)==(0 if locked else 2)
    for channel,vid in itertools.product(range(3),(512,65535,0xffffffff)):
        writer(body,code,channel,vid);oversized+=1
    return dict(cases=cases,locked_cases=locked_cases,oversized_arguments_forwarded=oversized,
        channels=[0,1,2],vid_grid=[0,511],channel_2_mode_bytes=[0,1,2,255],
        formula='0.245 + VID * 0.005; channel 2 uses 0.49 + VID * 0.01 only in mode 1',
        electrical_range_verified=False,zero_code_electrical_semantics_verified=False,
        mirror_is_measurement=False,trace_sha256=trace.hexdigest())


def pm_export(body,code):
    u=lambda a:struct.unpack_from('<I',body,a)[0]
    vm=Slice(body,code);vm.ram.update(enumerate(body))
    for i in range(140):vm.putf(ACC+4*i,0);vm.putf(u(0xb100+4*i),0)
    sums=[0.0]*3
    for sample in range(4):
        for c in range(3):
            source=STATE+0x44+24*c
            produced,_=writer(body,code,c,64+8*sample+16*c,mode=1)
            value=produced.getf(source);vm.putf(source,value);sums[c]=f32(sums[c]+value)
        vm.run(0x2cc6e,0x2cc91)
    vm.r['a5']=ACC;vm.f['f0']=.25
    vm.run(0x2ce89,0x2ce9a)
    vm.r.update(a12=0x15b40,a13=301);vm.run(0x2d13a,0x2d14e)
    fields=[]
    for c in range(3):
        index=48+5*c;source=STATE+0x44+24*c;ptr=u(0xb340+4*index)
        assert ptr==ACC+4*(40+5*c) and u(0xb100+ptr-ACC)==source
        assert vm.getf(0x15b40+4*index)==f32(sums[c]*.25)
        fields.append(dict(index=index,channel=c,source=hex(source),accumulator=hex(ptr),unit='V'))
    return dict(samples=4,fields=fields,additional_scaling=False,reciprocal_fixture=.25,
        source='software command mirror',averaged=True,physical_rail_identity_verified=False,
        independent_setting_readback=False,freshness_verified=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('smu','study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();body=a.smu.read_bytes();assert hashlib.sha256(body).hexdigest()==SHA
    study=json.loads(a.study.read_text());assert study['source_sha256']==SHA
    code={int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
    for address,instruction in code.items():
        raw=bytes.fromhex(instruction['bytes']);assert body[address:address+len(raw)]==raw
    report=dict(schema=1,source_sha256=SHA,hardware_execution=False,
        overrides=overrides(body,code),writers=command_mirrors(body,code),pm=pm_export(body,code),
        physical_second_rail_identified=False,independent_device_readback_verified=False,
        runtime_write_enabled=False,peripheral_effects_verified=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__=='__main__':main()
