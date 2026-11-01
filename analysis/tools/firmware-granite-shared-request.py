#!/usr/bin/env python3
"""Trace the shared request's quantization, channel-0 route and PM mirror.

Only pinned instructions operate on synthetic memory. Scripted peripheral
completion is not a voltage measurement, a device getter or hardware success.
CEIL.S covers finite signed-representable fixtures, not FPU exception behavior.
"""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
H = runpy.run_path(str(HERE / 'firmware-granite-semantics.py'))
R = runpy.run_path(str(HERE / 'firmware-granite-registers.py'))
T = runpy.run_path(str(HERE / 'firmware-granite-topology.py'))
F = runpy.run_path(str(HERE / 'firmware-granite-rails.py'))
Slice, f32, SHA = H['Slice'], H['f32'], R['SHA']
STATE, ACC = 0x8c30, 0x15320


def request(body, code, enabled, entries, last, floor, wide, extras=(0, 0, 0, 0)):
    vm = Slice(body, code)
    vm.r.update(a1=0x50000)
    vm.put(0xae68, enabled, 1); vm.put(0xae67, len(entries), 1)
    vm.put(0xa19c, last, 1); vm.put(0xae11, wide, 1)
    vm.putf(0xb9a8, floor)
    for i, value in enumerate(entries): vm.put(0xaf28+2*i, value, 2)
    for address, value in zip((STATE+0x48, STATE+0x4c, 0xaed8, STATE+0xc), extras):
        vm.putf(address, value)
    vm.run(0x2100c, None, hooks={})
    # Run the caller's actual copy after the separately executed callee.
    vm.run(0x210ca, 0x210d3)
    assert vm.getf(STATE) == vm.getf(STATE+4)
    base, step = f32(.245), f32(.005)
    if enabled:
        chosen = entries[-1 if last else 0]
        expected = max(f32(floor), f32(base + chosen*step))
    else:
        a, b, minimum, clamp = map(f32, extras)
        expected = max(f32(f32(a+b)+max(f32(floor), minimum)), clamp)
    quantized = max(0, math.ceil(f32(f32(-.00001)+f32(expected-base)*f32(200))))
    if enabled:
        encoded = quantized if wide else min(quantized, 255)
    else:
        encoded = quantized
        expected = f32(base+quantized*step)
    assert vm.getf(STATE) == expected
    assert vm.get(STATE+0x3e, 2) == (encoded & 0xffff)
    return vm, encoded


def apply_request(body, code, vm, locked):
    vm.put(STATE+0x40, locked, 2)
    vm.put(STATE+0x3c, 0x1234, 2); vm.putf(STATE+0x44, -999)
    requested = vm.get(STATE+0x3e, 2)
    packets = []

    def packet(caller):
        assert (caller.r['a10'], caller.r['a11']) == (0, requested)
        child = R['VoltageSlice'](body, code, 0, requested, 1)
        child.run(0x32d6c, 0x32dcb)
        assert not child.responses and child.writes == child.expected_writes
        packets.extend(child.writes)

    vm.run(0x20fb0, None, hooks={0x32d6c: packet})
    if locked:
        assert not packets and vm.get(STATE+0x3c, 2) == 0x1234
        assert vm.getf(STATE+0x44) == -999
    else:
        assert len(packets) == 2 and vm.get(STATE+0x3c, 2) == requested
        assert vm.getf(STATE+0x44) == f32(f32(.245)+requested*f32(.005))
    return packets


