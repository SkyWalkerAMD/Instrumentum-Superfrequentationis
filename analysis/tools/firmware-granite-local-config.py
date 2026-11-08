#!/usr/bin/env python3
"""Replay Granite local configuration C dispatch, byte ports and mirror widths.

These are bounded original-instruction experiments with scripted port replies.
They establish neither physical units nor electrically applied settings. The
software mirror, the submitted byte and a raw-reader fixture are kept separate.
"""
import argparse
from collections import deque
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
H = runpy.run_path(str(HERE / 'firmware-granite-semantics.py'))
T = runpy.run_path(str(HERE / 'firmware-granite-topology.py'))
C = runpy.run_path(str(HERE / 'firmware-granite-common-config.py'))
Slice, f32, helper = H['Slice'], H['f32'], C['helper']
CONFIG, RECORDS = 0xc270, (0xc270, 0xc284, 0xc298)
MASK = 0xffffffff


class Port(Slice):
    def __init__(self, body, code, replies=()):
        super().__init__(body, code)
        self.r['a1'] = 0x50000
        self.events = []
        self.replies = deque(replies)
        self.ports = {0xf885a908, 0xf885a90c}
        for p in RECORDS:
            for i in range(20):
                super().put(p+i, body[p+i], 1)

    def put(self, address, value, size=4):
        if address in self.ports:
            assert size == 4
            self.events.append(('write', address, value & MASK))
        else:
            assert address < 0x100000, hex(address)
            super().put(address, value, size)

    def get(self, address, size=4):
        if address in self.ports:
            assert size == 4 and self.replies
            value = self.replies[0]
            if value is None:
                value = 0
            else:
                self.replies.popleft()
            self.events.append(('read', address, value))
            return value
        assert address < 0x100000, hex(address)
        return super().get(address, size)


def read_primitive(vm, budget=256):
    """Execute NEG's checked two's-complement operation without changing Slice.

    The shared numerical interpreter does not implement integer NEG. The two
    bounded surrounding slices and this one checked instruction cover the
    complete raw reader; no fabricated disassembly instruction is substituted.
    """
    vm.run(0x1c9ec, 0x1ca02)
    ins = vm.code[0x1ca02]
    raw = bytes.fromhex(ins['bytes'])
    assert ins['text'] == 'neg a9,a5' and len(raw) == 3
    assert vm.body[0x1ca02:0x1ca05] == raw
    vm.r['a9'] = -vm.r['a5'] & MASK
    vm.trace.append(0x1ca02)
    vm.run(0x1ca05, None, budget=budget, hooks={})


def raw_ports(body, code):
    trace = hashlib.sha256()
    write_cases = read_cases = 0
    for record, selector, value in itertools.product(RECORDS, range(8), range(256)):
        vm = Port(body, code)
        vm.r.update(a2=record, a3=selector, a4=value)
        address = (vm.get(record+8) & 0xfffff) + 0xf8800000
        target_mask = vm.get(record+6, 2)
        vm.run(0x1c9c4, None, hooks={})
        expected = selector | value << 4 | target_mask << 12
        assert vm.events == [('write', address, expected)]
        trace.update(struct.pack('<4I', record, selector, value, expected))
        write_cases += 1
    # Raw writer has no selector/code width clamp. These are observations of
    # the leaf's encoding, not accepted or safe hardware input ranges.
    for selector, value in ((8, 256), (0x100, 0x10000), (MASK, MASK)):
        vm = Port(body, code); vm.r.update(a2=CONFIG, a3=selector, a4=value)
        vm.run(0x1c9c4, None, hooks={})
        assert vm.events == [('write', 0xf885a908, (selector | value << 4 | 2 << 12) & MASK)]
    for record, selector, value, delay in itertools.product(RECORDS, range(8), range(256), (0, 2)):
        pending = ((value ^ 0xff) << 24) | 0x12345  # Bit 23 remains clear.
        success = (value << 24) | 0x800000 | 0x54321
        vm = Port(body, code, [pending] * delay + [success])
        vm.r.update(a2=record, a3=selector)
        address = (vm.get(record+8) & 0xfffff) + 0xf8800000
        target_mask = vm.get(record+6, 2)
        request = (selector | 8) | (target_mask & -target_mask) << 12
        read_primitive(vm)
        expected = [('write', address, request)]
        expected += [('read', address, x) for x in [pending] * delay + [success]]
        assert vm.events == expected and vm.r['a2'] == value and not vm.replies
        trace.update(struct.pack('<5I', record, selector, value, delay, request))
        read_cases += 1
    vm = Port(body, code, [None]); vm.r.update(a2=CONFIG, a3=0)
    try:
        read_primitive(vm, budget=128)
    except ValueError as error:
        assert str(error) == 'Bounded instruction budget exceeded'
    else:
        raise AssertionError('Reader completed without its ready bit')
    assert [x for x in vm.events if x[0] == 'write'] == [('write', 0xf885a908, 0x2008)]
    records = [dict(address=hex(p), target_mask=struct.unpack_from('<H', body, p+6)[0],
                    port=hex((struct.unpack_from('<I', body, p+8)[0] & 0xfffff)+0xf8800000)) for p in RECORDS]
    return dict(write_cases=write_cases, read_cases=read_cases, oversized_leaf_cases=3,
        stalled_reader_cases=1, records=records,
        write_entry='0x1c9c4', write_completion_polled=False,
        read_entry='0x1c9ec', reader_uses_lowest_mask_bit=True,
        ready_bit=23, result_bits='31:24', result_representation='raw_byte',
        reader_has_selected_loop_timeout=False, reply_values_are_fixtures=True,
        physical_target_identity_verified=False, trace_sha256=trace.hexdigest())


