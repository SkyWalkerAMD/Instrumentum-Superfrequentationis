#!/usr/bin/env python3
"""Byte-checked Granite manual-OC state transitions, with transport fixtures.

The capability getter verifies the local firmware mode byte only. No clock,
voltage, MP5 application, feature policy or electrical behavior is measured.
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
Slice = H['Slice']
OC, FEATURES = 0xae68, 0x68c8


def read_caps(body, code, vm, mask):
    child = Slice(body, code); child.ram = vm.ram; child.r['a2'] = 0x1234
    result, responses = [], []
    def fuse(v):
        assert v.r['a11'] == 1 and v.r['a10'] in S['FUSE_IDS']
        v.r['a10'] = (mask >> S['FUSE_IDS'].index(v.r['a10'])) & 1
    def output(v):
        assert v.r['a10'] == 0x1234; result.append(v.r['a11'])
    def response(v):
        assert v.r['a10'] == 0x1234; responses.append(v.r['a11'])
    child.run(0x1f36c, None, hooks={0x1e22c: fuse, 0x1580: output, 0x14ec: response})
    assert len(result) == 1 and responses == [1]
    return result[0]


def run_mode(body, code, vm, enabled, argument, mask):
    events = []; permission_results = []; seen = set()
    def input_word(v):
        assert v.r['a10'] == 0x1234; v.r['a10'] = argument
    def permission(v):
        assert v.r['a10'] == 0
        child = Slice(body, code); child.ram = v.ram; child.r['a2'] = 0
        def fuse(c):
            assert (c.r['a10'], c.r['a11']) == (0x2cd7, 1)
            c.r['a10'] = mask & 1
        child.run(0x2c910, None, hooks={0x1e22c: fuse})
        permission_results.append(child.r['a2']); v.r['a10'] = child.r['a2']
        seen.update(child.trace)
    def lock(v):
        assert v.r['a10'] == 0x10000; events.append(('lock',))
    def unlock(v):
        assert v.r['a10'] == 0x10000; events.append(('unlock',))
    def broadcast(v):
        assert v.r['a10'] == (0x5d if enabled else 0x5e)
        events.append(('broadcast_boundary', v.r['a10']))
    def policy(v):
        assert v.r['a10'] == 1; events.append(('feature_policy_boundary',))
    def register(v):
        assert (v.r['a10'], v.r['a11']) == (0x2f, 0x1eddc)
        events.append(('register_callback_boundary',))
    def unregister(v):
        assert v.r['a10'] == 0x2f; events.append(('unregister_callback_boundary',))
    def local(entry):
        def run(v):
            child = Slice(body, code); child.ram = v.ram
            child.run(entry, None, hooks={}); seen.update(child.trace)
            events.append(('local_state_helper', entry))
        return run
    def response(v):
        assert v.r['a10'] == 0x1234; events.append(('ack', v.r['a11']))
    vm.run(0x2a688 if enabled else 0x2a73c, None, hooks={0x15dc: input_word,
        0x2c910: permission, 0x3bb64: lock, 0x3bc38: unlock, 0x1d6fc: broadcast,
        0x1de0c: policy, 0x1dd8c: register, 0x1dd9c: unregister,
        0x267cc: local(0x267cc), 0x267b0: local(0x267b0), 0x14ec: response})
    seen.update(vm.trace)
    return events, permission_results, seen


def cases(body, code, enabled):
    trace = hashlib.sha256(); total = rejected = broadcasts = 0; seen = set()
    for platform, variant, permitted_fuse, suppressed, old, argument, features in itertools.product(
            (0, 1), (0, 1, 2), (0, 1), (0, 1), (0, 1, 255),
            (0, 0x1000000, 0xfeffffff), ((0, 0), (0xc0, 4), (0xa5a5a5a5, 0x5a5a5a5a))):
        vm = Slice(body, code); vm.r.update(a1=0x50000, a2=0x1234)
        vm.put(0x817e, platform, 1); vm.put(0x8170, variant, 1)
        vm.put(0x13548, suppressed, 1); vm.put(0x1351f, 0, 1); vm.put(OC, old, 1)
        initial = (*features, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000)
        for i, word in enumerate(initial): vm.put(FEATURES+4*i, word)
        vm.put(0x9e4f, 7, 1); vm.put(0x9e64, 0x12345678); vm.put(0xd0e7, 9, 1)
        mask = 0x7fe | permitted_fuse
        before_caps = read_caps(body, code, vm, mask)
        before_ram = vm.ram.copy()
        events, permissions, visited = run_mode(body, code, vm, enabled, argument, mask)
        permitted = bool((platform or variant == 1) and permitted_fuse and not suppressed)
        assert bool(before_caps & 1) == permitted
        expected = list(initial)
        expected_events = []
        accepted = not enabled or permitted
        if enabled and permitted:
            mask0 = 0 if old else initial[0] & 0x40
            mask1 = 0 if old else initial[1] & 4
            if not old:
                expected_events += [('lock',), ('broadcast_boundary', 0x5d), ('unlock',)]
                broadcasts += 1
            if argument & 0x1000000: mask0 |= initial[0] & 0x80
            expected[2] |= mask0; expected[3] |= mask1
            expected[6] |= mask0; expected[7] |= mask1
            if mask0 or mask1: expected_events.append(('feature_policy_boundary',))
            expected_events.append(('register_callback_boundary',))
            if variant == 1: expected_events.append(('local_state_helper', 0x267cc))
        elif not enabled and old:
            expected[4] |= initial[2]; expected[5] |= initial[3]
            expected[2] = expected[3] = 0
            expected_events += [('feature_policy_boundary',), ('lock',),
                ('broadcast_boundary', 0x5e), ('unlock',), ('unregister_callback_boundary',)]
            broadcasts += 1
            if variant == 1: expected_events.append(('local_state_helper', 0x267b0))
        expected_events.append(('ack', 1 if accepted else 0xfd))
        assert events == expected_events
        assert permissions == ([int(permitted)] if enabled else [])
        expected_mode = int(enabled) if accepted and (enabled or old) else old
        assert vm.get(OC, 1) == expected_mode
        assert [vm.get(FEATURES+4*i) for i in range(8)] == expected
        changed_variant = accepted and variant == 1 and (enabled or old)
        assert vm.get(0x9e4f, 1) == ((0 if enabled else 3) if changed_variant else 7)
        assert vm.get(0xd0e7, 1) == ((0 if enabled else 3) if changed_variant else 9)
        assert vm.get(0x9e64) == (0 if changed_variant and enabled else 0x12345678)
        after_caps = read_caps(body, code, vm, mask)
        assert bool(after_caps & 0x10000) == bool(expected_mode)
        assert (before_caps ^ after_caps) & ~0x10000 == 0
        if not accepted: assert vm.ram == before_ram
        seen.update(visited); rejected += not accepted; total += 1
        trace.update(struct.pack('<10I', platform, variant, permitted_fuse, suppressed,
            old, argument, *features, expected_mode, after_caps))
    expected_paths = {0x2a72f, 0x2a6a9, 0x2a6cb, 0x2a71b} if enabled else {0x2a748, 0x2a78c, 0x2a797}
    assert expected_paths <= seen
    return dict(cases=total, rejected=rejected, broadcast_boundary_cases=broadcasts,
        command='0x5d' if enabled else '0x5e', stored_mode_byte=hex(OC),
        readback_bit=16, enable_permission_bit=0,
        unrelated_capability_bits_unchanged=True, trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu', 'study', 'output'): p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args(); body = a.smu.read_bytes()
    code = T['instructions'](a.study, body, T['SMU_SHA'])
    for command, entry, flags in ((0x5d, 0x2a688, 0x101005), (0x5e, 0x2a73c, 0x111005)):
        assert struct.unpack_from('<II', body, 0x6fa4+8*command) == (entry, flags)
    report = dict(schema=1, source_sha256=T['SMU_SHA'], hardware_execution=False,
        enable=cases(body, code, True), disable=cases(body, code, False),
        application_argument_zero_only=True, enable_argument_bit24_changes_feature_mask=True,
        disable_does_not_require_enable_capability=True, local_firmware_state_readback=True,
        cross_processor_transport_executed=False, hardware_effect_measured=False,
        target_hardware_accepted=False, electrical_limits_verified=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
