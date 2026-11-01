#!/usr/bin/env python3
"""Replay pinned UEFI provider CPU gates; CPUID inputs are synthetic.

Stop before any accepted-path firmware service or rejected-path epilogue.
No installer, host CPUID, firmware service or hardware access is executed.
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

HELPERS = runpy.run_path(str(Path(__file__).with_name('firmware-granite-topology-protocol.py')))
STACK_BASE, STACK_SIZE, STACK = 0x100000, 0x20000, 0x110008
STOP = 0xf000


class Gate:
    def __init__(self, image, profile):
        self.image, self.profile = image, profile
        self.accept, self.reject = (0x785, 0x897) if profile == 'a' else (0x779, 0x88b)
        self.trace = hashlib.sha256()
        self.cases = self.accepted = self.cpuid_calls = 0
        self.instructions = set()

    def run(self, eax, ebx, ecx=0, edx=0):
        vm = u.Uc(u.UC_ARCH_X86, u.UC_MODE_64)
        vm.mem_map(0, 0x10000)
        vm.mem_map(STACK_BASE, STACK_SIZE)
        vm.mem_write(0, self.image)
        vm.mem_write(STACK, struct.pack('<Q', STOP))
        vm.reg_write(x.UC_X86_REG_RSP, STACK)
        vm.reg_write(x.UC_X86_REG_RCX, 0xabcdef)
        calls = []
        outcome = []

        def instruction(uc, address, size, _):
            if address in (self.accept, self.reject):
                outcome.append(address == self.accept)
                uc.emu_stop()
                return
            assert (0x72c <= address < self.accept or 0x390 <= address < 0x3b7 or
                    self.profile == 'b' and 0x960 <= address < 0x9b3), hex(address)
            self.instructions.add(address)
            if address == 0x395:
                assert bytes(uc.mem_read(address, size)) == b'\x0f\xa2'
                assert uc.reg_read(x.UC_X86_REG_EAX) == 0x80000001
                calls.append(0x80000001)
                for reg, value in zip((x.UC_X86_REG_EAX, x.UC_X86_REG_EBX,
                                       x.UC_X86_REG_ECX, x.UC_X86_REG_EDX),
                                      (eax, ebx, ecx, edx)):
                    uc.reg_write(reg, value)
                uc.reg_write(x.UC_X86_REG_RIP, address + size)

        def write(uc, access, address, size, value, _):
            assert STACK_BASE <= address and address + size <= STACK_BASE + STACK_SIZE, hex(address)

        vm.hook_add(u.UC_HOOK_CODE, instruction)
        vm.hook_add(u.UC_HOOK_MEM_WRITE, write)
        vm.emu_start(0x72c, STOP, count=300)
        assert len(outcome) == 1 and vm.reg_read(x.UC_X86_REG_RIP) in (self.accept, self.reject)
        expected = (eax & 0x0fff0f00) == 0x00b40f00 if self.profile == 'a' else (
            eax & 0x0fff0ff0) in (0x00b60f80, 0x00b60f00)
        expected = expected and (ebx >> 28) == 0
        assert outcome[0] == expected, (self.profile, hex(eax), hex(ebx))
        expected_calls = 1 if self.profile == 'a' or (
            eax & 0x0fff0ff0) == 0x00b60f80 and ebx >> 28 == 0 else 2
        assert len(calls) == expected_calls
        row = dict(eax=eax, ebx=ebx, ecx=ecx, edx=edx, accepted=outcome[0], cpuid_calls=len(calls))
        self.trace.update(json.dumps(row, sort_keys=True, separators=(',', ':')).encode())
        self.cases += 1
        self.accepted += int(outcome[0])
        self.cpuid_calls += len(calls)
        return row

    def check(self):
        # Every extended/base model and package nibble; endpoint steppings.
        for model, package, step in itertools.product(range(256), range(16), (0, 15)):
            eax = 0x00b00f00 | (model >> 4) << 16 | (model & 15) << 4 | step
            self.run(eax, package << 28)
        # Every single-bit change around three signatures, including reserved
        # and ignored bits. These are input fixtures, not machine captures.
        for signature, bit in itertools.product((0x00b40f40, 0x00b60f00, 0x00b60f80), range(32)):
            self.run(signature ^ (1 << bit), 0)
        # Ignored EBX low bits and ECX/EDX are not interpreted by these gates.
        for signature, low, ecx, edx in itertools.product(
                (0x00b40f40, 0x00b60f00, 0x00b60f80), (0, 0x0fffffff),
                (0, 0xffffffff), (0, 0xffffffff)):
            self.run(signature, low, ecx, edx)
        samples = [self.run(signature, package << 28)
                   for signature in (0x00b40f40, 0x00b60f00, 0x00b60f80)
                   for package in (0, 1)]
        return dict(cases=self.cases, accepted=self.accepted, rejected=self.cases-self.accepted,
                    cpuid_calls=self.cpuid_calls, trace_sha256=self.trace.hexdigest(),
                    gate_entry='0x72c', accepted_boundary=hex(self.accept),
                    rejected_boundary=hex(self.reject), cpuid_instruction='0x395',
                    visited_instructions=len(self.instructions), examples=samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provider-a', required=True, type=Path)
    parser.add_argument('--provider-b', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    providers = {}
    for profile, path in (('a', args.provider_a), ('b', args.provider_b)):
        expected = HELPERS['PROVIDERS'][profile][0]
        data = HELPERS['image'](path, expected)
        providers[profile] = Gate(data, profile).check()
        providers[profile]['image_sha256'] = expected
    result = dict(schema=1, firmware='ASUS ProArt X870E-CREATOR WIFI BIOS 2503',
                  providers=providers, hardware_execution=False, host_cpuid_executed=False,
                  cpuid_inputs='synthetic', firmware_services_executed=False,
                  installer_executed=False, user_cpu_capture_verified=False,
                  runtime_provider_verified=False, physical_mapping_verified=False,
                  pm_slot_mapping_verified=False, runtime_write_enabled=False,
                  new_physical_units={},
                  interpretation='The fixture signature 0x00b40f40 with EBX[31:28]=0 passes A and rejects B. Entry eligibility is not proof of protocol installation, PM binding or device effect.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print(json.dumps(dict(output=str(args.output), cases=sum(p['cases'] for p in providers.values()),
                          runtime_provider_verified=False)))


if __name__ == '__main__':
    main()