def execute_step(body, code, parent, events):
    child = Port(body, code)
    child.ram = parent.ram.copy()
    child.f['f0'] = parent.f['f0']
    writes = []; waits = []
    def quantize(vm):
        vm.r['a10'] = helper(body, code, 0x1ca44, value=vm.f['f0'])
    def convert(vm):
        vm.f['f0'] = helper(body, code, 0x1ca2c, vm.r['a10'])
    def write(vm):
        leaf = Port(body, code); leaf.ram = vm.ram.copy()
        leaf.r.update(a2=vm.r['a10'], a3=vm.r['a11'], a4=vm.r['a12'])
        leaf.run(0x1c9c4, None, hooks={})
        assert len(leaf.events) == 1
        writes.append((vm.r['a10'], vm.r['a11'], vm.r['a12']))
        events.extend(leaf.events)
    def wait(vm):
        waits.append(vm.r['a10']); events.append(('delay_argument', vm.r['a10']))
    child.run(0x32220, None, budget=20000,
              hooks={0x1ca44:quantize, 0x1ca2c:convert, 0x1c9c4:write, 0x2f1c:wait})
    parent.ram = child.ram
    return writes, waits


def host_route(body, code):
    assert struct.unpack_from('<II', body, 0x6fa4+8*0x73) == (0x1dca0, 0x206)
    scale = struct.unpack_from('<f', body, 0x11420)[0]
    assert struct.unpack_from('<I', body, 0x11420)[0] == 0x3a83126f
    trace = hashlib.sha256(); cases = equal = oversized = write_count = 0
    examples = []
    payloads = (498, 500, 750, 1000, 1100, 1249, 1252, 1254, 1255, 1258, 1260, 1500, 2000, 3000)
    for old, payload in itertools.product((0, 1, 85, 169, 203, 254, 255), payloads):
        vm = Port(body, code); vm.r['a2'] = 0x1234
        vm.put(CONFIG+1, old, 1); vm.putf(CONFIG+12, -999)
        value = f32(f32(payload)*scale)
        target = helper(body, code, 0x1ca44, value=value)
        events = []; replies = []; step = []
        def argument(w):
            assert w.r['a10'] == 0x1234
            w.r['a10'] = payload
        def apply(w):
            assert w.f['f0'] == value
            step.append(execute_step(body, code, w, events))
        def reply(w):
            assert w.r['a10'] == 0x1234
            replies.append(w.r['a11'])
        vm.run(0x1dca0, None, hooks={0x15dc:argument, 0x32220:apply, 0x14ec:reply})
        assert replies == [1] and len(step) == 1
        direction = 1 if target > old else -1
        sequence = list(range(old+direction, target+direction, direction)) if old != target else []
        writes, waits = step[0]
        assert writes == [(CONFIG, 0, x & 255) for x in sequence]
        assert waits == [25] * len(sequence)
        assert vm.get(CONFIG+1, 1) == target & 255
        mirror = vm.getf(CONFIG+12)
        assert mirror == (helper(body, code, 0x1ca2c, target) if sequence else -999)
        assert all(x[0] != 'read' for x in events)
        if target > 255:
            assert mirror != helper(body, code, 0x1ca2c, target & 255)
            oversized += 1
        equal += not sequence; write_count += len(sequence); cases += 1
        trace.update(json.dumps([old, payload, target, mirror, events, replies], separators=(',', ':')).encode())
        if old == 255 and payload in (1252, 1255, 1500, 2000):
            examples.append(dict(old_byte=old, payload=payload, input_float=value,
                target_u32=target, submitted_byte=(target & 255) if sequence else None,
                stored_byte=vm.get(CONFIG+1, 1), software_mirror=mirror,
                converted_submitted_byte=helper(body, code, 0x1ca2c, target & 255),
                writes=len(sequence), reply=replies[0]))
    # The handler reaches the setter even for these full-width arguments. Do
    # not attempt billions of loop iterations or model negative FP conversion.
    large = []
    for payload in (0, 497, 0x10000, 0x7fffffff, MASK):
        vm = Slice(body, code); vm.r['a2'] = 0x1234
        vm.run(0x1dca0, 0x1dcb4, hooks={0x15dc:lambda w: w.r.update(a10=payload)})
        assert vm.f['f0'] == f32(f32(payload)*scale)
        row = dict(payload=payload, setter_input=vm.f['f0'], setter_call_reached=True,
                   full_step_loop_executed=False)
        if payload >= 0x10000:
            row['quantized_u32'] = helper(body, code, 0x1ca44, value=vm.f['f0'])
            assert row['quantized_u32'] > 255
        else:
            row['negative_quantizer_exception_semantics_verified'] = False
        large.append(row)
    return dict(command='0x73', table='0x6fa4', handler='0x1dca0', flags='0x206',
        payload_kind='u32 multiplied by binary32 0.001', scale_bits='0x3a83126f',
        cases=cases, equal_code_retentions=equal, oversized_target_cases=oversized,
        synthetic_port_writes=write_count, examples=examples, prefix_only_cases=large,
        quantized_target_bits=32, submitted_code_bits=8, software_mirror_uses_full_target=True,
        selected_handler_and_step_payload_range_guard=False, acknowledgement_is_applied_readback=False,
        raw_reader_called=False, physical_unit_verified=False, delay_unit_verified=False,
        trace_sha256=trace.hexdigest())


