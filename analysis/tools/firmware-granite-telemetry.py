#!/usr/bin/env python3
"""Byte-checked SVI telemetry -> common PM experiments; no hardware execution.

Polling and controller replies are fixtures, not a peripheral model. The units
come from the pinned converter and scale initializer, not numerical plausibility.
"""
import argparse
import hashlib
import itertools
import json
import runpy
import struct
from pathlib import Path

helpers = runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))
Slice, f32 = helpers['Slice'], helpers['f32']
SHA = '5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596'
RAW, CONVERTED, CONFIG, ACC = 0xa668, 0xa624, 0xc598, 0x15320


def current_scales(body, code):
    assert code[0x32ee9]['text'] == 'movi.n a11,0x9'
    assert code[0x32eeb]['text'] == 'call8 0x00032bf8'
    assert struct.unpack_from('<I', body, 0x11fc0)[0] == 0xa620
    assert code[0x28d47]['text'] == 'mov.n a3,a6'
    assert code[0x28d54]['text'] == 'call8 0x00032ee4'
    assert code[0x28d57]['text'] == 'ssip f0,a3,0x18'
    values = []
    for reply in range(256):
        vm = Slice(body, code); vm.r['a10'] = reply
        mode = (reply >> 3) & 7
        vm.run(0x32eee, 0x32f09 if mode else 0x32f0e)
        expected = (1 << (mode-1))/32 if mode else 0
        assert vm.f['f0'] == expected
        values.append(expected)
        # Replay the initializer's actual postincrement store and loop. The
        # helper call is the explicit boundary; each register reply is a fixture.
        for channel in range(3):
            vm.r.update(a3=0xa620+24*channel, a2=channel)
            vm.run(0x28d57, 0x28d54 if channel < 2 else 0x28d5f)
            assert vm.getf(0xa620+24*channel) == expected
    return dict(register='0x09', selector_bits='5:3', replies=256,
                initializer_stores=768, mode_0_scale=0,
                nonzero_scale='2**(mode-1) / 32 A per code', values=values[::8][:8])


class ReplySlice(Slice):
    """Only the readout window is scripted; no polling/hardware timing claim."""
    def __init__(self, body, code, replies):
        super().__init__(body, code)
        u = lambda a: struct.unpack_from('<I', body, a)[0]
        # Config bases are copied from the reference image. Initializer assigns
        # addresses 1/2/3; runtime board configuration is not established.
        self.ports = set()
        for channel in range(3):
            config = CONFIG+12*channel
            self.put(config+0x14, u(config+0x14))
            self.put(config+0x1c, channel+1, 1)
            self.ports.add(((u(config+0x14)+u(0x126fc)) & u(0x11430))+u(0x11438)+0x200)
        self.replies, self.requests = iter(replies), []

    def get(self, address, size=4):
        if address in self.ports:
            assert size == 4
            return next(self.replies)
        return super().get(address, size)

    def put(self, address, value, size=4):
        if hasattr(self, 'ports') and address in self.ports:
            assert size == 4
            self.requests.append((address, value))
        else:
            super().put(address, value, size)


def retained_replies(body, code):
    checked = retained = 0
    for mask, high in itertools.product(range(512), (0, 0xf80003ff)):
        values = [101+37*i for i in range(9)]
        replies = [high | (v << 10) | (((mask >> i) & 1) << 26) for i,v in enumerate(values)]
        vm = ReplySlice(body, code, replies); vm.r['a2'] = RAW
        for channel in range(3):
            for kind in range(3): vm.put(RAW+16*channel+4*kind, 700+channel*10+kind)
            vm.put(RAW+16*channel+12, 0xffffffff)
        vm.run(0x32e88, 0x32edf)
        assert len(vm.requests) == 9
        assert [v for _,v in vm.requests] == [channel+1 | kind<<6 for channel in range(3) for kind in range(3)]
        for channel in range(3):
            invalid = 0
            for kind in range(3):
                i=3*channel+kind; valid=(mask >> i) & 1
                expected=values[i] if valid else 700+channel*10+kind
                assert vm.get(RAW+16*channel+4*kind) == expected
                invalid += not valid; retained += not valid; checked += 1
            assert vm.get(RAW+16*channel+12) == (0xffffffff+invalid) & 0xffffffff
    return dict(validity_masks=512, cases=1024, payloads=checked, retained_payloads=retained,
                per_channel_error_counter_wrap=True, payload_bits='25:10', valid_bit=26,
                request_kinds=[0,1,2], polling_executed=False, peripheral_replies_fixture=True)


