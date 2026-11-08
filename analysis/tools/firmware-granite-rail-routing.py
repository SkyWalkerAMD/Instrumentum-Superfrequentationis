#!/usr/bin/env python3
"""Byte-checked SVI startup/status paths with synthetic controller replies.

The local status port is not established as a peripheral setting getter.
Register helpers, mailbox I/O and synchronization use explicit boundaries;
no real hardware, installed BIOS or independent physical rail is validated.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
H = runpy.run_path(str(HERE / 'firmware-granite-semantics.py'))
T = runpy.run_path(str(HERE / 'firmware-granite-topology.py'))
Slice, f32 = H['Slice'], H['f32']
CONFIG, STATE, ACC, OUT = 0xc598, 0x8c30, 0x15320, 0x15b40


def startup_modes(body, code):
    """Run address stores and the three register-02 extraction/store calls.

    The intervening timing/peripheral initialization and the later timing
    cache loop are outside these selected slices, not silently executed.
    """
    trace = hashlib.sha256()
    for reply in range(256):
        vm = Slice(body, code)
        for channel in range(3):
            vm.put(CONFIG + 12*channel + 0x1c, 63-channel, 1)
            vm.put(CONFIG + 12*channel + 0x1e, 255, 1)
        vm.run(0x32aeb, 0x32aff)
        assert [vm.get(CONFIG + 12*c + 0x1c, 1) for c in range(3)] == [1, 2, 3]
        vm.r.update(a3=CONFIG, a6=CONFIG)
        events = []
        replies = [reply, reply ^ 0x55, reply ^ 0xaa]

        def register_write(v):
            events.append(('write', v.r['a10'], v.r['a11'], v.r['a12']))
            assert v.r['a10'] < 3 and v.r['a11'] == 0x25 and v.r['a12'] == 7

        def mode(v):
            channel = v.r['a10']
            child = Slice(body, code)
            child.r['a2'] = channel

            def register_read(c):
                assert (c.r['a10'], c.r['a11']) == (channel, 2)
                events.append(('read', channel, 2, replies[channel]))
                c.r['a10'] = replies[channel]

            child.run(0x32f10, None, hooks={0x32bf8: register_read})
            v.r['a10'] = child.r['a2']

        vm.run(0x32b55, 0x32b72, hooks={0x32cbc: register_write, 0x32f10: mode})
        expected = [(value >> 5) & 7 for value in replies]
        assert [vm.get(CONFIG + 12*c + 0x1e, 1) for c in range(3)] == expected
        assert events == [event for c in range(3) for event in
            (('write', c, 0x25, 7), ('read', c, 2, replies[c]))]
        trace.update(json.dumps([replies, expected, events], separators=(',', ':')).encode())
    return dict(cases=256, mode_stores=768, register_byte_values_per_channel=256,
        initial_channel_addresses=[1, 2, 3], source_register=2, source_bits='7:5',
        mode_addresses=['0x0000c5b6', '0x0000c5c2', '0x0000c5ce'],
        register_helper_results_are_fixtures=True,
        peripheral_timing_initialization_executed=False,
        physical_channel_identity_verified=False, trace_sha256=trace.hexdigest())


class StatusSlice(Slice):
    def __init__(self, body, code, channel, response, alternate=False):
        super().__init__(body, code)
        u = lambda a: struct.unpack_from('<I', body, a)[0]
        base = 0x100000 + channel*0x40000
        self.address = 63-channel if alternate else channel+1
        self.port = ((base + u(0x126dc)) & u(0x11430)) + u(0x11438) + 0x200
        self.response, self.events = response, []
        self.r['a2'] = channel
        self.put(CONFIG + 12*channel + 0x14, base)
        self.put(CONFIG + 12*channel + 0x1c, self.address, 1)

    def get(self, address, size=4):
        if address == self.port:
            assert size == 4 and self.events == [('write', self.port, self.address)]
            self.events.append(('read', address, self.response))
            return self.response
        return super().get(address, size)

    def put(self, address, value, size=4):
        if hasattr(self, 'port') and address == self.port:
            assert size == 4 and not self.events
            self.events.append(('write', address, value))
        else:
            super().put(address, value, size)


def status_read(body, code, channel, response, alternate=False):
    vm = StatusSlice(body, code, channel, response, alternate)
    vm.run(0x32d38, None, hooks={})
    assert vm.r['a2'] == (response >> 6) & 511
    assert vm.events == [('write', vm.port, vm.address), ('read', vm.port, response)]
    return vm.r['a2'], vm.events


def status_cases(body, code):
    trace = hashlib.sha256()
    cases = 0
    for channel, value, rest, alternate in itertools.product(range(3), range(512),
                                                             (0, 0xffff803f), (False, True)):
        response = rest | value << 6
        actual, events = status_read(body, code, channel, response, alternate)
        assert actual == value
        trace.update(json.dumps([channel, response, actual, events], separators=(',', ':')).encode())
        cases += 1
    return dict(cases=cases, codes=512, firmware_channels=[0, 1, 2],
        status_port_offset='0x00073014', result_bits='14:6',
        sequence=['select configured address', 'read one local status word'],
        completion_bit_checked=False, polling_executed=False,
        peripheral_register_read_packet_sent=False, device_setting_readback_verified=False,
        trace_sha256=trace.hexdigest())


def mirror_startup(body, code):
    u = lambda a: struct.unpack_from('<I', body, a)[0]
    assert code[0x1140]['text'] == 'call8 0x00032ae8'
    assert code[0x1143]['text'] == 'call8 0x00020a20'
    assert code[0x20acb]['text'] == 'call8 0x00032d38'
    trace = hashlib.sha256()
    cases = 0
    for mode, value in itertools.product(range(8), range(512)):
        vm = Slice(body, code)
        vm.r.update(a1=0x50000)
        vm.putf(0x50004, f32(.245))
        vm.put(CONFIG+0x36, mode, 1)
        for address in (0x11114, 0x1111c, 0x11120): vm.put(address, u(address))
        for channel in range(3):
            vm.put(STATE+0x3c+24*channel, 0xabcd, 2)
            vm.put(STATE+0x3e+24*channel, 0xdcba, 2)
            vm.put(STATE+0x40+24*channel, 0x1234, 2)
            vm.putf(STATE+0x44+24*channel, -77-channel)
        replies = [(value+137*c) & 511 for c in range(3)]
        calls = []

        def reader(v):
            channel = v.r['a10']
            assert channel == len(calls)
            actual, events = status_read(body, code, channel, replies[channel] << 6)
            calls.append((channel, events))
            v.r['a10'] = actual

        vm.run(0x20abe, 0x20b0d, hooks={0x32d38: reader})
        assert len(calls) == 3
        produced = []
        for channel in range(3):
            base, step = (.49, .01) if channel == 2 and mode == 1 else (.245, .005)
            expected = f32(f32(base)+replies[channel]*f32(step))
            assert vm.get(STATE+0x3c+24*channel, 2) == replies[channel]
            assert vm.get(STATE+0x3e+24*channel, 2) == replies[channel]
            assert vm.get(STATE+0x40+24*channel, 2) == 0x1234
            assert vm.getf(STATE+0x44+24*channel) == expected
            produced.append(expected)
        trace.update(struct.pack('<5I3f', mode, value, *replies, *produced))
        cases += 1
    return dict(cases=cases, channel_mirrors=cases*3, all_eight_mode_codes=True,
        source='startup local-controller status bits, then software VID conversion',
        source_addresses=['0x00008c74', '0x00008c8c', '0x00008ca4'],
        current_and_requested_code_addresses=[['0x00008c6c', '0x00008c6e'],
            ['0x00008c84', '0x00008c86'], ['0x00008c9c', '0x00008c9e']],
        conversion='0.245 + code * 0.005; channel 2 mode 1 uses 0.49 + code * 0.01',
        zero_code_keeps_software_offset=True, override_flags_untouched_in_selected_slice=True,
        continuous_status_refresh_verified=False, device_setting_readback_verified=False,
        trace_sha256=trace.hexdigest())


def guarded_register_writer(body, code):
    """The separate table entry is not asserted to be an enabled host route."""
    u = lambda a: struct.unpack_from('<I', body, a)[0]
    assert u(0x6f8c) == 0x330b0
    trace = hashlib.sha256()
    cases = accepted = cached = 0
    for channel, register, gate, data in itertools.product((0, 1, 2, 3, 255),
                                                          range(256), (0, 1), (0, 255)):
        vm = Slice(body, code)
        vm.r.update(a1=0x50000, a2=0x55)
        vm.put(CONFIG+0x12, gate, 1)
        events = []
        base = 0x100000 + channel*0x40000
        address = channel+1
        prior = 0xa5a55a5a
        select = ((base + u(0x12708)) & u(0x11430)) + u(0x11438) + 0x200
        port = ((base + u(0x1270c)) & u(0x11430)) + u(0x11438) + 0x200
        if channel < 3:
            vm.put(CONFIG+12*channel+0x14, base)
            vm.put(CONFIG+12*channel+0x1c, address, 1)
            vm.put(CONFIG+12*channel+0x18, 0x12345678)
            vm.put(port, prior)

        def argument(v):
            assert v.r['a10'] == 0x55
            v.r['a10'] = channel | register << 8 | data << 16 | 0x5a000000

        def lock(v):
            assert v.r['a10'] == 0x2000
            events.append(('lock',))

        def write(v):
            events.append(('write', v.r['a10'], v.r['a11'], v.r['a12']))

        def unlock(v):
            assert v.r['a10'] == 0x2000
            events.append(('unlock',))

        def respond(v):
            assert v.r['a10'] == 0x55
            events.append(('response', v.r['a11']))

        vm.run(0x330b0, None, hooks={0x15dc: argument, 0x3bb64: lock,
            0x32cbc: write, 0x3bc38: unlock, 0x14ec: respond})
        allowed = not gate and channel < 3 and 0x20 <= register <= 0x2c and register != 0x22
        if allowed:
            assert events == [('lock',), ('write', channel, register, data), ('unlock',), ('response', 1)]
            accepted += 1
            if register == 0x20:
                expected = (prior & ~(0xff0 | 1 << 28 | 1 << 29)) | ((data & 15) << 4) | \
                    ((data & 15) << 8) | (((data >> 4) & 1) << 28) | ((data >> 7) << 29)
                assert vm.get(select) == address
                assert vm.get(port) == vm.get(CONFIG+12*channel+0x18) == expected
                cached += 1
            else:
                assert vm.get(port) == prior and vm.get(CONFIG+12*channel+0x18) == 0x12345678
        else:
            assert events == [('response', 255)]
        trace.update(json.dumps([channel, register, gate, data, events], separators=(',', ':')).encode())
        cases += 1
    return dict(cases=cases, accepted=accepted, rejected=cases-accepted,
        register_20_cache_updates=cached, table_pointer='0x00006f8c', handler='0x000330b0',
        gate_address='0x0000c5aa', gate_nonzero_rejects=True, channels=[0, 1, 2],
        registers=[i for i in range(0x20, 0x2d) if i != 0x22],
        mailbox_lock_and_register_write_helpers_are_fixtures=True,
        enabled_host_route_verified=False, device_effects_verified=False,
        independent_second_rail_verified=False, trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('smu', 'study', 'output'): p.add_argument('--'+key, type=Path, required=True)
    a = p.parse_args()
    body = a.smu.read_bytes()
    code = T['instructions'](a.study, body, T['SMU_SHA'])
    result = dict(schema=1, source_sha256=T['SMU_SHA'], hardware_execution=False,
        startup_modes=startup_modes(body, code), local_status_reader=status_cases(body, code),
        startup_mirrors=mirror_startup(body, code), guarded_writer=guarded_register_writer(body, code),
        physical_channel_identity_verified=False, independent_second_rail_verified=False,
        runtime_write_enabled=False, device_setting_readback_verified=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__': main()
