#!/usr/bin/env python3
"""Byte-checked firmware core-coordinate remapping on synthetic RAM.

Fuses and register replies are fixtures. The reference firmware's compact
command indexes are neither PM storage positions nor verified APIC/OS IDs.
No firmware installation, mailbox or device operation executes on the host.
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
Slice = H['Slice']
STATE, MAP, TARGETS, STACK = 0x8164, 0x8318, 0x6814, 0x50000


def encoded(smt, domain, target, subgroup, core, thread):
    return (((domain & 1) << 9) | ((target & 15) << 5) |
            ((subgroup & 1) << 4) | ((core & 7) << 1) | (thread & 1)) if smt else (
            ((domain & 1) << 8) | ((target & 15) << 4) |
            ((subgroup & 1) << 3) | (core & 7))


def encode(body, code, args, ram=None):
    vm = Slice(body, code)
    if ram is not None:
        vm.ram = ram
    vm.r['a1'] = 0x60000
    for i, value in enumerate(args, 2):
        vm.r['a'+str(i)] = value
    vm.run(0x2e1b4, None, hooks={})
    assert vm.r['a2'] == encoded(*args)
    return vm.r['a2']


def decode(body, code, smt, value, ram=None, destinations=None):
    vm = Slice(body, code)
    if ram is not None:
        vm.ram = ram
    destinations = destinations or (0x61000, 0x61004, 0x61008, 0x6100c)
    vm.r.update(a1=0x60000, a2=smt, a3=value)
    for i, address in enumerate(destinations, 4):
        vm.r['a'+str(i)] = address
    vm.run(0x2e17c, None, hooks={})
    result = [vm.get(p) for p in destinations]
    expected = [(value >> (9 if smt else 8)) & 1,
                (value >> (5 if smt else 4)) & 15,
                (value >> (1 if smt else 0)) & 7, value & 1 if smt else 0]
    assert result == expected
    return result


def codecs(body, code):
    trace = hashlib.sha256(); cases = 0
    for args in itertools.product((0, 1), (0, 1, 2, 255), (0, 1, 2, 15, 16, 255),
                                  (0, 1, 2, 255), (0, 1, 7, 8, 255), (0, 1, 2, 255)):
        value = encode(body, code, args)
        result = decode(body, code, args[0], value)
        # The decoder intentionally has no subgroup output.
        assert result == [args[1] & 1, args[2] & 15, args[4] & 7, args[5] & 1 if args[0] else 0]
        trace.update(struct.pack('<11I', *args, value, *result)); cases += 1
    return dict(cases=cases, encoder='0x2e1b4', decoder='0x2e17c',
                subgroup_not_returned_by_decoder=True, trace_sha256=trace.hexdigest())


def initialize(body, code, present, masks, smt, raw_high=0):
    vm = Slice(body, code); vm.r['a1'] = STACK
    # Explicit synthetic state, including deliberately supplied config flags.
    for address in range(STATE, STATE+0x300):
        vm.put(address, 0, 1)
    vm.put(0x81e4+0xfb, 1, 1)  # Avoid unrelated per-target feature-fuse read.
    for target in range(2):
        vm.put(TARGETS+0x14+4*target, 0x20000000+0x800000*target)
    register_requests = []; fuse_requests = []; encodings = []

    def fuse(v):
        pair = (v.r['a10'], v.r['a11']); fuse_requests.append(pair)
        assert pair in ((0x1df6, 2), (0x25f3, 1))
        v.r['a10'] = present if pair[0] == 0x1df6 else 0

    def target_fuse(v):
        assert v.r['a10'] in (0, 1) and (v.r['a11'], v.r['a12']) == (0x1ee0, 8)
        v.r['a10'] = 0

    def register(v):
        target, selector = v.r['a10'], v.r['a11']
        assert target in (0, 1) and selector in (0x203c003c, 0x203c0030)
        child = Slice(body, code); child.ram = v.ram
        child.r.update(a1=0x60000, a2=target, a3=selector)
        reply = (masks[target] | raw_high) if selector == 0x203c003c else ((not smt) << 10)
        def transport(c):
            address = 0x20000000+0x800000*target+(selector & 0x3fffff)
            assert (c.r['a10'], c.r['a11'], c.r['a12']) == (0, address, 2)
            register_requests.append((target, selector, address, reply))
            c.r['a10'] = reply
        child.run(0x1db30, None, hooks={0x2b70: transport})
        v.r['a10'] = child.r['a2']

    def pack(v):
        args = tuple(v.r['a'+str(i)] for i in range(10, 16))
        result = encode(body, code, args, v.ram)
        encodings.append((args, result)); v.r['a10'] = result

    # Stop before the final hardware read/modify/write loop at 0x1eb00.
    vm.run(0x1e8ec, 0x1eafc, budget=30000,
           hooks={0x1e22c: fuse, 0x1e280: target_fuse, 0x1db30: register, 0x2e1b4: pack})
    active = [target for target in range(2) if present & (1 << target) and masks[target]]
    core_list = [(t, core) for t in active for core in range(8) if masks[t] & (1 << core)]
    threads = 2 if smt else 1
    thread_list = [(t, core, thread) for t, core in core_list for thread in range(threads)]
    expected = [0xffff] * 64
    for ordinal, target in enumerate(active):
        cores = [core for core in range(8) if masks[target] & (1 << core)]
        for compact_core, core in enumerate(cores):
            for thread in range(threads):
                index = encoded(smt, 0, ordinal, 0, compact_core, thread)
                expected[index] = encoded(smt, 0, target, 0, core, thread)
    assert [vm.get(MAP+2*i, 2) for i in range(64)] == expected
    assert [(vm.get(STATE+0x7c+4*i, 1), vm.get(STATE+0x7e+4*i, 1))
            for i in range(len(core_list))] == core_list
    assert [(vm.get(STATE+0xbc+4*i, 1), vm.get(STATE+0xbe+4*i, 1), vm.get(STATE+0xbf+4*i, 1))
            for i in range(len(thread_list))] == thread_list
    assert vm.get(TARGETS, 2) == present
    assert vm.get(TARGETS+2, 2) == sum(1 << t for t in active)
    assert vm.get(STATE+0x2a, 2) == present.bit_count()
    assert vm.get(STATE+0x2c, 2) == len(active)
    assert vm.get(STATE+0xe, 1) == len(core_list)
    assert vm.get(STATE+0xf, 1) == len(thread_list)
    assert len(encodings) == len(thread_list)*2
    assert len([x for x in register_requests if x[1] == 0x203c0030]) == bool(active)
    assert fuse_requests == [(0x1df6, 2), (0x25f3, 1)]
    return vm, expected, dict(presence_mask=present, core_masks=masks, smt=bool(smt),
        active_mask=sum(1 << t for t in active), core_records=core_list,
        thread_records=thread_list, remap=expected, register_requests=register_requests)


def initialization_cases(body, code):
    grid = (0, 1, 2, 0x55, 0x80, 0x81, 0xfe, 0xff)
    cases = set(itertools.product((0, 1, 2, 3), grid, grid, (0, 1)))
    for mask in range(256):
        for smt in (0, 1):
            cases.add((3, mask, 0xa5, smt)); cases.add((3, 0xa5, mask, smt))
    trace = hashlib.sha256(); records = 0; entries = 0
    examples = []
    for present, first, second, smt in sorted(cases):
        vm, table, result = initialize(body, code, present, [first, second], smt)
        trace.update(json.dumps(result, sort_keys=True, separators=(',', ':')).encode())
        records += len(result['thread_records']); entries += sum(v != 0xffff for v in table)
        if (present, first, second, smt) in ((2, 0, 0x81, 1), (3, 0, 0xff, 1), (3, 0x81, 0xa5, 1), (3, 0xff, 0xff, 1)):
            examples.append(result)
    # Upper half of the register reply is discarded before mask processing.
    baseline = initialize(body, code, 3, [0x81, 0xa5], 1)[1]
    for upper in (0x00010000, 0x80000000, 0xffff0000):
        assert initialize(body, code, 3, [0x81, 0xa5], 1, upper)[1] == baseline
    return dict(cases=len(cases), upper_half_discard_cases=3,
        table_words_checked=len(cases)*64, thread_records_checked=records,
        populated_entries_checked=entries, present_targets_are_not_compacted_by_core_mask=True,
        command_targets_are_compacted_by_nonzero_core_mask=True,
        one_core_mask_bit_per_local_slot=True, examples=examples,
        trace_sha256=trace.hexdigest())


def consumers(body, code):
    trace = hashlib.sha256(); cases = routed = rejected = 0
    for present, masks, smt in ((2, [0, 0x81], 1), (3, [0, 0xff], 1),
                                (3, [0x81, 0xa5], 1), (3, [0x81, 0xa5], 0)):
        _, table, _ = initialize(body, code, present, masks, smt)
        for entry, index, gate, low in itertools.product((0x2a274, 0x2a314),
                tuple(range(64))+(64, 255, 65535), (0, 1), (0, 0xffff)):
            vm = Slice(body, code); vm.r.update(a1=STACK, a2=0x123)
            vm.put(STATE+0x4b, smt, 1); vm.put(0x8b38, gate, 1)
            for i, value in enumerate(table): vm.put(MAP+2*i, value, 2)
            request = (index << 16) | low if entry == 0x2a274 else index
            events = []
            def input_word(v):
                assert v.r['a10'] == 0x123; v.r['a10'] = request
            def unpack(v):
                decode(body, code, v.r['a10'], v.r['a11'], v.ram,
                       tuple(v.r['a'+str(i)] for i in range(12, 16)))
            def forward(v):
                event = tuple(v.r['a'+str(i)] for i in (10, 11, 12))
                events.append(('forward', *event)); v.r['a10'] = 0x87654321
            def reply(v):
                assert v.r['a10'] == 0x123; events.append(('status', v.r['a11']))
            def payload(v):
                assert v.r['a10'] == 0x123; events.append(('payload', v.r['a11']))
            vm.run(entry, None, hooks={0x15dc: input_word, 0x2e17c: unpack,
                0x1d488: forward, 0x14ec: reply, 0x1580: payload})
            ok = index < 64 and table[index] != 0xffff and bool(gate)
            if ok:
                _, target, core, _ = decode(body, code, smt, table[index])
                expected = [('forward', target, 0x3d, (core << 16) | low)] if entry == 0x2a274 else [
                    ('forward', target, 0x3e, core), ('payload', 0x87654321)]
                expected.append(('status', 1)); routed += 1
            else:
                expected = [('status', 255)]; rejected += 1
            assert events == expected, (entry, index, gate, events, expected)
            trace.update(json.dumps([entry, request, gate, smt, table, events], separators=(',', ':')).encode())
            cases += 1
    return dict(cases=cases, routed_cases=routed, rejected_cases=rejected,
        setter_entry='0x2a274', getter_body_entry='0x2a314',
        setter_selector='upper 16 input bits', getter_selector='entire input word',
        maximum_index=63, absent_entry=65535,
        destination_core_does_not_use_thread_bit=True,
        gate_address='0x8b38', public_getter_route_verified=False,
        target_transport_and_responses_are_fixtures=True,
        trace_sha256=trace.hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('smu', 'study', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args(); body = args.smu.read_bytes()
    code = T['instructions'](args.study, body, T['SMU_SHA'])
    u32 = lambda address: struct.unpack_from('<I', body, address)[0]
    assert [u32(TARGETS+0x14+4*t) for t in range(2)] == [0x20000000, 0x20800000]
    assert (u32(0x11620), u32(0x11624)) == (0x203c003c, 0x203c0030)
    assert u32(0x6d54) == 0x2a274
    report = dict(schema=1, source_sha256=T['SMU_SHA'], hardware_execution=False,
        tuple_encoding=codecs(body, code), initialization=initialization_cases(body, code),
        consumers=consumers(body, code),
        firmware_target_bases=['0x20000000', '0x20800000'],
        reference_presence_fuse_bit_address='0x1df6', core_mask_selector='0x203c003c',
        smt_selector='0x203c0030', smt_disable_bit=10,
        command_remap_address='0x8318', command_remap_words=64,
        source_core_record_address='0x81e0', source_core_record_stride=4,
        source_thread_record_address='0x8220', source_thread_record_stride=4,
        reference_addresses_are_host_addresses=False,
        physical_ccd_mapping_verified=False, os_cpu_mapping_verified=False,
        apic_identity_verified=False, runtime_write_enabled=False,
        limits='Original instruction slices on synthetic fuses/registers. Compact command encodings differ from fixed PM storage slots; SMT target stride 32 differs from the published CPUID CCD stride 16. Neither equality in some fixtures nor a reachable consumer authenticates a runtime PM/APIC/OS binding. No target machine is available.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:({x:y for x,y in v.items() if x!='examples'} if k=='initialization' else v) for k,v in report.items()}))


if __name__ == '__main__':
    main()