def quantization(body, code):
    trace = hashlib.sha256(); cases = clamped = locked_cases = different = 0
    # Accepted 61h storage flows into the selected first/last entry, the full
    # CEIL/clamp path, the shared software mirror and the channel-0 writer.
    for value, selector, last, cap, wide, locked in itertools.product(
            range(512), (0,1), (0,1), (0,80,255), (0,1), (0,1)):
        status, writes = F['replay'](body, code, selector << 9 | value, 1, 2, cap)
        assert status == 1 and len(writes) == 1
        entries = [11, 133]
        address, stored = next(iter(writes.items()))
        assert address == 0xaf28+2*selector
        entries[selector] = stored
        vm, encoded = request(body, code, 1, entries, last, .5, wide)
        unclamped = max(0, math.ceil(f32(f32(-.00001)+f32(vm.getf(STATE)-f32(.245))*200)))
        clamped += not wide and unclamped > 255
        packets = apply_request(body, code, vm, locked)
        different += vm.getf(STATE+4) != vm.getf(STATE+0x44)
        locked_cases += bool(locked)
        trace.update(struct.pack('<8I2f', value, selector, last, cap, wide, locked,
                                 encoded, len(packets), vm.getf(STATE+4), vm.getf(STATE+0x44)))
        cases += 1
    # The disabled path has distinct rounding and ignores the configured width
    # flag: the stored float is reconstructed from its un-clamped encoded code.
    disabled = 0
    for floor, a, b, minimum, clamp, wide in itertools.product(
            (-1, .245, .5, 1.52, 3), (0, .125), (0, .25), (0, .75), (0, 2), (0,1)):
        vm, encoded = request(body, code, 0, (), 0, floor, wide, (a,b,minimum,clamp))
        trace.update(struct.pack('<6fI', floor, a, b, minimum, clamp, vm.getf(STATE), encoded))
        disabled += 1
    # IEEE adjacent values around every ordinary VID boundary. The epsilon in
    # the firmware must be retained rather than replacing it with round().
    boundaries = 0
    for value in range(512):
        voltage = f32(f32(.245)+value*f32(.005))
        bits = struct.unpack('<I', struct.pack('<f', voltage))[0]
        for adjacent in (bits-1, bits, bits+1):
            floor = struct.unpack('<f', struct.pack('<I', adjacent))[0]
            for enabled, wide in itertools.product((0,1),(0,1)):
                vm, encoded = request(body, code, enabled, (0,0), 0, floor, wide)
                trace.update(struct.pack('<4I', adjacent, enabled, wide, encoded))
                boundaries += 1
    return dict(cases=cases, clamped_cases=clamped, locked_cases=locked_cases,
        request_differs_from_command_mirror_cases=different, disabled_cases=disabled,
        adjacent_boundary_cases=boundaries, ceil_fixture_domain='finite signed int32',
        enabled_float_is_prequantization=True, disabled_float_is_code_reconstruction=True,
        enabled_8bit_cap_only_when_byte_ae11_zero=True, disabled_path_ignores_8bit_cap=True,
        selected_entry='first if byte[a19c] == 0, else last configured entry',
        final_writer_channel=0, device_readback=False, physical_rail_identity_verified=False,
        trace_sha256=trace.hexdigest())


def export(body, code):
    u = lambda a: struct.unpack_from('<I', body, a)[0]
    assert u(0xb340+19*4) == ACC+11*4 and u(0xb100+11*4) == STATE+4
    assert u(0xb340+48*4) == ACC+40*4 and u(0xb100+40*4) == STATE+0x44
    vm = Slice(body, code); vm.ram.update(enumerate(body))
    for i in range(140): vm.putf(ACC+4*i,0); vm.putf(u(0xb100+4*i),0)
    sums = [0.0, 0.0]; trace = hashlib.sha256()
    for i, (value, wide, locked) in enumerate(((400,0,0),(100,1,0),(300,1,1),(250,0,0))):
        child, _ = request(body,code,1,(0,value),1,.5,wide)
        apply_request(body,code,child,locked)
        for j, address in enumerate((STATE+4,STATE+0x44)):
            val = child.getf(address); vm.putf(address,val); sums[j] = f32(sums[j]+val)
            trace.update(struct.pack('<IIf',i,address,val))
        vm.run(0x2cc6e,0x2cc91)
    vm.r['a5']=ACC; vm.f['f0']=.25; vm.run(0x2ce89,0x2ce9a)
    vm.r.update(a12=0x15b40,a13=301); vm.run(0x2d13a,0x2d14e)
    expected = [f32(s*.25) for s in sums]
    assert [vm.getf(0x15b40+4*i) for i in (19,48)] == expected
    assert expected[0] != expected[1]
    return dict(samples=4, pm_indices=[19,48], unit='V', values=expected,
        field='Shared voltage request mirror (averaged)', source_address='0x8c34',
        source_space='smu_local', accumulator='0x1534c', additional_scaling=False,
        averaging_reciprocal_fixture=.25, setting_readback_verified=False,
        physical_measurement_verified=False, freshness_verified=False,
        trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('smu','study','output'): p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args(); body=a.smu.read_bytes()
    code=T['instructions'](a.study,body,SHA)
    assert code[0x210c7]['text']=='call8 0x0002100c'
    report=dict(schema=1,source_sha256=SHA,hardware_execution=False,
        quantization=quantization(body,code),export=export(body,code),
        new_common_units={'19':'V'},new_core_or_ccd_units={},
        independent_second_rail_verified=False,runtime_write_enabled=False,
        physical_or_os_core_mapping_verified=False,
        limitations=['Synthetic RAM and peripheral responses; no device effects or readback.',
                    'Finite in-range conversions only; no FPU exceptions or complete scheduler.',
                    'Public SVI3 scale establishes software volts, not physical rail identity.'])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__=='__main__': main()
