#!/usr/bin/env python3
"""Replay the pinned APOB HOB lookup and record selection in emulated memory.

The cached HOB list, its descriptor and APOB bytes are synthetic. UEFI service
initialization and assertion/reporting paths do not execute. No host CPUID,
port I/O, firmware installation or hardware acquisition is performed.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct
import uuid

import unicorn as u
from unicorn import x86_const as x

HELPERS = runpy.run_path(str(Path(__file__).with_name('firmware-granite-topology-protocol.py')))
GUID = uuid.UUID('30b174f3-7712-4cca-bd13-d0b8a8801997').bytes_le
HOB, APOB, OUT, STACK, STOP = 0x300000, 0x310000, 0x330000, 0x118008, 0xf000
NOT_FOUND, UNSUPPORTED = 0x800000000000000e, 0x8000000000000003
PROFILES = {
    'a': dict(entry=0x2bb0, cache=0x36f0, diagnostic=0x3708, assertion=0x371d,
              ranges=((0x96c, 0x9e8), (0x2ac0, 0x2de4))),
    'b': dict(entry=0x2b20, cache=0x3720, diagnostic=0x3738, assertion=0x374d,
              ranges=((0x9b4, 0xa30), (0x2a30, 0x2d54))),
}


def record(instance, group=3, kind=3, length=456):
    data = bytearray([0xff] * length)
    data[:16] = struct.pack('<4I', group, kind, instance, length)
    return bytes(data)


def container(records, version=5, first=32):
    total = first + sum(map(len, records))
    return struct.pack('<4I', 0x424f5041, version, total, first) + bytes(first-16) + b''.join(records)


class Replay:
    def __init__(self, image, profile):
        self.image, self.profile = image, profile
        self.trace = hashlib.sha256()
        self.cases = 0
        self.seen = set()

    def run(self, data, instance, present=True, supported=1, reported_size=0, stalled=False):
        p = self.profile
        vm = u.Uc(u.UC_ARCH_X86, u.UC_MODE_64)
        for base, size in ((0, 0x10000), (0x100000, 0x20000), (HOB, 0x40000)):
            vm.mem_map(base, size)
        vm.mem_write(0, self.image)
        # These globals are explicit fixtures: cached HOB list already available,
        # no diagnostic callback and assertion reporting disabled. The actual
        # firmware's service/diagnostic behavior is outside this replay.
        vm.mem_write(p['cache'], struct.pack('<Q', HOB))
        vm.mem_write(p['diagnostic'], bytes(8))
        vm.mem_write(p['assertion'], bytes(1))
        prefix = struct.pack('<HHI', 2, 8, 0)
        prefix += struct.pack('<HHI', 4, 40, 0) + bytes(16) + bytes(16)
        if present:
            prefix += struct.pack('<HHI', 4, 40, 0) + GUID
            prefix += struct.pack('<B3xIQ', supported, reported_size, APOB)
        prefix += struct.pack('<HHI', 0xffff, 8, 0)
        vm.mem_write(HOB, prefix)
        vm.mem_write(APOB, data)
        vm.mem_write(OUT, b'\xa5' * 8)
        vm.mem_write(STACK, struct.pack('<Q', STOP))
        vm.mem_write(STACK+0x28, struct.pack('<Q', OUT))
        vm.reg_write(x.UC_X86_REG_RSP, STACK)
        vm.reg_write(x.UC_X86_REG_R8, instance)
        count = 0

        def code(uc, address, size, _):
            nonlocal count
            if address == STOP:
                uc.emu_stop()
                return
            assert any(start <= address < end for start, end in p['ranges']), hex(address)
            raw = bytes(uc.mem_read(address, size))
            assert raw == self.image[address:address+size]
            # Port I/O is prohibited even in the emulator; assertion branches
            # reaching an inline OUT must fail, not be silently accepted.
            assert raw[0] not in (0xe4, 0xe5, 0xe6, 0xe7, 0xec, 0xed, 0xee, 0xef)
            assert raw[:2] != b'\x66\xef'
            self.seen.add(address)
            count += 1

        def write(uc, access, address, size, value, _):
            assert (0x100000 <= address and address+size <= 0x120000) or (OUT <= address and address+size <= OUT+8), hex(address)

        vm.hook_add(u.UC_HOOK_CODE, code)
        vm.hook_add(u.UC_HOOK_MEM_WRITE, write)
        vm.emu_start(p['entry'], STOP+1, count=20000)
        complete = vm.reg_read(x.UC_X86_REG_RIP) == STOP
        assert complete != stalled
        status = vm.reg_read(x.UC_X86_REG_RAX) if complete else None
        pointer = struct.unpack('<Q', vm.mem_read(OUT, 8))[0]
        result = dict(status=status, selected_offset=pointer-APOB if pointer else None,
                      complete=complete, instructions=count)
        self.trace.update(json.dumps(result, sort_keys=True).encode())
        self.cases += 1
        return result


def study(path, key):
    sha = HELPERS['PROVIDERS'][key][0]
    replay = Replay(HELPERS['image'](path, sha), PROFILES[key])
    valid = 0
    for instance, position, version, first, reported_size in itertools.product(
            (0, 1, 0x102, 0xffffffff), range(3), (5, 6, 0xffffffff), (16, 32, 64), (0, 0xffffffff)):
        items = [record(0, 1, 7, 48), record(instance ^ 0x55, 3, 4, 64)]
        items.insert(position, record(instance))
        offset = first + sum(map(len, items[:position]))
        result = replay.run(container(items, version, first), instance, reported_size=reported_size)
        assert result['status'] == 0 and result['selected_offset'] == offset
        valid += 1
    selection = 0
    for ids in ((), (0,), (1,), (0, 1), (0, 0), (1, 0, 1), (0x102, 1, 0x102)):
        for query in (0, 1, 0x102, 0xffffffff):
            result = replay.run(container([record(i) for i in ids]), query)
            matches = [i for i, value in enumerate(ids) if value == query]
            expected = NOT_FOUND if not matches else UNSUPPORTED if len(matches)>1 else 0
            assert result['status'] == expected
            assert result['selected_offset'] == (32+456*matches[0] if expected == 0 else None)
            selection += 1
    failures = 0
    good = container([record(0)])
    for changes in ({'present': False}, {'supported': 0}):
        result = replay.run(good, 0, **changes)
        assert result['status'] == NOT_FOUND and result['selected_offset'] is None
        failures += 1
    for field, value in ((0, 0), (4, 4), (8, 0), (8, 0xffffffff)):
        bad = bytearray(good); struct.pack_into('<I', bad, field, value)
        result = replay.run(bytes(bad), 0)
        assert result['status'] == NOT_FOUND and result['selected_offset'] is None
        failures += 1
    # A zero-length nonmatching record loops forever in this selected firmware
    # path. Bound it in research; never reproduce this walker in the application.
    bad = bytearray(container([record(0, 1, 7, 48)])); struct.pack_into('<I', bad, 32+12, 0)
    replay.run(bytes(bad), 0, stalled=True)
    return dict(source_sha256=sha, cases=replay.cases, valid_selection_cases=valid,
                instance_selection_cases=selection, unavailable_cases=failures,
                bounded_zero_length_stalls=1, executed_instruction_addresses=len(replay.seen),
                trace_sha256=replay.trace.hexdigest(), descriptor_size_used=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('provider-a', 'provider-b', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    args = p.parse_args()
    assert u.__version__ == '2.1.4'
    report = dict(schema=1, hardware_execution=False,
        reference='ASUS ProArt X870E-CREATOR WIFI 2503',
        apob_hob_guid=str(uuid.UUID(bytes_le=GUID)),
        descriptor=dict(hob_payload_offset=24, supported_offset=0, size_offset=4, pointer_offset=8),
        providers={k: study(getattr(args, 'provider_'+k), k) for k in PROFILES},
        synthetic_cached_hob_list=True, uefi_services_executed=False,
        diagnostic_paths_executed=False, hmac_verified=False,
        physical_mapping_verified=False, pm_slot_mapping_verified=False,
        os_cpu_mapping_verified=False, new_field_units=0, new_hardware_writes=0)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('APOB handoff replay passed; no hardware execution or PM/OS binding')


if __name__ == '__main__':
    main()
