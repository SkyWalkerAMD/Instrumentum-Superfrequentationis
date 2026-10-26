#!/usr/bin/env python3
"""Replay pinned SMU 02/A7/A8 instructions against synthetic RAM and helpers.

Verifies version, signed encoding, input range, guard rejection, byte storage
and the MP5 99h argument. Does not emulate MP5 or prove physical power effects.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct

SMU_SHA = 'a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7'
HANDLERS = {2: 0x23a0c, 0xa7: 0x2c2a4, 0xa8: 0x2c2e0}


def replay(body, instructions, command, argument=0, byte=0, guard=True):
    reg = {f'a{i}': 0 for i in range(16)}
    reg['a2'] = 0x1234
    ram = {0xb395: byte}
    response = result = None
    writes, broadcasts, trace = [], [], []
    pc = HANDLERS[command]
    value = lambda a: reg[a] if a in reg else int(a, 0)
    signed = lambda a: a if a < 0x80000000 else a-0x100000000
    for _ in range(40):
        ins = instructions[pc]
        raw = bytes.fromhex(ins['bytes'])
        assert body[pc:pc+len(raw)] == raw
        op, _, operands = ins['text'].partition(' ')
        a = operands.split(',') if operands else []
        next_pc = pc+len(raw)
        trace.append(pc)
        if op == 'entry':
            pass
        elif op in ('mov.n', 'movi', 'movi.n'):
            v = value(a[1])
            # Ghidra prints this MOVI.N's sign-extended 8-bit immediate as f5.
            # Xtensa MOVI.N has a signed -32..95 immediate; f5 represents -11.
            if op == 'movi.n' and 0xe0 <= v <= 0xff:
                v -= 0x100
            reg[a[0]] = v & 0xffffffff
        elif op == 'l32r':
            reg[a[0]] = struct.unpack_from('<I', body, value(a[1]))[0]
        elif op == 'l8ui':
            reg[a[0]] = ram[value(a[1])+value(a[2])]
        elif op == 'sext':
            bits = value(a[2])+1
            v = value(a[1]) & ((1 << bits)-1)
            reg[a[0]] = (v if v < 1 << (bits-1) else v-(1 << bits)) & 0xffffffff
        elif op == 's8i':
            address = value(a[1])+value(a[2])
            assert address == 0xb395
            ram[address] = value(a[0]) & 255
            writes.append((address, ram[address]))
        elif op == 'beqz.n':
            if not value(a[0]):
                next_pc = value(a[1])
        elif op in ('bgei', 'blt'):
            left, right = signed(value(a[0])), signed(value(a[1]))
            if (left >= right) if op == 'bgei' else (left < right):
                next_pc = value(a[2])
        elif op == 'j':
            next_pc = value(a[0])
        elif op == 'call8':
            destination = value(a[0])
            if destination == 0x15a4:
                assert reg['a10'] == 0x1234
                reg['a10'] = argument
            elif destination == 0x321f8:
                assert reg['a10'] == 2
                reg['a10'] = int(guard)
            elif destination == 0x2151c:
                broadcasts.append((reg['a10'], reg['a11']))
            elif destination == 0x158c:
                assert reg['a10'] == 0x1234 and result is None
                result = reg['a11']
            else:
                assert destination == 0x151c and reg['a10'] == 0x1234 and response is None
                response = reg['a11']
        elif op == 'retw.n':
            assert response is not None
            return response, result, ram[0xb395], writes, broadcasts, trace
        else:
            raise ValueError('Unexpected instruction: '+ins['text'])
        pc = next_pc
    raise ValueError('Synthetic instruction budget exceeded')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smu', type=Path, required=True)
    parser.add_argument('--study', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    body = args.smu.read_bytes()
    assert hashlib.sha256(body).hexdigest() == SMU_SHA
    study = json.loads(args.study.read_text(encoding='utf-8'))
    assert study['source_sha256'] == SMU_SHA
    instructions = {}
    for command, entry in HANDLERS.items():
        assert struct.unpack_from('<I', body, 0x7c38+command*8)[0] == entry
        fn = next(f for f in study['functions'] if int(f['entry'], 16) == entry)
        assert not fn['instructions_truncated']
        instructions.update({int(i['address'], 16): i for i in fn['instructions']})
    covered, checksum = set(), hashlib.sha256()
    version = replay(body, instructions, 2)
    assert version[:5] == (1, 0x00732e00, 0, [], [])
    covered.update(version[-1])
    for byte in range(256):
        r = replay(body, instructions, 0xa8, byte=byte)
        expected = (byte if byte < 128 else byte-256) & 0xffffffff
        assert r[:5] == (1, expected, byte, [], [])
        covered.update(r[-1]); checksum.update(json.dumps(r).encode())
    words = sorted({v & 0xffffffff for v in range(-128, 128)} |
                   {0x80, 0xff, 0x100, 0xffffff7f, 0x7fffffff, 0x80000000})
    accepted = rejected = 0
    for word, guard, byte in itertools.product(words, (False, True), (0, 127, 255)):
        r = replay(body, instructions, 0xa7, word, byte, guard)
        signed = word if word < 0x80000000 else word-0x100000000
        if guard and -10 <= signed <= 5:
            assert r[:5] == (1, None, word & 255, [(0xb395, word & 255)], [(0x99, word)])
            readback = replay(body, instructions, 0xa8, byte=r[2])
            assert readback[1] == word
            accepted += 1
        else:
            assert r[:5] == (0xff if guard else 0xfd, None, byte, [], [])
            rejected += 1
        covered.update(r[-1]); checksum.update(json.dumps([word, guard, byte, r]).encode())
    assert covered == set(instructions)
    report = dict(schema=1, source_sha256=SMU_SHA, hardware_execution=False,
        handlers={hex(k): hex(v) for k, v in HANDLERS.items()}, firmware_version='0x00732e00',
        storage='0xb395', shared_scope=True, getter_signed_bits=8, accepted_counts=[-10, 5],
        version_cases=1, getter_cases=256, setter_cases=accepted+rejected,
        accepted=accepted, rejected_without_writes=rejected, covered_instruction_count=len(covered),
        helpers='synthetic mailbox argument/reply, eligibility boolean and MP5 broadcast recorder',
        guard_implementation_emulated=False, mp5_consumption_emulated=False,
        physical_units_verified=False, hardware_effect_measured=False,
        result_trace_sha256=checksum.hexdigest())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Verified 1 version, 256 signed queries, {accepted+rejected} adjustment cases; no hardware execution')


if __name__ == '__main__':
    main()
