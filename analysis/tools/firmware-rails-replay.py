#!/usr/bin/env python3
"""Replay the pinned SMU 61h handler with synthetic arguments and RAM only.

This proves field extraction, guards and stored words, not physical rail names,
voltage units, host mailbox routing, downstream consumption or hardware effects.
The two mailbox helper calls are replaced with an argument/response fixture.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct

SMU_SHA = 'a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7'


def replay(body, instructions, argument, active, count, clamp):
    reg = {f'a{i}': 0 for i in range(16)}
    reg['a2'] = 0x1234  # Opaque synthetic message context.
    ram = {0xe13c: active, 0xe13b: count, 0x917b: clamp}
    writes = {}
    response = None
    pc = 0x307a8
    trace = []
    value = lambda a: reg[a] if a in reg else int(a, 0)
    for _ in range(48):
        ins = instructions[pc]
        raw = bytes.fromhex(ins['bytes'])
        assert body[pc:pc+len(raw)] == raw
        op, _, operands = ins['text'].partition(' ')
        a = operands.split(',') if operands else []
        next_pc = pc+len(raw)
        trace.append(pc)
        if op == 'entry':
            pass
        elif op in ('mov.n', 'movi.n', 'movi'):
            reg[a[0]] = value(a[1])
        elif op == 'l32r':
            reg[a[0]] = struct.unpack_from('<I', body, value(a[1]))[0]
        elif op == 'extui':
            reg[a[0]] = (value(a[1]) >> value(a[2])) & ((1 << value(a[3]))-1)
        elif op == 'l8ui':
            reg[a[0]] = ram[value(a[1])+value(a[2])]
        elif op == 'moveqz':
            if value(a[2]) == 0:
                reg[a[0]] = value(a[1])
        elif op == 'beqz.n':
            if value(a[0]) == 0:
                next_pc = value(a[1])
        elif op == 'bgeu':
            if value(a[0]) >= value(a[1]):
                next_pc = value(a[2])
        elif op == 'addi':
            reg[a[0]] = (value(a[1])+value(a[2])) & 0xffffffff
        elif op == 'addx2':
            reg[a[0]] = (value(a[1])*2+value(a[2])) & 0xffffffff
        elif op == 'minu':
            reg[a[0]] = min(value(a[1]), value(a[2]))
        elif op == 's16i':
            address = value(a[1])+value(a[2])
            assert address % 2 == 0 and 0xe248 <= address < 0xe258 and address not in writes
            writes[address] = value(a[0]) & 0xffff
        elif op == 'j':
            next_pc = value(a[0])
        elif op == 'call8':
            assert reg['a10'] == 0x1234
            if value(a[0]) == 0x15a4:
                reg['a10'] = argument
            else:
                assert value(a[0]) == 0x151c and response is None
                response = reg['a11']
        elif op == 'retw.n':
            assert response is not None
            return response, writes, trace
        else:
            raise ValueError('Unexpected instruction: '+ins['text'])
        pc = next_pc
    raise ValueError('Synthetic instruction budget exceeded')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--smu', type=Path, required=True)
    p.add_argument('--study', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    body = args.smu.read_bytes()
    assert hashlib.sha256(body).hexdigest() == SMU_SHA
    study = json.loads(args.study.read_text(encoding='utf-8'))
    assert study['source_sha256'] == SMU_SHA
    fn = next(f for f in study['functions'] if f['entry'] == '000307a8')
    instructions = {int(i['address'], 16): i for i in fn['instructions']}
    words = (0, 1, 191, 255, 256, 511)
    axes = ((0, 1), range(5), (0, 1, 2, 3, 4, 5, 255), words, words, (0, 1, 191, 255), (0, 0xfc000000))
    accepted = rejected = 0
    covered = set()
    checksum = hashlib.sha256()
    for active, count, index, first, second, clamp, high in itertools.product(*axes):
        argument = high | (second << 17) | (index << 9) | first
        response, writes, trace = replay(body, instructions, argument, active, count, clamp)
        covered.update(trace)
        if active and index < count:
            v0, v1 = first, second if second else first
            if clamp:
                v0, v1 = min(v0, clamp), min(v1, clamp)
            assert response == 1 and writes == {0xe248+2*index: v0, 0xe250+2*index: v1}
            accepted += 1
        else:
            assert response == 0xfd and not writes
            rejected += 1
        checksum.update(json.dumps([argument, active, count, clamp, response, sorted(writes.items()), trace]).encode())
    assert covered == set(instructions), (covered, set(instructions))
    report = dict(schema=1, source_sha256=SMU_SHA, function='0x307a8', command='0x61',
        hardware_execution=False, mailbox_helpers='synthetic argument and response only',
        physical_rails_verified=False, voltage_units_verified=False, downstream_consumption_verified=False,
        active_values=list(axes[0]), state_counts=list(axes[1]), indices=list(axes[2]),
        rail_word_values=list(words), clamp_values=list(axes[5]), ignored_high_bits=list(axes[6]),
        cases=accepted+rejected, accepted=accepted, rejected_without_writes=rejected,
        covered_instruction_count=len(covered), result_trace_sha256=checksum.hexdigest(),
        active_address='0xe13c', count_address='0xe13b', clamp_address='0x917b',
        first_array='0xe248', second_array='0xe250', runtime_write_enabled=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Verified {accepted+rejected} synthetic rails cases, {len(covered)} instructions; no hardware execution')


if __name__ == '__main__':
    main()
