#!/usr/bin/env python3
"""Replay the original legacy P-state frequency branch, without hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RSP,
    UC_X86_REG_R15, UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX,
    UC_X86_REG_EFLAGS, UC_X86_REG_XMM0, UC_X86_REG_XMM1)

FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-amd-pstate-frequency.json'
FIXTURE_SHA = 'd70a28e35e8e0d895d40b159d70a1ff2610b9c8f84b40c292c84dff39577befd'
SOURCE_SHA = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
GET_BITS = 0x4b9dc0
ENTRY, STOP = 0x22c890, 0x22c63a


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('P-state fixture identity mismatch')
    fixture = json.loads(data)
    code = bytes.fromhex(fixture['code_hex'])
    assert fixture['original_sha256'] == SOURCE_SHA
    assert len(code) == fixture['size'] == 1340
    assert hashlib.sha256(code).hexdigest() == fixture['code_sha256']
    return fixture


class FrequencyBranch:
    def __init__(self, fixture):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.uc.mem_map(0x22c000, 4096)
        self.uc.mem_write(int(fixture['address'], 16), bytes.fromhex(fixture['code_hex']))
        self.uc.mem_map(0x15cd000, 4096)
        for item in fixture['floating_constants']:
            self.uc.mem_write(int(item['address'], 16), bytes.fromhex(item['bytes']))
        self.uc.mem_map(0x4b9000, 4096)
        self.uc.mem_write(GET_BITS, b'\xc3')  # Synthetic helper return, not original code.
        self.uc.mem_map(0x70000000, 65536)
        self.addresses = {i.address for i in Cs(CS_ARCH_X86, CS_MODE_64).disasm(
            bytes.fromhex(fixture['code_hex']), int(fixture['address'], 16))}
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def hook(self, uc, address, size, context):
        self.steps += 1
        assert self.steps <= 80
        if address == STOP:
            self.finished = True
            uc.emu_stop()
            return
        if address == GET_BITS:
            assert uc.reg_read(UC_X86_REG_RDI) == 0x70000100
            assert uc.reg_read(UC_X86_REG_RSI) == 8 and uc.reg_read(UC_X86_REG_RDX) == 13
            raw = struct.unpack('<I', uc.mem_read(0x70000100, 4))[0]
            uc.reg_write(UC_X86_REG_RAX, (raw >> 8) & 63)
            self.helpers += 1
            return
        assert address in self.addresses
        assert 0x22c890 <= address <= 0x22c8e3 or 0x22c8f8 <= address <= 0x22c95e

    def run(self, fid, divider):
        self.steps = self.helpers = 0
        self.finished = False
        raw = 0xffffc000 | (divider << 8) | fid
        self.uc.mem_write(0x70000100, struct.pack('<I', raw))
        for register, value in ((UC_X86_REG_RAX, 0), (UC_X86_REG_RBX, raw),
            (UC_X86_REG_RSP, 0x70008000), (UC_X86_REG_R15, 0x70000100),
            (UC_X86_REG_EFLAGS, 2), (UC_X86_REG_XMM0, 0), (UC_X86_REG_XMM1, 0)):
            self.uc.reg_write(register, value)
        self.uc.emu_start(ENTRY, 0, count=80)
        assert self.finished and self.helpers == 1
        return struct.unpack('<d', struct.pack('<Q', self.uc.reg_read(UC_X86_REG_XMM0) & ((1 << 64)-1)))[0]


def investigate(fixture):
    machine = FrequencyBranch(fixture)
    accepted = set(range(8, 27)) | set(range(28, 45, 2))
    digest = hashlib.sha256()
    cases = 0
    for fid in range(256):
        for divider in range(64):
            observed = machine.run(fid, divider)
            expected = (20 * fid if divider == 9 else 200 * fid // divider) if divider in accepted else 0
            assert observed == expected, (fid, divider, observed, expected)
            digest.update(struct.pack('<BBd', fid, divider, observed))
            cases += 1
    return dict(schema=1,source_elf_sha256=SOURCE_SHA,fixture_sha256=FIXTURE_SHA,
        cases=cases,result_sha256=digest.hexdigest(),hardware_execution=False,
        scope='Only the original legacy-frequency arithmetic branch; synthetic extraction of bits 13:8. No full original function/GUI/MSR execution.',
        observations=[
            'The original branch accepts DFS 08h..1Ah and even 1Ch..2Ch, and returns zero for the other codes.',
            'DFS 09h divides by 1.25 in the original instructions, whereas AMD OSRR specifies 1.125.',
            'Other accepted codes use FID * 25 / (DFS / 8); the original truncates the result to integer MHz.',
            'FID below 10h is not rejected by this arithmetic branch. The new decoder checks the reserved FID range.'],
        divider9_example=dict(fid=165,original_mhz=machine.run(165,9),documented_mhz=200*165/9))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = investigate(load_fixture())
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(str(report['cases'])+' original P-state frequency branch cases; no hardware')


if __name__ == '__main__':
    main()
