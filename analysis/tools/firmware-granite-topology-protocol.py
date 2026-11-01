#!/usr/bin/env python3
"""Replay the reference UEFI topology protocol on synthetic packed records.

Only byte-pinned enumeration/translation helpers execute inside Unicorn.
No installer, firmware service, MSR, host CPUID or target machine is executed.
Stored identifiers are not assumed to be PM slots or physical/OS core IDs.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct

import pefile
import unicorn as u
from unicorn import x86_const as x

PROVIDERS = {
    'a': ('8ea4e45882f053775e87943b20b092d2086787a9500a120fb9f35af62d424295',
          (0x1d28, 0x1d78, 0x1de4, 0x1e7c, 0x1f40, 0x2118)),
    'b': ('c4828555b14a4d8b8e84c7f94f2258b941a778a553cbb7666aac603b251a56b4',
          (0x1d9c, 0x1dec, 0x1e58, 0x1ef0, 0x1fb4, 0x2178)),
}
TOPOLOGY_SHA = '78e0fae1bbe0c4a35d6edaf1e50f0b1c5572066fdf63f9da5b8b59517d93b813'
ORDER = (0, 4, 8, 12, 2, 6, 10, 14, 3, 7, 11, 15, 1, 5, 9, 13)
BAD = 0x8000000000000002
DEVICE_ERROR = 0x8000000000000007
SENTINEL = 0xdeadbeefcafebabe
PROVIDER_BASE, DATA, STACK, STOP = 0x200000, 0x300000, 0x110008, 0xf000
OBJECT, RECORD, OUT = DATA, DATA + 0x1000, DATA + 0x2000


def image(path, expected):
    body = path.read_bytes()
    assert hashlib.sha256(body).hexdigest() == expected
    pe = pefile.PE(data=body)
    assert pe.FILE_HEADER.Machine == 0x8664 and pe.OPTIONAL_HEADER.ImageBase == 0
    for section in pe.sections:
        if section.Characteristics & 0x20000000:
            assert section.PointerToRawData == section.VirtualAddress
    memory = pe.get_memory_mapped_image()
    assert len(memory) < STOP
    return memory


def records(outer_ids, middle_ids=(0, 1), core_ids=tuple(range(8)), threads=(1, 1)):
    # Header + eight 0x33-byte groups; each group has an ID and two
    # 0x19-byte groups, each with an ID and eight (ID, flag, flag) triples.
    data = bytearray(b'\xff' * 0x1c8)
    for i, outer in enumerate(outer_ids):
        data[0x30 + 0x33*i] = outer
        for j, middle in enumerate(middle_ids):
            base = 0x31 + 0x33*i + 0x19*j
            data[base] = middle
            for k, core in enumerate(core_ids):
                data[base+1+3*k:base+4+3*k] = bytes((core, *threads))
    return data


class Replay:
    def __init__(self, provider, topology, functions):
        self.provider, self.topology, self.functions = provider, topology, functions
        self.trace = hashlib.sha256()
        self.seen = set()
        self.cases = 0

    def run(self, entry, args, data, values=None, scope=(1, 1), pointer=True, sorter=False):
        vm = u.Uc(u.UC_ARCH_X86, u.UC_MODE_64)
        for address, size in ((0, 0x10000), (PROVIDER_BASE, 0x10000),
                              (0x100000, 0x20000), (DATA, 0x10000)):
            vm.mem_map(address, size)
        vm.mem_write(0, self.topology)
        vm.mem_write(PROVIDER_BASE, self.provider)
        vm.mem_write(OBJECT+0x40, struct.pack('<Q', PROVIDER_BASE+self.functions[4]))
        vm.mem_write(OBJECT+0x50, struct.pack('<3Q', *scope, RECORD if pointer else 0))
        vm.mem_write(RECORD, bytes(data))
        vm.mem_write(OUT, struct.pack('<32Q', *([SENTINEL]*32)))
        if values is not None:
            vm.mem_write(OUT, struct.pack('<'+'Q'*len(values), *values))
        vm.mem_write(STACK, struct.pack('<Q', STOP))
        vm.reg_write(x.UC_X86_REG_RSP, STACK)
        for reg, value in zip((x.UC_X86_REG_RCX, x.UC_X86_REG_RDX,
                               x.UC_X86_REG_R8, x.UC_X86_REG_R9), args):
            vm.reg_write(reg, value)
        for i, value in enumerate(args[4:]):
            vm.mem_write(STACK+40+8*i, struct.pack('<Q', value))
        diagnostics = []
        start = entry if sorter else PROVIDER_BASE+entry

        def instruction(uc, address, size, _):
            if sorter and address == 0x3acc:
                # Explicit diagnostic fixture: record its argument, then return.
                # Neither the firmware logging/assertion helper nor I/O executes.
                diagnostics.append(uc.reg_read(x.UC_X86_REG_ECX))
                sp = uc.reg_read(x.UC_X86_REG_RSP)
                ret, = struct.unpack('<Q', uc.mem_read(sp, 8))
                uc.reg_write(x.UC_X86_REG_RSP, sp+8)
                uc.reg_write(x.UC_X86_REG_RIP, ret)
                return
            if sorter and 0x54e0 <= address < 0x569a:
                pass
            else:
                assert PROVIDER_BASE+self.functions[0] <= address < PROVIDER_BASE+self.functions[5], hex(address)
            self.seen.add(address)

        def write(uc, access, address, size, value, _):
            assert (0x100000 <= address and address+size <= 0x120000 or
                    OUT <= address and address+size <= OUT+256), hex(address)

        vm.hook_add(u.UC_HOOK_CODE, instruction)
        vm.hook_add(u.UC_HOOK_MEM_WRITE, write)
        vm.emu_start(start, STOP, count=30000)
        assert vm.reg_read(x.UC_X86_REG_RIP) == STOP
        status = vm.reg_read(x.UC_X86_REG_RAX)
        result = list(struct.unpack('<32Q', vm.mem_read(OUT, 256)))
        trace = dict(entry=entry, args=args, data=bytes(data).hex(), values=values,
                     scope=scope, pointer=pointer, sorter=sorter,
                     status=status, result=result, diagnostics=diagnostics)
        self.trace.update(json.dumps(trace, sort_keys=True, separators=(',', ':')).encode())
        self.cases += 1
        return status, result, diagnostics


def check(replay):
    f = replay.functions
    enumerations = translations = errors = sorted_cases = 0
    examples = []
    for count in range(9):
        data = records(tuple((3+2*i) % 16 for i in range(count)))
        status, result, _ = replay.run(f[0], (OBJECT, 0, 0, OUT), data)
        assert status == 0 and result[0] == count
        enumerations += 1
    for outer, middle_count, core_count, thread_count in itertools.product(range(8), range(3), range(9), range(3)):
        data = records(tuple(range(8)), tuple(range(middle_count)), tuple(range(core_count)),
                       (1, 1) if thread_count == 2 else (1, 0) if thread_count == 1 else (0, 1))
        status, result, _ = replay.run(f[1], (OBJECT, 0, 0, outer, OUT), data)
        assert status == 0 and result[0] == middle_count
        enumerations += 1
        if middle_count:
            status, result, _ = replay.run(f[2], (OBJECT, 0, 0, outer, middle_count-1, OUT), data)
            assert status == 0 and result[0] == core_count
            enumerations += 1
            if core_count:
                status, result, _ = replay.run(f[3], (OBJECT, 0, 0, outer, middle_count-1, core_count-1, OUT), data)
                assert status == 0 and result[0] == thread_count
                enumerations += 1
    for rotation, outer, middle, core in itertools.product(range(8), range(8), range(2), range(8)):
        ids = tuple((i+rotation)*2 % 16 for i in range(8))
        data = records(ids, (6, 3), (1, 3, 5, 7, 9, 11, 13, 15))
        values = [0, 0, outer, middle, core]
        args = (OBJECT, OUT, OUT+8, OUT+16, OUT+24, OUT+32)
        status, result, _ = replay.run(f[4], args, data, values)
        assert status == 0 and result[:5] == [0, 0, ids[outer], (6, 3)[middle], 1+2*core]
        translations += 1
    data = records((8, 4), (6, 3), (1, 3, 5))
    # Contiguous optional pointer prefixes are accepted. The first coordinate
    # alone is not range-checked by this function; keep that limitation visible.
    for mask in range(32):
        pointers = tuple(OUT+8*i if mask & (16 >> i) else 0 for i in range(5))
        status, result, _ = replay.run(f[4], (OBJECT, *pointers), data, [0, 0, 1, 1, 2])
        valid = mask in (16, 24, 28, 30, 31)
        assert status == (0 if valid else BAD), mask
        errors += 1
    for function, args in (
        (f[0], (0, 0, 0, OUT)), (f[0], (OBJECT, 1, 0, OUT)),
        (f[0], (OBJECT, 0, 1, OUT)), (f[0], (OBJECT, 0, 0, 0)),
        (f[1], (OBJECT, 0, 0, 8, OUT)),
        (f[2], (OBJECT, 0, 0, 0, 2, OUT)),
        (f[3], (OBJECT, 0, 0, 0, 0, 8, OUT)),
    ):
        status, result, _ = replay.run(function, args, data)
        assert status == BAD and result[0] == SENTINEL
        errors += 1
    status, result, _ = replay.run(f[0], (OBJECT, 0, 0, OUT), data, pointer=False)
    assert status == BAD and result[0] == SENTINEL
    status, result, _ = replay.run(f[4], (OBJECT, OUT, OUT+8, OUT+16, 0, 0), data, [0, 0, 0], pointer=False)
    assert status == DEVICE_ERROR and result[:3] == [0, 0, 0]
    errors += 2
    patterns = [(), (8, 4), (15, 0, 2), (0, 0), (16,), (4, 16), (255,), (254, 4)]
    patterns += [tuple(ORDER[(i+rotation) % 16] for i in range(count))
                 for count, rotation in itertools.product(range(1, 9), range(16))]
    for ids in patterns:
        data = records(ids)
        status, result, diagnostics = replay.run(0x54e0, (OBJECT, 0, 0, len(ids), OUT), data, sorter=True)
        # Invalid sentinel records cause a callback error; the fixture returns
        # from diagnostics and the ordering helper keeps the original ordinal.
        effective = [i if value == 255 else value for i, value in enumerate(ids)]
        expected = [i for key in ORDER for i, value in enumerate(effective) if value == key]
        assert status == 0 and result[:len(expected)] == expected
        assert all(value == SENTINEL for value in result[len(expected):])
        expected_diagnostics = [0x31080079 for value in ids if value == 255]
        if len(expected) != len(ids):
            expected_diagnostics.append(0x31080097)
        assert diagnostics == expected_diagnostics
        if ids in ((8, 4), (16,), (255,), (0, 0)):
            examples.append(dict(stored_ids=ids, returned_ordinals=result[:len(ids)],
                                 status=status, diagnostic_codes=[hex(v) for v in diagnostics]))
        sorted_cases += 1
    return dict(cases=replay.cases, enumeration_cases=enumerations,
        translation_cases=translations, pointer_and_bound_cases=errors,
        ordering_cases=sorted_cases, ordering_examples=examples,
        trace_sha256=replay.trace.hexdigest(), instruction_addresses=len(replay.seen))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('provider-a', 'provider-b', 'topology', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    assert u.__version__ == '2.1.4' and pefile.__version__ == '2024.8.26'
    topology = image(args.topology, TOPOLOGY_SHA)
    outcomes = {}
    for name, (sha, functions) in PROVIDERS.items():
        provider = image(getattr(args, 'provider_'+name), sha)
        outcomes[name] = dict(source_sha256=sha, **check(Replay(provider, topology, functions)))
    report = dict(schema=1, hardware_execution=False, providers=outcomes,
        topology_source_sha256=TOPOLOGY_SHA, protocol_guid='D20482F8-7ECF-4A40-8C11-D326D8153806',
        record_header_bytes=0x30, record_fields_bytes=0x10, record_hmac_bytes=0x20,
        record_hmac_verified=False, groups_base=0x30, outer_stride=0x33, middle_stride=0x19, core_stride=3,
        enumeration_limits=[8, 2, 8, 2], invalid_record_id=255,
        ordering=list(ORDER), ordering_returns='original ordinals sorted by stored identifiers',
        diagnostic_helper_is_fixture=True, installer_and_record_producer_executed=False,
        register_update_slot_executed=False,
        stored_identifier_physical_meaning_verified=False, pm_slot_mapping_verified=False,
        target_model_applicability_verified=False, new_physical_units={}, runtime_write_enabled=False,
        unicorn_version=u.__version__, pefile_version=pefile.__version__)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
