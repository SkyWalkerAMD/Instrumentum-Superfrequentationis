#!/usr/bin/env python3
"""Replay APOB reference register selectors in isolated synthetic memory.

Only the two pinned protocol +0x48 bodies execute. Register services are
scripted callbacks; no firmware installation, device or host I/O executes.
The derived addresses/bits are not physical CCD, PM slot or OS CPU identities.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

import unicorn as u
from unicorn import x86_const as x

H = runpy.run_path(str(Path(__file__).with_name('firmware-granite-topology-protocol.py')))
DATA, STACK, STOP = 0x300000, 0x110008, 0xf000
OBJECT, RECORD, SERVICE = DATA, DATA+0x1000, DATA+0x2000
READ, WRITE = 0xe000, 0xe100
BAD, DEVICE_ERROR = H['BAD'], H['DEVICE_ERROR']
PROFILES = {'a': (0x2118, 0x2284, 0x68, 0x371d),
            'b': (0x2178, 0x22e4, 0x70, 0x374d)}


class Replay:
    def __init__(self, image, profile):
        self.image, self.profile = image, profile
        self.trace = hashlib.sha256()
        self.cases = 0

    def run(self, data, group=0, subgroup=0, core=0, thread=0,
            prior=0xa0000000, read_status=0, write_status=0, socket=0,
            die=0, object_present=True, record_present=True):
        entry, end, service_offset, diagnostics = PROFILES[self.profile]
        vm = u.Uc(u.UC_ARCH_X86, u.UC_MODE_64)
        for address, size in ((0, 0x10000), (0x100000, 0x20000), (DATA, 0x10000)):
            vm.mem_map(address, size)
        vm.mem_write(0, self.image)
        # Diagnostic/assertion behavior is excluded, not silently emulated.
        vm.mem_write(diagnostics, b'\0')
        vm.mem_write(OBJECT+0x50, struct.pack('<2Q', 1, 1))
        vm.mem_write(OBJECT+0x60, struct.pack('<Q', RECORD if record_present else 0))
        vm.mem_write(OBJECT+service_offset, struct.pack('<Q', SERVICE))
        vm.mem_write(SERVICE+0x20, struct.pack('<2Q', READ, WRITE))
        vm.mem_write(RECORD, bytes(data))
        vm.mem_write(STACK, struct.pack('<Q', STOP))
        vm.reg_write(x.UC_X86_REG_RSP, STACK)
        args = (OBJECT if object_present else 0, socket, die, group, subgroup, core, thread)
        for reg, value in zip((x.UC_X86_REG_RCX, x.UC_X86_REG_RDX,
                               x.UC_X86_REG_R8, x.UC_X86_REG_R9), args):
            vm.reg_write(reg, value)
        vm.mem_write(STACK+40, struct.pack('<3Q', *args[4:]))
        events = []

        def instruction(uc, address, size, _):
            if address in (READ, WRITE):
                assert uc.reg_read(x.UC_X86_REG_RCX) == SERVICE
                cpu_socket = uc.reg_read(x.UC_X86_REG_EDX)
                register = uc.reg_read(x.UC_X86_REG_R8D)
                ptr = uc.reg_read(x.UC_X86_REG_R9)
                assert 0x100000 <= ptr < 0x120000-4
                if address == READ:
                    uc.mem_write(ptr, struct.pack('<I', prior))
                    events.append(('read', cpu_socket, register))
                    status = read_status
                else:
                    value, = struct.unpack('<I', uc.mem_read(ptr, 4))
                    events.append(('write', cpu_socket, register, value))
                    status = write_status
                sp = uc.reg_read(x.UC_X86_REG_RSP)
                ret, = struct.unpack('<Q', uc.mem_read(sp, 8))
                uc.reg_write(x.UC_X86_REG_RAX, status)
                uc.reg_write(x.UC_X86_REG_RSP, sp+8)
                uc.reg_write(x.UC_X86_REG_RIP, ret)
            else:
                assert entry <= address < end, hex(address)

        def write(uc, access, address, size, value, _):
            assert 0x100000 <= address and address+size <= 0x120000, hex(address)

        vm.hook_add(u.UC_HOOK_CODE, instruction)
        vm.hook_add(u.UC_HOOK_MEM_WRITE, write)
        vm.emu_start(entry, STOP, count=10000)
        assert vm.reg_read(x.UC_X86_REG_RIP) == STOP
        result = vm.reg_read(x.UC_X86_REG_RAX)
        self.trace.update(json.dumps([bytes(data).hex(), args, prior, read_status,
                                     write_status, result, events], separators=(',', ':')).encode())
        self.cases += 1
        return result, events


def check(replay):
    cases = 0
    # Selected entry is separated from the first entry whose flags supply the
    # bit stride. Stored core IDs are deliberately unrelated to ordinals.
    for first, outer, middle, core, thread in itertools.product(
            ((0, 0), (0, 1), (1, 0), (2, 255)), (0, 1, 8, 64, 254),
            (0, 1, 2, 6, 254), range(8), range(2)):
        data = H['records']((19, outer), (middle, 3), (15, 13, 11, 9, 7, 5, 3, 1))
        data[0x33:0x35] = bytes(first)
        prefix = (1+bool(first[1])) if first[0] else 0
        bit = prefix*core+thread
        register = ((0x203c0020 | outer << 23) if replay.profile=='a' else
                    (0x20bc0020 | middle << 22)) & 0xffffffff
        status, events = replay.run(data, group=1, core=core, thread=thread)
        assert status == 0 and events == [('read', 0, register),
            ('write', 0, register, 0xa0000000 | (1 << bit))]
        cases += 1
    base = H['records']((0,), (0,), tuple(range(8)))
    for read_status, write_status in itertools.product((0, 1, DEVICE_ERROR), (0, 1, DEVICE_ERROR)):
        status, events = replay.run(base, core=3, thread=1,
                                    read_status=read_status, write_status=write_status)
        failed_read = bool(read_status >> 63)
        assert status == (read_status if failed_read else write_status)
        assert len(events) == (1 if failed_read else 2)
    invalid = [dict(object_present=False), dict(record_present=False), dict(socket=1),
               dict(die=1), dict(group=8), dict(subgroup=2), dict(core=8)]
    for kwargs in invalid:
        status, events = replay.run(base, **kwargs)
        assert status == BAD and not events
    for at in (0x30, 0x31, 0x32, 0x33):
        data = bytearray(base); data[at] = 0 if at==0x33 else 255
        status, events = replay.run(data)
        assert status == BAD and not events
    # Missing explicit thread bound: thread 2 reads the next core's ID byte.
    # This documents why the product only derives candidates for flag 0/1;
    # it does not reproduce the unchecked firmware indexing in product code.
    status, events = replay.run(base, thread=2)
    assert status == 0 and events[-1][-1] == (0xa0000000 | 4)
    data = bytearray(base); data[0x35] = 0
    status, events = replay.run(data, thread=2)
    assert status == BAD and not events
    return dict(cases=replay.cases, routing_cases=cases, service_status_cases=9,
        invalid_cases=11, unchecked_thread_cases=2, first_core_flags_supply_stride=True,
        selected_core_stored_id_used=False, thread_argument_explicitly_bounded=False,
        address_formula='uint32(0x203c0020 | (group_stored_id << 23))' if replay.profile=='a'
            else 'uint32(0x20bc0020 | (subgroup_stored_id << 22))',
        bit_formula='(first_core_flag_prefix * core_ordinal + thread_argument) & 31',
        register_services_are_fixtures=True, trace_sha256=replay.trace.hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('provider-a', 'provider-b', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    assert u.__version__ == '2.1.4'
    results = {}
    for profile, (expected, _) in H['PROVIDERS'].items():
        image = H['image'](getattr(args, 'provider_'+profile), expected)
        results[profile] = check(Replay(image, profile))
    report = dict(schema=1, hardware_execution=False, providers=results,
        source_sha256={k:v[0] for k,v in H['PROVIDERS'].items()},
        runtime_provider_verified=False, physical_mapping_verified=False,
        os_cpu_mapping_verified=False, pm_slot_mapping_verified=False,
        register_identity_or_effect_verified=False, runtime_write_enabled=False,
        limits='Pinned original selector instructions; register read/write replies and status are fixtures. No installer, diagnostics, hardware or host firmware executes. Candidate addresses and bit aliases do not establish physical identities or a usable setting interface.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
