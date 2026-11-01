#!/usr/bin/env python3
"""Replay the pinned UEFI APIC constructor with synthetic CPUID responses.

Unicorn executes only the selected x86 function and its CPUID wrapper in memory.
No UEFI entry point, host CPUID instruction, driver or target machine is run.
The input coordinates are not assumed to be PM slots or physical target IDs.
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

SHA = '78e0fae1bbe0c4a35d6edaf1e50f0b1c5572066fdf63f9da5b8b59517d93b813'


def replay(memory, smt, coordinates):
    vm = u.Uc(u.UC_ARCH_X86, u.UC_MODE_64)
    vm.mem_map(0, 0x20000); vm.mem_write(0, memory)
    vm.mem_map(0x100000, 0x20000)
    stack, stop = 0x110008, 0x1f000
    vm.mem_write(stack, struct.pack('<Q', stop)); vm.reg_write(x.UC_X86_REG_RSP, stack)
    for reg, value in zip((x.UC_X86_REG_RCX,x.UC_X86_REG_RDX,x.UC_X86_REG_R8,x.UC_X86_REG_R9), coordinates):
        vm.reg_write(reg, value)
    vm.mem_write(stack+40, struct.pack('<2Q', *coordinates[4:]))
    queries = []; seen = set()

    def cpuid(uc, _):
        leaf, subleaf = uc.reg_read(x.UC_X86_REG_EAX), uc.reg_read(x.UC_X86_REG_ECX)
        queries.append((leaf, subleaf))
        if leaf == 0xb:
            assert subleaf == 0
            words = (smt, 1 << smt, 1 << 8, 0)
        else:
            assert leaf == 0x80000026 and 0 <= subleaf <= 3
            kind, shift = ((1,smt),(2,3+smt),(4,4+smt),(0,0))[subleaf]
            words = (shift, (1 << shift) if kind else 0, kind << 8 | subleaf, 0)
        for reg, value in zip((x.UC_X86_REG_EAX,x.UC_X86_REG_EBX,x.UC_X86_REG_ECX,x.UC_X86_REG_EDX), words):
            uc.reg_write(reg, value)
        return True  # Replace the instruction; never use the host's CPUID.

    def instruction(uc, address, size, _):
        # Any unexpected branch/call stops, including diagnostic helper 3acc.
        assert 0x526c <= address < 0x53f9 or 0x3a0 <= address < 0x3e0, hex(address)
        seen.add(address)

    vm.hook_add(u.UC_HOOK_INSN, cpuid, None, 1, 0, x.UC_X86_INS_CPUID)
    vm.hook_add(u.UC_HOOK_CODE, instruction)
    vm.emu_start(0x526c, stop, count=2000)
    assert vm.reg_read(x.UC_X86_REG_RIP) == stop
    assert queries == [(0xb,0)]+[(0x80000026,i) for i in range(4)]
    result = vm.reg_read(x.UC_X86_REG_EAX)
    expected = ((coordinates[0] << (4+smt)) + coordinates[2] +
                (coordinates[3] << (3+smt)) + (coordinates[4] << smt) +
                (coordinates[5] if smt else 0))
    assert result == expected
    return result, seen


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('binary','output'): p.add_argument('--'+key, type=Path, required=True)
    a = p.parse_args(); body = a.binary.read_bytes()
    assert hashlib.sha256(body).hexdigest() == SHA
    assert u.__version__ == '2.1.4' and pefile.__version__ == '2024.8.26'
    pe = pefile.PE(data=body)
    assert pe.FILE_HEADER.Machine == 0x8664 and pe.OPTIONAL_HEADER.ImageBase == 0
    assert pe.get_offset_from_rva(0x526c) == 0x526c
    memory = pe.get_memory_mapped_image(); assert len(memory) < 0x1f000
    trace = hashlib.sha256(); cases = 0; instructions = set(); examples = []
    for smt in (0,1):
        # Coordinate 2 is intentionally nonzero in half the fixtures despite
        # the absent type-3 level. The routine does not reject this combination.
        for coords in itertools.product((0,1),(0,),(0,1),(0,1),range(8),(0,1)):
            result, seen = replay(memory,smt,coords)
            trace.update(struct.pack('<8I',smt,*coords,result))
            instructions.update(seen); cases += 1
            if smt and coords in ((0,0,0,0,0,1),(0,0,1,0,0,0),(0,0,0,1,0,0)):
                examples.append(dict(coordinates=coords,apic=result))
    assert [e['apic'] for e in examples] == [1,16,1]
    report = dict(schema=1, source_sha256=SHA, hardware_execution=False,
        entry_file_offset='0x526c', cpuid_wrapper_file_offset='0x3a0', cases=cases,
        instruction_addresses=len(instructions), synthetic_smt_shifts=[0,1],
        synthetic_topology_level_types=[1,2,4,0],
        formula='(coordinate0 << type4_shift) + coordinate2 + (coordinate3 << type2_shift) + (coordinate4 << type1_shift) + (coordinate5 if leaf0B_shift == 1 else 0)',
        coordinate1_checked_zero_in_selected_path=True,
        absent_type3_shift_stays_zero=True, nonzero_coordinate2_is_not_rejected=True,
        collision_examples=examples, cpuid_words_are_fixtures=True,
        constructor_only_not_callers=True, topology_protocol_argument_names_verified=False,
        pm_storage_to_constructor_coordinates_verified=False,
        physical_or_os_core_mapping_verified=False, runtime_write_enabled=False,
        trace_sha256=trace.hexdigest(), unicorn_version=u.__version__, pefile_version=pefile.__version__)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