def startup_sequence(body, code):
    trace = hashlib.sha256(); cases = equal_rewrites = wrapped = 0
    base = struct.unpack_from('<f', body, 0x11440)[0]
    reciprocal = struct.unpack_from('<f', body, 0x11448)[0]
    fixed = struct.unpack_from('<f', body, 0x12690)[0]
    assert struct.unpack_from('<I', body, 0x12690)[0] == 0x3f8ccccd
    fixed_code = helper(body, code, 0x1ca44, value=fixed)
    assert fixed_code == 203
    for target, old, mode3, mode4 in itertools.product((0, 1, 127, 255, 256, 257, 512), (0, 1, 127, 255), (0, 0x40, 255), (0, 8, 255)):
        vm = Port(body, code)
        vm.put(CONFIG+1, old, 1); vm.put(CONFIG+2, mode3, 1); vm.put(CONFIG+3, mode4, 1)
        vm.putf(CONFIG+12, -999)
        vm.f['f0'] = f32(base+(target+.5)/reciprocal)
        assert helper(body, code, 0x1ca44, value=vm.f['f0']) == target
        calls = []
        def quantize(w):
            w.r['a10'] = helper(body, code, 0x1ca44, value=w.f['f0'])
        def convert(w):
            w.f['f0'] = helper(body, code, 0x1ca2c, w.r['a10'])
        def write(w):
            calls.append((w.r['a10'], w.r['a11'], w.r['a12']))
            leaf = Port(body, code); leaf.ram = w.ram.copy()
            leaf.r.update(a2=w.r['a10'], a3=w.r['a11'], a4=w.r['a12'])
            leaf.run(0x1c9c4, None, hooks={}); w.events.extend(leaf.events)
        vm.run(0x32298, None, hooks={0x1ca44:quantize, 0x1ca2c:convert, 0x1c9c4:write})
        expected = [(CONFIG, 4, mode4 | 8), (CONFIG, 0, fixed_code),
                    (CONFIG, 3, mode3 & 0xbf), (CONFIG, 4, (mode4 | 8) & 0xf9),
                    (CONFIG, 0, target & 255), (CONFIG, 4, mode4 & 0xf1)]
        assert calls == expected and len(vm.events) == 6
        assert all(x[0] == 'write' for x in vm.events)
        assert vm.get(CONFIG+1, 1) == target & 255
        assert vm.get(CONFIG+2, 1) == mode3 & 0xbf and vm.get(CONFIG+3, 1) == mode4 & 0xf1
        assert vm.getf(CONFIG+12) == helper(body, code, 0x1ca2c, target & 255)
        equal_rewrites += (target & 255) == old; wrapped += target > 255; cases += 1
        trace.update(json.dumps([target, old, mode3, mode4, calls, vm.getf(CONFIG+12)], separators=(',', ':')).encode())
    return dict(entry='0x32298', cases=cases, equal_byte_rewrites=equal_rewrites,
        oversized_target_cases=wrapped, writes_per_case=6, fixed_intermediate_float_bits='0x3f8ccccd',
        fixed_intermediate_code=fixed_code, submitted_target_bits=8,
        mirror_converts_truncated_target=True, raw_reader_called=False,
        write_completion_polled=False, delay_called=False, trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu', 'study', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args(); body = a.smu.read_bytes()
    code = T['instructions'](a.study, body, T['SMU_SHA'])
    result = dict(schema=1, source_sha256=T['SMU_SHA'], hardware_execution=False,
        raw_ports=raw_ports(body, code), host_route=host_route(body, code),
        startup_sequence=startup_sequence(body, code),
        physical_units_verified=False, independent_second_rail_verified=False,
        local_raw_reader_host_route_verified=False, device_write_read_pair_verified=False,
        runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