def convert(vm, mode, raws, scales):
    vm.put(CONFIG+0x36, mode, 1)
    for channel in range(3):
        for kind, value in enumerate(raws[channel]): vm.put(RAW+16*channel+4*kind, value)
        vm.putf(CONVERTED-4+24*channel, scales[channel])
    vm.run(0x27e41, 0x27ed0)
    expected=[]
    for channel in range(3):
        current, voltage, temperature = raws[channel]
        v=f32(f32(voltage)*f32(.01 if channel==2 and mode==1 else .005))
        a=max(0, f32(f32(current)*scales[channel]))
        row=[v,a,f32(v*a),float((temperature & 255)-40)]
        assert [vm.getf(CONVERTED+24*channel+4*i) for i in range(4)] == row
        assert vm.get(CONVERTED+24*channel+16,1) == (0 if temperature & 512 else 1)
        expected.append(row)
    return expected


def conversion_cases(body, code):
    checked=0
    for mode, scale_mode, value, flags in itertools.product((0,1,2,255),range(8),range(256),(0,512)):
        scale=(1 << (scale_mode-1))/32 if scale_mode else 0
        scales=[scale,scale*2,scale/2]
        raws=[(value*257, value*257, value|flags) for _ in range(3)]
        convert(Slice(body,code),mode,raws,scales); checked += 3
    # The consumer clamps a negative coefficient product instead of changing
    # the voltage/temperature. This tests the slice, not a valid hardware mode.
    convert(Slice(body,code),0,[(1,250,100)]*3,[-1,-2,-4])
    return dict(channel_cases=checked, negative_coefficient_cases=3,
                temperature_codes=256, voltage_and_current_codes=256,
                input_domain='uint16 fixture grid (0,257,...,65535); not claimed to be valid hardware range',
                temperature='low_byte - 40 deg C', voltage='code * 0.005 V; channel 2 mode 1 uses 0.01 V',
                current='max(0, code * initialized scale) A', power='converted voltage * converted current W',
                temperature_status='bit 9 clear; not propagated to PM common fields')


def common_pm(body, code):
    u=lambda a:struct.unpack_from('<I',body,a)[0]
    assert u(0x1228c)==0xb100 and u(0x122cc)==0xb340 and u(0x122b0)==0x15b40
    vm=Slice(body,code)
    # Seed RAM, preserving both image pointer tables. Peripheral responses and
    # unrelated PM sources below are synthetic; no installed firmware implied.
    vm.ram.update(enumerate(body))
    for i in range(140): vm.putf(ACC+4*i,0); vm.putf(u(0xb100+4*i),0)
    expected=[[0.0]*4 for _ in range(3)]
    for sample in range(4):
        raws=[(80+sample*4+c*20, 200+sample*8+c*20, 90+sample+c*10) for c in range(3)]
        values=convert(vm,1,raws,[.125,.25,.5])
        for c in range(3):
            for k in range(4): expected[c][k]=f32(expected[c][k]+values[c][k])
        vm.run(0x2cc6e,0x2cc91)
    vm.r['a5']=ACC; vm.f['f0']=.25 # explicit exact reciprocal fixture
    vm.run(0x2ce89,0x2ce9a)
    # Skip unrelated direct fields and normalization ranges. These 12 fields
    # lie in the first 140 accumulators and receive no later scaling there.
    vm.r['a12']=0x15b40; vm.r['a13']=301
    vm.run(0x2d13a,0x2d14e)
    fields=[]
    for c in range(3):
        for k,unit in enumerate(('V','A','W','deg C')):
            index=49+5*c+k; ptr=u(0xb340+4*index); source=CONVERTED+24*c+4*k
            assert ptr==ACC+4*(41+5*c+k) and u(0xb100+ptr-ACC)==source
            assert vm.getf(0x15b40+4*index)==f32(expected[c][k]*.25)
            fields.append(dict(index=index,channel=c,quantity=('voltage','current','power','temperature')[k],
                               unit=unit,source=hex(source),accumulator=hex(ptr)))
    return dict(samples=4,accumulator_words=140,output_pointer_entries=301,fields=fields,
                reciprocal_fixture=.25,additional_scaling=False,physical_rail_identity_verified=False,
                freshness_verified=False,independent_setting_readback=False)


def main():
    p=argparse.ArgumentParser()
    for name in ('smu','study','output'): p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args(); body=args.smu.read_bytes()
    assert hashlib.sha256(body).hexdigest()==SHA
    study=json.loads(args.study.read_text(encoding='utf-8')); assert study['source_sha256']==SHA
    code={int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
    for address,i in code.items():
        raw=bytes.fromhex(i['bytes']); assert body[address:address+len(raw)]==raw
    report=dict(schema=1,source_sha256=SHA,hardware_execution=False,
                current_scale=current_scales(body,code),replies=retained_replies(body,code),
                conversion=conversion_cases(body,code),pm=common_pm(body,code),
                runtime_write_enabled=False,physical_channel_mapping_verified=False)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Verified',report['conversion']['channel_cases'],'synthetic channel conversions and 12 common PM fields')


if __name__=='__main__': main()
