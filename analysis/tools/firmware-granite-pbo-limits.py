#!/usr/bin/env python3
"""Byte-checked Granite 98.84.0 PPT/TDC/EDC/Tctl setter experiments.

Synthetic RAM, fuse and mailbox replies only. Locks, hardware policy refresh and
reciprocal results are explicit boundaries. ACK is not setting readback and no
physical power/current/temperature or electrical limit is measured here.
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
S = runpy.run_path(str(HERE/'firmware-granite-pbo-scalar.py'))
Slice, f32 = H['Slice'], H['f32']
bits = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]
ARGUMENTS = (0, 1, 999, 1000, 1001, 123456, 500000, 1999999,
             2000000, 2000001, 16777217, 0xffffffff)


def capability(body, code):
    trace = hashlib.sha256(); cases = 0
    for platform, variant, mask, manual in itertools.product(
            (0, 1, 255), (0, 1, 2), (0, 2, 0x7fd, 0x7ff), (0, 1, 255)):
        vm = Slice(body, code); vm.r.update(a1=0x50000, a2=0x1234)
        vm.put(0x817e, platform, 1); vm.put(0x8170, variant, 1)
        vm.put(0x13548, 0, 1); vm.put(0x1351f, 0, 1); vm.put(0xae68, manual, 1)
        child = Slice(body, code); child.ram = vm.ram; child.r['a2'] = 1
        def permission_fuse(v):
            assert (v.r['a10'], v.r['a11']) == (0x2cd9, 1)
            v.r['a10'] = (mask >> 1) & 1
        child.run(0x2c910, None, hooks={0x1e22c: permission_fuse})
        results, replies = [], []
        def fuse(v):
            assert v.r['a11'] == 1 and v.r['a10'] in S['FUSE_IDS']
            v.r['a10'] = (mask >> S['FUSE_IDS'].index(v.r['a10'])) & 1
        def result(v):
            assert v.r['a10'] == 0x1234; results.append(v.r['a11'])
        def response(v):
            assert v.r['a10'] == 0x1234; replies.append(v.r['a11'])
        vm.run(0x1f36c, None, hooks={0x1e22c: fuse, 0x1580: result, 0x14ec: response})
        expected = mask if platform or variant == 1 else mask & 32
        expected |= 0x10000 if manual else 0
        assert results == [expected] and replies == [1]
        assert bool(child.r['a2']) == bool(expected & 2)
        trace.update(struct.pack('<6I', platform, variant, mask, manual, child.r['a2'], expected))
        cases += 1
    return dict(cases=cases, capability_mask=2, fuse_id='0x2cd9',
        manual_oc_mask=65536, permission_helper_argument=1,
        permission_matches_query_bit=True, trace_sha256=trace.hexdigest())


def execute(body, code, vm, wrapper, helper, argument):
    events = []; seen = set()
    def input_word(v):
        assert v.r['a10'] == 0x1234; v.r['a10'] = argument
    def response(v):
        assert v.r['a10'] == 0x1234; events.append(('ack', v.r['a11']))
    def consume(v):
        address, value = v.r['a10'], v.f['f0']
        prior = v.getf(address+12)
        child = Slice(body, code); child.ram = v.ram
        child.r.update(a1=0x4fe00, a2=address); child.f['f0'] = value
        def reciprocal(c):
            assert c.f['f0'] > 0; c.f['f0'] = f32(1/c.f['f0'])
        child.run(0x1c928, None, hooks={0x3f870: reciprocal})
        assert child.getf(address) == value
        assert child.getf(address+12) == min(prior, value)
        assert child.getf(address+4) == (f32(1/value) if value > 0 else 1.)
        seen.update(child.trace); events.append(('consume', address, value))
    def lock(v): events.append(('lock', v.r['a10']))
    def unlock(v): events.append(('unlock', v.r['a10']))
    def refresh(v): events.append(('policy_refresh_boundary',))
    def apply_helper(v):
        child = Slice(body, code); child.ram = v.ram
        child.r.update(a1=0x4ff00, a2=v.r['a10']); child.f = v.f.copy()
        child.run(helper, None, hooks={0x1c928: consume, 0x3bb64: lock,
            0x3bc38: unlock, 0x26208: refresh})
        seen.update(child.trace)
    vm.run(wrapper, None, hooks={0x15dc: input_word, helper: apply_helper, 0x14ec: response})
    seen.update(vm.trace)
    assert events[-1] == ('ack', 1)
    return events, seen


def base(body, code, variant, gate, sentinel):
    vm = Slice(body, code); vm.r.update(a1=0x50000, a2=0x1234)
    vm.put(0x8170, variant, 1); vm.put(0x8197, gate, 1)
    vm.put(0x3210064, 0x80000000 if sentinel else 0)
    return vm


def ppt(body, code):
    cases = clamped = 0; trace = hashlib.sha256(); seen = set()
    for arg, variant, gate, sentinel, override, cap, baseline, coefficient in itertools.product(
            ARGUMENTS, (0, 1, 2), (0, 1), (False, True), (0, 1),
            (0., 100., 500.), (50., 250.), (.5, 1., 2.)):
        vm = base(body, code, variant, gate, sentinel)
        vm.put(0x139b7, override, 1)
        vm.putf(0x9e24, cap); vm.putf(0x9e18, baseline)
        vm.putf(0x8b20, coefficient); vm.putf(0xd188, -99)
        vm.putf(0x9e64, 7.); vm.put(0x9e49, 0, 1)
        events, visited = execute(body, code, vm, 0x26614, 0x26578, arg)
        requested = f32(f32(arg)*f32(.001))
        selected = min(requested, cap) if gate and not sentinel else requested
        if override: selected = requested
        assert vm.getf(0xd188) == selected
        if variant != 1: selected = min(selected, baseline)
        product = f32(selected*coefficient)
        assert vm.getf(0x9e30) == selected and vm.get(0x9e49, 1) == 1
        assert events == [('lock', 0x20), ('consume', 0x9e58, product), ('unlock', 0x20), ('ack', 1)]
        clamped += selected != requested; cases += 1; seen.update(visited)
        trace.update(struct.pack('<9I', arg, variant, gate, sentinel, override,
            bits(cap), bits(baseline), bits(coefficient), bits(selected)))
    assert {0x26599, 0x26608, 0x265d7, 0x265e0} <= seen
    return dict(cases=cases, clamped_cases=clamped, command='0x56',
        wrapper='0x26614', helper='0x26578', stored_policy='0x9e30',
        scaled_consumer='0x9e58', override_byte='0x139b7',
        trace_sha256=trace.hexdigest())


def currents(body, code, kind):
    cases = clamped = refreshes = 0; trace = hashlib.sha256(); seen = set()
    tdc = kind == 'tdc'
    cap_address, state, flag = (0x9f0c, 0x9f20, 0x9f54) if tdc else (0xa140, 0xa13c, 0xa14d)
    wrapper, helper, command = (0x26928, 0x268cc, 0x57) if tdc else (0x27158, 0x270e8, 0x58)
    for arg, variant, gate, sentinel, cap in itertools.product(
            ARGUMENTS, (0, 1, 2), (0, 1), (False, True), (0., 50., 250., 2000.)):
        vm = base(body, code, variant, gate, sentinel)
        vm.putf(cap_address, cap); vm.putf(state, -99); vm.putf(state+12, 7.)
        vm.put(flag, 0, 1)
        events, visited = execute(body, code, vm, wrapper, helper, arg)
        requested = f32(f32(arg)*f32(.001))
        selected = min(requested, cap) if variant != 1 or gate and not sentinel else requested
        assert vm.getf(state) == selected and vm.get(flag, 1) == 1
        if tdc:
            expected = [('consume', state, selected), ('ack', 1)]
        elif variant == 1:
            expected = [('lock', 0x10000), ('policy_refresh_boundary',), ('unlock', 0x10000), ('ack', 1)]
            refreshes += 1
        else:
            expected = [('ack', 1)]
        assert events == expected
        cases += 1; clamped += selected != requested; seen.update(visited)
        trace.update(struct.pack('<6I', arg, variant, gate, sentinel, bits(cap), bits(selected)))
    branches = {0x268f0, 0x26904, 0x26910} if tdc else {0x27118, 0x27129, 0x27144}
    assert branches <= seen
    return dict(cases=cases, clamped_cases=clamped, command=hex(command),
        wrapper=hex(wrapper), helper=hex(helper), stored_policy=hex(state),
        policy_refresh_boundary_cases=refreshes, trace_sha256=trace.hexdigest())


def temperature(body, code):
    trace = hashlib.sha256(); cases = clamped = 0
    for arg, cap, prior in itertools.product(
            (0, 1, 89, 90, 91, 94, 95, 96, 99, 100, 101, 127, 128, 16777217, 0xffffffff),
            (0., 50., 90., 95., 100., 127.), (0., 75., 200.)):
        vm = base(body, code, 1, 1, False)
        vm.putf(0x9f58, cap); vm.putf(0x9f90, prior)
        events, _ = execute(body, code, vm, 0x26c7c, 0x26c60, arg)
        selected = min(f32(arg), cap)
        assert vm.getf(0x9f84) == selected
        assert events == [('consume', 0x9f84, selected), ('ack', 1)]
        clamped += selected != f32(arg); cases += 1
        trace.update(struct.pack('<4I', arg, bits(cap), bits(prior), bits(selected)))
    return dict(cases=cases, clamped_cases=clamped, command='0x59',
        wrapper='0x26c7c', helper='0x26c60', stored_policy='0x9f84',
        encoding='uint32 to binary32, no input scaling', trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu', 'study', 'output'): p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args(); body = a.smu.read_bytes()
    code = T['instructions'](a.study, body, T['SMU_SHA'])
    for command, entry in ((0x56, 0x26614), (0x57, 0x26928), (0x58, 0x27158), (0x59, 0x26c7c)):
        assert struct.unpack_from('<II', body, 0x6fa4+8*command) == (entry, 5)
    assert struct.unpack_from('<I', body, 0x11420)[0] == bits(.001)
    report = dict(schema=1, source_sha256=T['SMU_SHA'], hardware_execution=False,
        capability=capability(body, code), ppt=ppt(body, code), tdc=currents(body, code, 'tdc'),
        edc=currents(body, code, 'edc'), temperature=temperature(body, code),
        physical_unit_crosscheck='Pinned ZenStates Zen5Settings inherits Zen4Settings / TYPE_CPU4',
        power_current_encoding='uint32 to binary32, then binary32 0.001 multiply',
        reciprocal_result_is_fixture=True, all_acknowledgements_success=True,
        setters_check_capability_or_manual_oc=False, app_must_gate_full_capability_and_manual_oc=True,
        setting_readback_verified=False, hardware_effect_measured=False,
        target_hardware_accepted=False, electrical_limits_verified=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
