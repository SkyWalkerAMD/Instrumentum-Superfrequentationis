#!/usr/bin/env python3
"""Replay the Granite early mask/HOB producer and a separate HOB consumer.

The two masks are independent synthetic register inputs. This deliberately does
not substitute an inverted fuse byte for the SMU initializer's core mask.
Original IA32 TE bytes execute in Unicorn, including the producer's HOB
wrapper and the reference PEI core allocator. PCI windows, PEI/PHIT memory,
HOB lookup, CPUID and IDTR values are fixtures. No host hardware access occurs.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
import uuid

import unicorn as u
from unicorn import x86_const as x


PRODUCER_SHA = '7464250e2d6f8e3268af5da403997d57c86485ef86b25a23bc2b4af6abc65f1f'
CONSUMER_SHA = 'c76d2ac61c511c113406297113d999f9cf7f1827c3b4413029907d5f7966ecb6'
ALLOCATOR_SHA = '7530328588b4a385c8c75d500b59245badc9237c9020a574cc61dcbc8f850a58'
HOB_GUID = uuid.UUID('0cdfa7da-0d0b-41f0-b98b-0f6359c0857f').bytes_le
STACK, STACK_BASE, DATA, STOP = 0x110008, 0x100000, 0x300000, 0x400000
HOB, OUTPUT, IDTR = DATA, DATA + 0x1000, DATA + 0x2004
PHIT, SERVICES = DATA + 0x3000, DATA + 0x4000
PCI = 0xe0000000


class TeImage:
    def __init__(self, path, expected):
        body = path.read_bytes()
        assert hashlib.sha256(body).hexdigest() == expected
        sig, machine, count, subsystem, stripped, entry, code, base = struct.unpack_from('<HHBBHIIQ', body)
        assert sig == 0x5a56 and machine == 0x14c and count == 3 and subsystem == 11
        self.sha256, self.base, self.entry = expected, base, entry
        self.delta = stripped - 40
        self.sections = []
        high = 0
        for n in range(count):
            name, virtual_size, rva, raw_size, raw, _, _, _, _, flags = struct.unpack_from('<8s6I2HI', body, 40 + n*40)
            offset = raw - self.delta
            assert 0 <= offset <= len(body)-raw_size
            self.sections.append((rva, body[offset:offset+raw_size], bool(flags & 0x20000000)))
            high = max(high, rva+max(virtual_size, raw_size))
        self.memory = bytearray(high)
        for rva, data, executable in self.sections:
            self.memory[rva:rva+len(data)] = data

    def install(self, vm):
        begin = self.base & ~4095
        size = (self.base + len(self.memory) - begin + 4095) & ~4095
        vm.mem_map(begin, size)
        vm.mem_write(self.base, bytes(self.memory))

    def instruction(self, vm, address, size, allowed):
        rva = address-self.base
        assert any(lo <= rva < hi for lo, hi in allowed), hex(rva)
        assert any(lo <= rva and rva+size <= lo+len(data) and executable
                   for lo, data, executable in self.sections), hex(rva)
        assert bytes(vm.mem_read(address, size)) == self.memory[rva:rva+size]
        return rva


def vm_for(image):
    vm = u.Uc(u.UC_ARCH_X86, u.UC_MODE_32)
    image.install(vm)
    vm.mem_map(STACK_BASE, 0x20000)
    vm.mem_map(DATA, 0x10000)
    vm.mem_map(STOP, 0x1000)
    vm.mem_write(STACK, struct.pack('<I', STOP))
    vm.reg_write(x.UC_X86_REG_ESP, STACK)
    return vm


def ret(vm, value):
    sp = vm.reg_read(x.UC_X86_REG_ESP)
    pc, = struct.unpack('<I', vm.mem_read(sp, 4))
    vm.reg_write(x.UC_X86_REG_EAX, value)
    vm.reg_write(x.UC_X86_REG_ESP, sp+4)
    vm.reg_write(x.UC_X86_REG_EIP, pc)


class Replay:
    def __init__(self, producer, consumer, allocator):
        self.producer, self.consumer, self.allocator = producer, consumer, allocator
        assert allocator.memory[0x6700:0x6708] == b'PEI SERV'
        assert struct.unpack_from('<I', allocator.memory, 0x6734)[0] == allocator.base+0x32bf
        self.seen = {'producer': set(), 'consumer': set(), 'gate': set(), 'allocator': set()}
        self.trace = hashlib.sha256()
        self.cases = self.reads = self.index_writes = self.consumer_cases = 0
        self.gate_cases = self.gate_accepted = 0
        self.allocator_calls = 0
        self.allocator_cases = []

    def prepare_allocator(self, vm, free_bytes=0x1000, padding=b'\xa5'*4):
        self.allocator.install(vm)
        # Only environment inputs are synthetic. The service pointer references
        # the original PEI table; its original +0x34 slot selects CreateHob.
        vm.mem_write(IDTR-4, struct.pack('<I', SERVICES))
        vm.mem_write(SERVICES, struct.pack('<I', self.allocator.base+0x6700))
        vm.mem_write(SERVICES+0x60, struct.pack('<I', PHIT))
        vm.mem_write(PHIT, struct.pack('<HHI', 1, 0x38, 0))
        vm.mem_write(PHIT+0x20, struct.pack('<3Q', HOB+8+free_bytes, HOB+8, HOB))
        vm.mem_write(HOB, b'\xa5'*0x218)
        vm.mem_write(HOB+0x20c, padding)

    def allocator_case(self, length, free_bytes):
        image = self.allocator
        vm = vm_for(image)
        # vm_for already installed this TE; initialize the same synthetic PHIT
        # without creating a second mapping for its original code sections.
        vm.mem_write(SERVICES+0x60, struct.pack('<I', PHIT))
        vm.mem_write(PHIT+0x20, struct.pack('<3Q', HOB+8+free_bytes, HOB+8, HOB))
        vm.mem_write(HOB, b'\xa5'*0x218)
        vm.mem_write(STACK+4, struct.pack('<4I', SERVICES, 4, length, OUTPUT))
        writes = []

        def instruction(uc, address, size, _):
            self.seen['allocator'].add(image.instruction(uc,address,size,((0x32bf,0x335b),)))

        def write(uc, access, address, size, value, _):
            assert (STACK_BASE <= address and address+size <= STACK_BASE+0x20000 or
                    HOB <= address and address+size <= HOB+0x10000), hex(address)
            writes.append((address,size,value))

        vm.hook_add(u.UC_HOOK_CODE,instruction)
        vm.hook_add(u.UC_HOOK_MEM_WRITE,write)
        vm.emu_start(image.base+0x32bf,STOP,count=100)
        assert vm.reg_read(x.UC_X86_REG_EIP) == STOP
        aligned = (length+7)&0xfff8
        expected = 0x80000002 if length > 0xfff8 else 0x80000009 if free_bytes < aligned else 0
        result = vm.reg_read(x.UC_X86_REG_EAX)
        assert result == expected
        if not result:
            assert struct.unpack('<HHI',vm.mem_read(HOB,8)) == (4,aligned,0)
            assert struct.unpack('<HHI',vm.mem_read(HOB+aligned,8)) == (0xffff,8,0)
            assert struct.unpack('<Q',vm.mem_read(PHIT+0x30,8))[0] == HOB+aligned
            assert struct.unpack('<Q',vm.mem_read(PHIT+0x28,8))[0] == HOB+aligned+8
            if length == 0x20c:
                assert bytes(vm.mem_read(HOB+0x20c,4)) == b'\xa5'*4
        else:
            assert bytes(vm.mem_read(HOB,8)) == b'\xa5'*8
        row = dict(request_bytes=length,free_bytes=free_bytes,response=hex(result),
                   allocated_bytes=aligned if not result else None,
                   original_instruction_writes=len(writes))
        self.allocator_cases.append(row)
        self.trace.update(json.dumps(row,sort_keys=True,separators=(',',':')).encode())

    def gate(self, signature, ebx):
        image = self.producer
        vm = vm_for(image)
        stopped = []
        cpuid = []

        def instruction(uc, address, size, _):
            rva = address-image.base
            if rva in (0x470, 0x657):
                stopped.append(rva)
                uc.emu_stop()
                return
            self.seen['gate'].add(image.instruction(uc, address, size,
                ((0x409, 0x470), (0x4a21, 0x4a59))))
            if rva == 0x4a30:
                assert uc.reg_read(x.UC_X86_REG_EAX) == 0x80000001
                cpuid.append((signature, ebx))
                for reg, value in ((x.UC_X86_REG_EAX, signature), (x.UC_X86_REG_EBX, ebx),
                                   (x.UC_X86_REG_ECX, 0x1234), (x.UC_X86_REG_EDX, 0x5678)):
                    uc.reg_write(reg, value)
                uc.reg_write(x.UC_X86_REG_EIP, address+size)

        def write(uc, access, address, size, value, _):
            assert STACK_BASE <= address and address+size <= STACK_BASE+0x20000, hex(address)

        vm.hook_add(u.UC_HOOK_CODE, instruction)
        vm.hook_add(u.UC_HOOK_MEM_WRITE, write)
        vm.emu_start(image.base+0x409, STOP, count=200)
        accepted = signature & 0x0fff0f00 == 0x00b40f00 and ebx >> 28 == 0
        assert stopped == [0x470 if accepted else 0x657] and len(cpuid) == 1
        self.gate_cases += 1
        self.gate_accepted += int(accepted)
        self.trace.update(struct.pack('<IIB', signature, ebx, accepted))

    def produce(self, presence, disabled, enabled, controls=(0, 0), allocate=True, padding=b'\xa5'*4):
        image = self.producer
        vm = vm_for(image)
        assert len(padding) == 4
        self.prepare_allocator(vm, 0x1000 if allocate else 0, padding)
        vm.mem_map(PCI, 0x1000)
        previous, selected = 0xabcdef01, 0xabcdef01
        words = {0x5d3bc: presence}
        for target in range(2):
            words[0x304a03dc | (target << 25)] = disabled[target]
            words[0x203c0030 | (target << 23)] = controls[target]
            words[0x203c003c | (target << 23)] = enabled[target]
        reads, writes, allocations, services = [], [], [], []

        def instruction(uc, address, size, _):
            if self.allocator.base <= address < self.allocator.base+len(self.allocator.memory):
                rva = self.allocator.instruction(uc,address,size,((0x32bf,0x335b),))
                self.seen['allocator'].add(rva)
                if rva == 0x32bf:
                    sp = uc.reg_read(x.UC_X86_REG_ESP)
                    args = struct.unpack('<4I',uc.mem_read(sp+4,16))
                    assert args[:3] == (SERVICES,4,0x20c)
                    assert STACK_BASE <= args[3] < STACK_BASE+0x20000
                    services.append(args[:3])
                return
            rva = address-image.base
            if rva == 0x8bf:
                args = (uc.reg_read(x.UC_X86_REG_ECX), uc.reg_read(x.UC_X86_REG_EDX))
                assert args == (4, 0x20c)
                allocations.append(args)
            self.seen['producer'].add(image.instruction(uc, address, size,
                ((0x240, 0x25d), (0x260, 0x299), (0x66d, 0x840),
                 (0x8bf,0x8f9), (0x997, 0x9b2), (0x4789, 0x47b2), (0x4917, 0x4940))))
            if rva == 0x8d0:
                uc.mem_write(uc.reg_read(x.UC_X86_REG_EAX),struct.pack('<HI',0x3ff,IDTR))
                uc.reg_write(x.UC_X86_REG_EIP,address+size)

        def read(uc, access, address, size, value, _):
            if address == PCI+0xb8:
                assert size == 4
                uc.mem_write(address, struct.pack('<I', selected))
            elif address == PCI+0xbc:
                assert size == 4 and selected in words
                value = words[selected]
                reads.append((selected, value))
                uc.mem_write(address, struct.pack('<I', value))
            else:
                assert not PCI <= address < PCI+0x1000, hex(address)

        def write(uc, access, address, size, value, _):
            nonlocal selected
            if address == PCI+0xb8:
                assert size == 4
                selected = value
                writes.append(value)
            else:
                assert (STACK_BASE <= address and address+size <= STACK_BASE+0x20000 or
                        HOB <= address and address+size <= HOB+0x20c or
                        HOB+0x210 <= address and address+size <= HOB+0x218 or
                        PHIT+0x28 <= address and address+size <= PHIT+0x38), hex(address)

        vm.hook_add(u.UC_HOOK_CODE, instruction)
        vm.hook_add(u.UC_HOOK_MEM_READ, read)
        vm.hook_add(u.UC_HOOK_MEM_WRITE, write)
        vm.emu_start(image.base+0x66d, STOP, count=10000)
        assert vm.reg_read(x.UC_X86_REG_EIP) == STOP
        assert vm.reg_read(x.UC_X86_REG_EAX) == (0 if allocate else 0x8000000e)
        mask = presence >> 22 & 3
        coordinates = [0x5d3bc]
        for target in range(2):
            if mask & (1 << target):
                coordinates += [0x304a03dc | target << 25, 0x203c0030 | target << 23, 0x203c003c | target << 23]
        assert [a for a, v in reads] == coordinates
        assert writes == [v for a in coordinates for v in (a, previous)]
        assert selected == previous and allocations == [(4, 0x20c)]
        assert services == [(SERVICES,4,0x20c)]
        self.allocator_calls += len(services)
        data = bytes(vm.mem_read(HOB, 0x210)) if allocate else None
        if allocate:
            expected = bytearray(500)
            struct.pack_into('<4I', expected, 0, 2, 8,
                int(any(mask & 1 << t and disabled[t] & 0x100 for t in range(2))), mask)
            flag8 = 0
            for t in range(2):
                f = disabled[t] & 255 if mask & 1 << t else 255
                e = enabled[t] & 255 if mask & 1 << t else 0
                struct.pack_into('<I', expected, 0x10+4*t, f)
                struct.pack_into('<I', expected, 0x40+4*t, e)
                if mask & 1 << t:
                    flag8 |= (~(controls[t] >> 8)) & 1
                for core in range(8):
                    state = 0 if f & 1 << core else 1 if e & 1 << core else 2
                    struct.pack_into('<I', expected, 0x74+4*(t*8+core), state)
            struct.pack_into('<I', expected, 0x70, flag8)
            assert data[:8] == struct.pack('<HHI', 4, 0x210, 0)
            assert data[8:24] == HOB_GUID and data[24:0x20c] == expected
            assert data[0x20c:] == padding
            assert struct.unpack('<HHI',vm.mem_read(HOB+0x210,8)) == (0xffff,8,0)
            assert struct.unpack('<Q',vm.mem_read(PHIT+0x30,8))[0] == HOB+0x210
        else:
            assert bytes(vm.mem_read(HOB,8)) == b'\xa5'*8
        row = dict(presence=presence, disabled=disabled, enabled=enabled,
                   controls=controls, allocated=allocate, reads=reads, padding=padding.hex(),
                   hob_sha256=hashlib.sha256(data).hexdigest() if data else None)
        self.trace.update(json.dumps(row, sort_keys=True, separators=(',', ':')).encode())
        self.cases += 1
        self.reads += len(reads)
        self.index_writes += len(writes)
        return data

    def consume(self, data):
        image = self.consumer
        vm = vm_for(image)
        vm.mem_write(HOB, data)
        vm.mem_write(OUTPUT, b'\xa5'*0x400)
        vm.mem_write(IDTR-4, struct.pack('<I', DATA+0x3000))
        vm.mem_write(STACK+4, struct.pack('<I', OUTPUT))
        located = []

        def instruction(uc, address, size, _):
            rva = address-image.base
            if rva == 0x434a:
                assert bytes(uc.mem_read(uc.reg_read(x.UC_X86_REG_ECX), 16)) == HOB_GUID
                located.append(str(uuid.UUID(bytes_le=HOB_GUID)))
                ret(uc, HOB)
                return
            self.seen['consumer'].add(image.instruction(uc, address, size, ((0x1f7b, 0x20ee),)))
            if rva == 0x1fbb:
                # The original SIDT instruction is a privileged/environment
                # boundary. A synthetic IDTR supplies the services pointer.
                uc.mem_write(uc.reg_read(x.UC_X86_REG_EAX), struct.pack('<HI', 0x3ff, IDTR))
                uc.reg_write(x.UC_X86_REG_EIP, address+size)

        def write(uc, access, address, size, value, _):
            assert STACK_BASE <= address and address+size <= STACK_BASE+0x20000 or OUTPUT <= address and address+size <= OUTPUT+0x400, hex(address)

        vm.hook_add(u.UC_HOOK_CODE, instruction)
        vm.hook_add(u.UC_HOOK_MEM_WRITE, write)
        vm.emu_start(image.base+0x1f7b, image.base+0x20ee, count=4000)
        assert vm.reg_read(x.UC_X86_REG_EIP) == image.base+0x20ee
        assert len(located) == 1
        output = bytes(vm.mem_read(OUTPUT, 0x40))
        mask, = struct.unpack_from('<I', data, 24+0xc)
        assert output[1] == 2 and output[5] == 8
        assert struct.unpack_from('<I', output, 9)[0] == mask
        for target in range(2):
            for core in range(8):
                state, = struct.unpack_from('<I', data, 24+0x74+4*(target*8+core))
                assert output[0xe+16*target+core] == (state if mask & 1 << target else 0xa5)
        self.trace.update(b'consumer:'+output)
        self.consumer_cases += 1
        return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--producer', type=Path, required=True)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--allocator', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--example-hob', type=Path)
    parser.add_argument('--vectors', type=Path)
    args = parser.parse_args()
    replay = Replay(TeImage(args.producer, PRODUCER_SHA), TeImage(args.consumer, CONSUMER_SHA),
                    TeImage(args.allocator, ALLOCATOR_SHA))
    for length,free in ((8,0x1000),(9,0x1000),(0x20b,0x1000),(0x20c,0x1000),
                        (0x20d,0x1000),(0x210,0x1000),(0x211,0x1000),
                        (0x20c,0),(0x20c,527),(0x20c,528),(0xfff8,0x10000),
                        (0xfff9,0x10000),(0xffff,0x10000)):
        replay.allocator_case(length,free)
    examples = []
    # Independent fuse/enabled inputs expose software-disabled and conflicting
    # states. Full-byte variation plus one-bit enabled bases exercise every bit.
    for disabled, enabled, target in itertools.product(range(256), (0, 1, 2, 4, 8, 16, 32, 64, 128, 255), range(2)):
        fs, es = [0, 0], [0, 0]
        fs[target], es[target] = disabled, enabled
        data = replay.produce((1 << target) << 22, fs, es)
        replay.consume(data)
        if target == 0 and (disabled, enabled) in ((0, 0), (0, 255), (0x81, 255), (0x81, 0x7e)):
            examples.append(dict(disabled=disabled, enabled=enabled,
                states=list(struct.unpack_from('<8I', data, 24+0x74))))
    # Keep presence, high bits, fuse bit 8, and control bits 8/10 independent.
    for presence, f0, f1, e0, e1, flag in itertools.product(range(4), (0, 0x1ff), (0, 0x181),
            (0, 0xff), (0, 0x81), (0, 0x100, 0x400, 0x500)):
        data = replay.produce((presence << 22) | 0xff3fffff,
            [f0 | 0xffff0000, f1 | 0xffff0000], [e0 | 0xabcdef00, e1 | 0x12345600], [flag, flag ^ 0x100])
        replay.consume(data)
    for presence in range(4):
        replay.produce(presence << 22, [0, 0], [255, 255], allocate=False)
    for model, package, step in itertools.product(range(256), range(16), (0, 15)):
        replay.gate(0x00b00f00 | (model & 0xf0) << 12 | (model & 15) << 4 | step,
                    package << 28 | 0x1234567)
    for bit in range(32):
        replay.gate(0x00b40f40 ^ 1 << bit, 0)
        replay.gate(0x00b40f40, 1 << bit)
    vectors = []
    for name, p, fs, es, cs, padding in (
        ('sparse-two-targets', 3, [0x180, 3], [0x3f, 0xfc], [0x100, 0x400], b'\0'*4),
        ('target-one-overlap', 2, [0x1ff, 0x80], [0xffff, 0x81], [0, 0x100], bytes.fromhex('5aa5ff00')),
        ('no-target-present', 0, [0x1ff, 0x1ff], [0xffffffff, 0xffffffff], [0, 0], b'\xa5'*4),
        ('enabled-high-byte-is-lost', 1, [0x100, 0], [0x100, 0xffffffff], [0x100, 0], bytes.fromhex('11223344'))):
        data = replay.produce(p << 22, fs, es, cs, padding=padding)
        replay.consume(data)
        vectors.append((name, data))
    example = vectors[0][1]
    if args.example_hob:
        args.example_hob.parent.mkdir(parents=True, exist_ok=True)
        args.example_hob.write_bytes(example)
    vector_header = make_vectors(vectors)
    if args.vectors:
        args.vectors.parent.mkdir(parents=True, exist_ok=True)
        args.vectors.write_bytes(vector_header.encode('utf-8'))
    report = dict(producer_sha256=PRODUCER_SHA, consumer_sha256=CONSUMER_SHA, allocator_sha256=ALLOCATOR_SHA,
        machine='IA32', producer_image_base=hex(replay.producer.base),
        producer_rva_minus_file_offset=hex(replay.producer.delta),
        consumer_image_base=hex(replay.consumer.base), consumer_rva_minus_file_offset=hex(replay.consumer.delta),
        allocator_image_base=hex(replay.allocator.base), allocator_rva_minus_file_offset=hex(replay.allocator.delta),
        allocator_entry='0x32bf', allocator_service_table='0x6700', allocator_service_slot='0x34',
        allocator_calls=replay.allocator_calls, allocator_cases=replay.allocator_cases,
        hob_guid=str(uuid.UUID(bytes_le=HOB_GUID)), hob_bytes=528, request_bytes=524, allocated_bytes=528,
        payload_bytes=500, padding_bytes=4, padding_initialized=False,
        producer_entry='0x66d', consumer_entry='0x1f7b', consumer_stop='0x20ee',
        producer_cases=replay.cases, consumer_cases=replay.consumer_cases,
        gate_cases=replay.gate_cases, gate_accepted=replay.gate_accepted,
        register_reads=replay.reads, index_writes=replay.index_writes,
        instructions={k:len(v) for k,v in replay.seen.items()}, trace_sha256=replay.trace.hexdigest(),
        examples=examples, register_data_writes=0, pci_index_restored=True,
        independent_mask_inputs=True, inverted_fuse_substitution_verified=False,
        example_hob_sha256=hashlib.sha256(example).hexdigest(),
        oracle_vectors=len(vectors), oracle_header_sha256=hashlib.sha256(vector_header.encode()).hexdigest(),
        hob_omits_core_mask_bits_8_to_15=True, hob_control_bit_is_8_not_mapper_smt_bit_10=True,
        hob_allocator_is_fixture=False, pei_services_pointer_and_phit_are_fixtures=True,
        producer_idtr_is_fixture=True, hob_lookup_is_fixture=True, consumer_idtr_is_fixture=True,
        cpuid_is_fixture=True, gate_stops_before_initializers_and_firmware_services=True,
        hardware_execution=False, runtime_provider_verified=False,
        physical_ccd_mapping_verified=False, apic_mapping_verified=False,
        os_cpu_mapping_verified=False, apob_binding_verified=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


def make_vectors(vectors):
    lines = ['// SPDX-License-Identifier: GPL-2.0-only',
        '// Generated from original IA32 instructions by firmware-granite-map-binding.py.',
        '// Producer and PEI allocator execute original instructions; RAM/register inputs are synthetic.',
        '// Complete 528-byte HOB: 524-byte request, 500-byte payload, 4 undefined padding bytes.',
        '// Producer SHA-256: '+PRODUCER_SHA,
        '// Allocator SHA-256: '+ALLOCATOR_SHA, '#pragma once', '#include <array>',
        '#include <cstdint>', '#include <string>', '#include <vector>',
        'namespace granite_core_status_fixture {',
        'struct Vector {', '    const char *name, *hobHex;',
        '    unsigned presenceMask, fuseFlag, inverseControlFlag;',
        '    std::array<unsigned,2> disabledMasks, enabledMasks;',
        '    std::array<std::array<unsigned,8>,2> states;', '};',
        'static const Vector vectors[] = {']
    for name, data in vectors:
        payload = data[24:]
        values = lambda offset, count: struct.unpack_from('<'+'I'*count, payload, offset)
        array = lambda vals: '{{'+','.join(map(str, vals))+'}}'
        lines.append('    {"'+name+'",')
        raw = data.hex()
        lines.extend('        "'+raw[i:i+96]+'"' for i in range(0, len(raw), 96))
        lines[-1] += ','
        lines.append('        '+','.join(map(str, (values(12,1)[0], values(8,1)[0], values(0x70,1)[0])))+',')
        lines.append('        '+array(values(0x10,2))+','+array(values(0x40,2))+',')
        lines.append('        {{'+array(values(0x74,8))+','+array(values(0x94,8))+'}}},')
    lines += ['};', 'inline std::vector<std::uint8_t> bytes(const Vector &v) {',
        '    const std::string hex(v.hobHex); std::vector<std::uint8_t> out;',
        '    for(std::size_t i=0;i<hex.size();i+=2)',
        '        out.push_back(std::uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));',
        '    return out;', '}', '}']
    return '\n'.join(lines)+'\n'


if __name__ == '__main__':
    main()
