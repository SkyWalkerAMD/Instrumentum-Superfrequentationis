#!/usr/bin/env python3
"""Byte-checked Granite 98.84.0 scalar storage, gates and readback.

Mailbox/fuse/MMIO replies are fixtures. The reciprocal is an explicit rounded
result boundary. This establishes a software setting protocol, not measured
voltage, device effects, electrical limits or target-machine acceptance.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
H = runpy.run_path(str(HERE/'firmware-granite-semantics.py'))
T = runpy.run_path(str(HERE/'firmware-granite-topology.py'))
Slice, f32 = H['Slice'], H['f32']
STATE, STORED, LIMIT, OC = 0x9fb8, 0xa064, 0xa068, 0xae68
bits = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]


def initialized_limits(body, code):
    trace = hashlib.sha256()
    for mantissa, exponent in itertools.product(range(16), range(8)):
        vm = Slice(body, code); vm.r.update(a1=0x50000, a4=STATE)
        calls = []
        def fuse(v):
            key = v.r['a10'], v.r['a11']; calls.append(key)
            assert key in ((0x2c15, 3), (0x2c11, 4))
            v.r['a10'] = exponent if key[0] == 0x2c15 else mantissa
        # Begin at the final two fuse requests; unrelated coefficient setup
        # preceding this slice and downstream model setup are not executed.
        vm.r['a10'] = 0x2c15; vm.r['a11'] = 3
        vm.run(0x26de0, 0x26e1e, hooks={0x1e22c: fuse})
        expected = float(mantissa)
        for _ in range(exponent): expected = f32(expected*10)
        assert vm.getf(LIMIT) == expected
        assert calls == [(0x2c15, 3), (0x2c11, 4)]
        trace.update(struct.pack('<3I', mantissa, exponent, vm.get(LIMIT)))
    return dict(cases=128, limit_address=hex(LIMIT), mantissa_fuse='0x2c11/4 bits',
        exponent_fuse='0x2c15/3 bits', formula='binary32 mantissa, multiplied by ten exponent times',
        universal_maximum_ten=False, physical_safety_range_verified=False,
        trace_sha256=trace.hexdigest())


FUSE_IDS = (0x2cd7, 0x2cd9, 0x2cda, 0x2cdb, 0x2cd8, 0x2cdc,
            0x2cdd, 0x2cde, 0x2cdf, 0x3706, 0x3707)


def capability(body, code, vm, mask):
    child = Slice(body, code); child.ram = vm.ram; child.r['a2'] = 2
    def fuse(v):
        assert (v.r['a10'], v.r['a11']) == (0x2cda, 1)
        v.r['a10'] = (mask >> 2) & 1
    child.run(0x2c910, None, hooks={0x1e22c: fuse})
    return child.r['a2']


def capability_equivalence(body, code):
    trace = hashlib.sha256(); cases = 0
    for platform, variant, mask, manual in itertools.product((0, 1, 255), (0, 1, 2),
                                                            (0, 4, 0x7fb, 0x7ff), (0, 1, 255)):
        vm = Slice(body, code); vm.r.update(a1=0x50000, a2=0x1234)
        vm.put(0x817e, platform, 1); vm.put(0x8170, variant, 1)
        vm.put(0x13548, 0, 1); vm.put(0x1351f, 0, 1); vm.put(OC, manual, 1)
        permission = capability(body, code, vm, mask)
        results, replies = [], []
        def fuse(v):
            assert v.r['a11'] == 1 and v.r['a10'] in FUSE_IDS
            v.r['a10'] = (mask >> FUSE_IDS.index(v.r['a10'])) & 1
        def result(v):
            assert v.r['a10'] == 0x1234; results.append(v.r['a11'])
        def response(v):
            assert v.r['a10'] == 0x1234; replies.append(v.r['a11'])
        vm.run(0x1f36c, None, hooks={0x1e22c: fuse, 0x1580: result, 0x14ec: response})
        expected = mask if platform or variant == 1 else mask & 32
        expected |= 0x10000 if manual else 0
        assert results == [expected] and replies == [1]
        assert bool(permission) == bool(expected & 4)
        trace.update(struct.pack('<6I', platform, variant, mask, manual, permission, expected))
        cases += 1
    return dict(cases=cases, permission_argument=2, capability_bit=2, capability_mask=4,
        fuse_id='0x2cda', manual_oc_bit=16, manual_oc_mask=65536,
        permission_matches_query_bit=True, trace_sha256=trace.hexdigest())


def read_scalar(body, code, vm, manual):
    vm.put(OC, manual, 1)
    child = Slice(body, code); child.ram = vm.ram; child.r['a2'] = 0x1234
    results, replies = [], []
    def result(v):
        assert v.r['a10'] == 0x1234; results.append(v.r['a11'])
    def response(v):
        assert v.r['a10'] == 0x1234; replies.append(v.r['a11'])
    child.run(0x26fe0, None, hooks={0x1580: result, 0x14ec: response})
    assert len(results) == 1 and replies == [1]
    return results[0]


def getter_cases(body, code):
    raws = (0, 1, 0x007fffff, 0x00800000, 0x80000000, 0xbf800000,
            0x7f800000, 0xff800000, 0x7fc00001, 0x7f800001, 0xffffffff,
            *(bits(v) for v in (.5, 1, 2.5, 5, 10, 11)))
    trace = hashlib.sha256()
    for raw, manual in itertools.product(raws, (0, 1, 255)):
        vm = Slice(body, code); vm.put(STORED, raw)
        actual = read_scalar(body, code, vm, manual)
        assert actual == (0 if manual else raw) and vm.get(STORED) == raw
        trace.update(struct.pack('<3I', raw, manual, actual))
    return dict(cases=len(raws)*3, command='0x6d', handler='0x26fe0',
        stored_address=hex(STORED), encoding='raw binary32 bits',
        manual_oc_returns_zero_without_changing_storage=True,
        nonfinite_payload_bits_preserved=True, physical_measurement=False,
        trace_sha256=trace.hexdigest())


def setter_cases(body, code):
    trace = hashlib.sha256(); cases = forced = capped = mismatched = 0
    arguments = (0, 1, 99, 100, 101, 199, 250, 500, 700, 999, 1000, 1001,
                 16777217, 0xffffffff)
    # Six modes exercise absent platform permission, fuse denial, fuse grant,
    # and the two branches that bypass the permission/max clamp entirely.
    modes = ((0, 0, 0, 0, 0), (1, 0x80000000, 0, 0, 0),
             (1, 0, 0, 0, 4), (1, 0, 1, 0, 0),
             (1, 0, 1, 0, 4), (1, 0, 0, 1, 4))
    for argument, mode, cap, table_index, coefficient, prior in itertools.product(
            arguments, modes, (0., .5, 1., 2.5, 10., 15.), (0, 7, 15), (.25, 1., 2.), (0., 4.)):
        gate, mmio, platform, variant, mask = mode
        vm = Slice(body, code); vm.r.update(a1=0x50000, a2=0x1234)
        vm.put(0x8197, gate, 1); vm.put(0x3210064, mmio)
        vm.put(0x817e, platform, 1); vm.put(0x8170, variant, 1)
        vm.putf(STORED, -99); vm.putf(LIMIT, cap); vm.put(STATE+0xbc, table_index, 1)
        vm.putf(STATE+0x2c+4*table_index, coefficient); vm.putf(STATE+12, prior)
        events = []; permissions = []; reciprocals = []
        def argument_word(v):
            assert v.r['a10'] == 0x1234; v.r['a10'] = argument
        def permission(v):
            assert v.r['a10'] == 2
            p = capability(body, code, vm, mask); permissions.append(p); v.r['a10'] = p
        def consumer(v):
            assert v.r['a10'] == STATE
            child = Slice(body, code); child.ram = v.ram
            child.r['a2'] = STATE; child.f['f0'] = v.f['f0']
            def reciprocal(c):
                assert c.f['f0'] > 0
                reciprocals.append(c.f['f0']); c.f['f0'] = f32(1/c.f['f0'])
            child.run(0x1c928, None, hooks={0x3f870: reciprocal})
            events.append(('consumer', v.f['f0']))
        def respond(v):
            assert v.r['a10'] == 0x1234; events.append(('response', v.r['a11']))
        vm.run(0x26f78, None, hooks={0x15dc: argument_word, 0x2c910: permission,
            0x1c928: consumer, 0x14ec: respond})
        requested = f32(f32(argument)*f32(.01))
        expected = requested
        if gate and mmio != 0x80000000:
            permitted = bool((platform or variant == 1) and mask & 4)
            assert permissions == [int(permitted)]
            expected = min(requested, cap) if permitted else 1.
            forced += not permitted
            capped += permitted and expected != requested
        else:
            assert not permissions
        product = f32(coefficient*expected)
        assert vm.getf(STORED) == expected
        assert vm.getf(STATE) == product and vm.getf(STATE+12) == min(prior, product)
        assert vm.getf(STATE+4) == (f32(1/product) if product > 0 else 1.)
        assert reciprocals == ([product] if product > 0 else [])
        assert events == [('consumer', product), ('response', 1)]
        # Readback uses the stored float, not an assumption that ACK means the
        # requested multiplier survived the firmware's permission/cap logic.
        assert read_scalar(body, code, vm, 0) == bits(expected)
        assert read_scalar(body, code, vm, 1) == 0
        mismatched += expected != requested
        trace.update(struct.pack('<10I', argument, gate, mmio, platform, variant, mask,
            bits(cap), table_index, bits(coefficient), bits(expected)))
        cases += 1
    return dict(cases=cases, command='0x5b', handler='0x26f78',
        argument_scale='uint32 to binary32, then multiply by binary32 0.01',
        all_acknowledgements_success=True, forced_one_cases=forced, capped_cases=capped,
        accepted_but_readback_differs=mismatched, setting_readback_is_raw_binary32=True,
        consumer_uses_selected_coefficient=True, reciprocal_result_is_fixture=True,
        setter_has_no_manual_oc_rejection=True, app_must_gate_manual_oc=True,
        physical_measurement=False, trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu', 'study', 'output'): p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args(); body = a.smu.read_bytes()
    code = T['instructions'](a.study, body, T['SMU_SHA'])
    for command, entry in ((0x5b, 0x26f78), (0x6d, 0x26fe0), (0x6f, 0x1f36c)):
        assert struct.unpack_from('<I', body, 0x6fa4+8*command)[0] == entry
    report = dict(schema=1, source_sha256=T['SMU_SHA'], hardware_execution=False,
        initialized_limits=initialized_limits(body, code), capabilities=capability_equivalence(body, code),
        getter=getter_cases(body, code), setter=setter_cases(body, code),
        electrical_limits_verified=False, target_hardware_accepted=False,
        independent_rail_protocol_verified=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
