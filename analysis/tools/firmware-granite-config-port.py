#!/usr/bin/env python3
"""Check MP5 configuration ports and retained mirrors on synthetic memory.

Original instruction bytes run in the bounded Slice interpreter. Timer values
and peripheral replies are scripted; this is not electrical/device validation.
The raw read primitive is not asserted to be a reachable host setting getter.
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
Slice, SHA = H['Slice'], T['MP5_SHA']
WRITE, TRIGGER = 0x18c0064, 0x18c006c
READ, READ_TRIGGER, REPLY, TIMER = 0x1bc0064, 0x1bc006c, 0x18c0068, 0x32001c0


class Port(Slice):
    def __init__(self, body, code, replies=()):
        super().__init__(body, code)
        self.r['a1'] = 0x50000
        self.events = []
        self.replies = deque(replies)

    def put(self, address, value, size=4):
        if address in (WRITE, TRIGGER, READ, READ_TRIGGER):
            assert size == 4
            self.events.append(('write', address, value))
        else:
            assert address < 0x100000, hex(address)
            super().put(address, value, size)

    def get(self, address, size=4):
        if address == REPLY:
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


def delay(body, code, ticks, start, stalled=False):
    class Timer(Slice):
        def get(self, address, size=4):
            assert address == TIMER and size == 4
            value = self.values.popleft() if len(self.values) > 1 else self.values[0]
            self.reads.append(value)
            return value
    vm = Timer(body, code)
    vm.r['a2'] = ticks
    offsets = (0, 0) if stalled else (0, 0, 1, 2, 3)
    vm.values = deque((start + x) & 0xffffffff for x in offsets)
    vm.reads = []
    vm.run(0x22e4, 0x22fc, budget=128)
    assert not stalled and ((vm.reads[-1] - vm.reads[0]) & 0xffffffff) >= ticks
    return vm.reads


def writes(body, code):
    trace = hashlib.sha256()
    cases = 0
    for selector, value, mask in itertools.product(range(8), range(256), (1, 0xff, 0x30000)):
        vm = Port(body, code)
        # a2 is overwritten before use by this primitive. It is not a target
        # selector; varying it must not turn a local port into a second rail.
        vm.r.update(a2=(value & 1) * 0xdeadbeef, a3=mask, a4=selector, a5=value)
        waits = []
        def wait(caller):
            assert caller.r['a10'] == 3
            start = 0xfffffffe if value & 1 else 0x100
            waits.append(delay(body, code, 3, start))
            caller.events.append(('wait_ticks', 3))
        vm.run(0x255d0, 0x25602, hooks={0x22e4: wait})
        expected = [('write', WRITE, (value << 4) | selector), ('wait_ticks', 3),
                    ('write', TRIGGER, mask), ('wait_ticks', 3),
                    ('write', TRIGGER, 0), ('wait_ticks', 3)]
        assert vm.events == expected and len(waits) == 3
        trace.update(json.dumps([selector, value, mask, vm.events, waits], separators=(',', ':')).encode())
        cases += 1
    wrappers = []
    for entry, call, mask in ((0x255a8, 0x255b4, 0xff), (0x255bc, 0x255c8, 0x30000)):
        assert code[call]['text'] == 'call8 0x000255d0'
        for selector in range(8):
            vm = Slice(body, code); vm.r.update(a2=7, a3=selector, a4=0x91)
            vm.run(entry, call)
            assert [vm.r['a'+str(i)] for i in range(10,14)] == [7, mask, selector, 0x91]
        wrappers.append(dict(entry=hex(entry), mask=hex(mask), cases=8))
    # Width behavior is reported, not promoted into an accepted input range.
    for selector, value in ((8, 256), (0xffffffff, 0xffffffff), (0x100, 0x10000000)):
        vm = Port(body, code); vm.r.update(a3=0xffffffff, a4=selector, a5=value)
        vm.run(0x255d0, 0x25602, hooks={0x22e4: lambda _: None})
        assert vm.events[0] == ('write', WRITE, ((value << 4) | (selector & 7)) & 0xffffffff)
    try:
        delay(body, code, 3, 0xfffffffe, stalled=True)
    except ValueError as error:
        assert str(error) == 'Bounded instruction budget exceeded'
    else:
        raise AssertionError('A stopped timer unexpectedly completed')
    return dict(cases=cases, wrappers=wrappers, oversized_cases=3, stalled_timer_cases=1,
        entry='0x255d0', packet='uint32(code << 4) | (selector & 7)',
        packet_address=hex(WRITE), trigger_address=hex(TRIGGER),
        trigger_sequence='mask then zero', wait_ticks_after_each_write=3,
        timer_address=hex(TIMER), timer_rate_hz_verified=False,
        completion_polled=False, returned_device_payload=False,
        first_argument_selects_target=False, trace_sha256=trace.hexdigest())


def reads(body, code):
    trace = hashlib.sha256(); cases = 0
    for selector, value, wait in itertools.product(range(8), range(256), (0, 1, 3)):
        # High status bits are intentionally distinct from the raw result byte.
        replies = [0xa5000000 | (value ^ 0xff)] * wait + [0x5a000100 | value]
        vm = Port(body, code, replies)
        mask = 1 << (value % 8)
        vm.r.update(a2=0xdeadbeef, a3=mask, a4=selector | 0xfffffff8)
        vm.run(0x25604, 0x25637)
        expected = [('write', READ, 8 | selector), ('write', READ_TRIGGER, mask)]
        expected += [('read', REPLY, r) for r in replies]
        expected += [('write', READ_TRIGGER, 0)]
        assert vm.events == expected and not vm.replies and vm.r['a2'] == value
        trace.update(json.dumps([selector, value, vm.events], separators=(',', ':')).encode())
        cases += 1
    vm = Port(body, code, (None,)); vm.r.update(a3=1, a4=0)
    try:
        vm.run(0x25604, 0x25637, budget=128)
    except ValueError as error:
        assert str(error) == 'Bounded instruction budget exceeded'
    else:
        raise AssertionError('Missing reply flag unexpectedly completed')
    assert [e for e in vm.events if e[0] == 'write'] == [('write', READ, 8), ('write', READ_TRIGGER, 1)]
    return dict(cases=cases, stalled_reply_cases=1, entry='0x25604',
        request_address=hex(READ), trigger_address=hex(READ_TRIGGER), reply_address=hex(REPLY),
        request='8 | (selector & 7)', ready_bit=8, returned_bits='7:0',
        result_representation='raw_byte', physical_unit_verified=False,
        actual_device_response_verified=False, host_getter_route_verified=False,
        write_then_read_pair_verified=False, trace_sha256=trace.hexdigest())


def alternate_transition(body, code):
    trace = hashlib.sha256(); cases = changed = 0
    old, new, state = 0x50000, 0x51000, 0x7820
    for enabled, mode3, mode4, before, after, mask in itertools.product(
            (0, 1), (0, 1), (0, 1), (0, 128), (0, 127, 255), (0, 0x55, 0xff)):
        if enabled and not (mode3 or mode4):
            continue  # The ordinary peripheral transition was studied separately.
        vm = Port(body, code)
        vm.r.update(a1=0x53000, a2=0, a3=old, a4=new)
        for address in range(state, state+0x4c10):
            vm.put(address, 0, 1)
        for index in range(54):
            vm.put(old+4*index, 0xa5000000+index)
            vm.put(new+4*index, 0x51000000+index)
        vm.put(old+9, before, 1); vm.put(new+9, after, 1)
        vm.putf(new+12, 1.25); vm.putf(0x8030, .625); vm.put(0x802a, before, 1)
        vm.put(state, enabled, 1); vm.put(state+3, mode3, 1); vm.put(state+4, mode4, 1)
        for index in range(8):
            vm.put(state+0xd+index, (mask >> index) & 1, 1)
        incoming = bytes(vm.get(new+i, 1) for i in range(216))
        def accessor(caller):
            child = Slice(body, code); child.r['a2'] = caller.r['a10']
            child.run(0x26820, 0x2682e)
            caller.r['a10'] = child.r['a2']
        vm.run(0x27b54, 0x27c60, hooks={0x26820: accessor})
        assert not vm.events  # Neither the write port nor the raw reader executes.
        result = bytes(vm.get(old+i, 1) for i in range(216))
        assert vm.getf(0x8030) == .625
        assert vm.get(0x802a, 1) == (after if enabled else before)
        if enabled:
            assert result == incoming
            changed += before != after
        else:
            assert result != incoming and vm.get(old+9, 1) == before
        trace.update(struct.pack('<7I', enabled, mode3, mode4, before, after, mask, vm.get(0x802a, 1)))
        cases += 1
    return dict(cases=cases, changed_code_with_retained_float_cases=changed,
        entry='0x27b54', state_source='0x7820', exported_float='0x8030', stored_code='0x802a',
        disabled_no_update=True, alternate_gate='byte[state+3] != 0 or byte[state+4] != 0',
        alternate_branch_copies_full_record=True, alternate_branch_writes_configuration_port=False,
        retained_export_despite_changed_code=True, mode_meanings_verified=False,
        trace_sha256=trace.hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('mp5', 'study', 'output'):
        parser.add_argument('--'+key, type=Path, required=True)
    args = parser.parse_args()
    body = args.mp5.read_bytes(); code = T['instructions'](args.study, body, SHA)
    report = dict(schema=1, source_sha256=SHA, hardware_execution=False,
        writes=writes(body, code), reads=reads(body, code),
        alternate_transition=alternate_transition(body, code),
        new_physical_units={}, physical_or_os_core_mapping_verified=False,
        independent_second_rail_verified=False, runtime_write_enabled=False,
        limits='Timer and reply words are fixtures. Local MP5 ports are not host addresses. Raw read primitive existence proves no host route, selected rail, valid electrical range or applied-setting readback.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
