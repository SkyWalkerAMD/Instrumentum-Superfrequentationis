#!/usr/bin/env python3
"""Static/synthetic Granite 61h handler research; never a hardware writer.

No rail identity, physical unit, consumer or setter/readback pair is inferred.
The tested semantics are the bounded integer storage performed by this handler.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct

SHA = '5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596'


def replay(body, instructions, argument, enabled, count, cap):
    registers = {'a'+str(i):0 for i in range(16)}
    memory = {0xae68:enabled,0xae67:count,0x13549:cap}
    writes, response, pc = {}, None, 0x2a644
    for _ in range(40):
        instruction = instructions[pc]
        raw = bytes.fromhex(instruction['bytes'])
        assert body[pc:pc+len(raw)] == raw
        op, _, text = instruction['text'].partition(' ')
        a = [s.strip() for s in text.split(',') if s.strip()]
        op = op.removesuffix('.n')
        val = lambda s: registers[s] if s.startswith('a') else int(s,0)
        next_pc = pc+len(raw)
        if op == 'entry': pass
        elif op == 'retw':
            assert response in (1,0xfd)
            return response,writes
        elif op in ('mov','movi'): registers[a[0]] = val(a[1])
        elif op == 'l32r': registers[a[0]] = struct.unpack_from('<I',body,int(a[1],0))[0]
        elif op == 'l8ui': registers[a[0]] = memory[val(a[1])+int(a[2],0)]
        elif op == 'extui': registers[a[0]] = (val(a[1])>>int(a[2],0)) & ((1<<int(a[3],0))-1)
        elif op == 'beqz':
            if not val(a[0]): next_pc = int(a[1],0)
        elif op == 'bgeu':
            if val(a[0]) >= val(a[1]): next_pc = int(a[2],0)
        elif op == 'addx2': registers[a[0]] = 2*val(a[1])+val(a[2])
        elif op == 'minu': registers[a[0]] = min(val(a[1]),val(a[2]))
        elif op == 'moveqz':
            if not val(a[2]): registers[a[0]] = val(a[1])
        elif op == 's16i':
            address = val(a[1])+int(a[2],0)
            assert address not in writes
            writes[address] = val(a[0]) & 65535
        elif op == 'call8':
            target = int(a[0],0)
            if target == 0x15dc: registers['a10'] = argument
            elif target == 0x14ec: response = registers['a11']
            else: raise ValueError('Unmodelled call: '+instruction['text'])
        else: raise ValueError('Unmodelled instruction: '+instruction['text'])
        pc = next_pc
    raise ValueError('Instruction budget exceeded')


def main():
    p = argparse.ArgumentParser()
    for key in ('smu','study','output'): p.add_argument('--'+key,type=Path,required=True)
    args = p.parse_args()
    body = args.smu.read_bytes()
    assert hashlib.sha256(body).hexdigest() == SHA
    study = json.loads(args.study.read_text(encoding='utf-8'))
    assert study['source_sha256'] == SHA
    functions = {int(f['entry'],16):f for f in study['functions']}
    instructions = {int(i['address'],16):i for f in functions.values() for i in f['instructions']}
    u = lambda a:struct.unpack_from('<I',body,a)[0]
    for command, entry in ((0x61,0x2a644),(0x6e,0x2a7c8),(0x70,0x29e54),(0xa5,0x25d8c)):
        assert u(0x6fa4+8*command) == entry
        f = functions[entry]
        assert not f['bad_instruction_truncation'] and not f['instructions_truncated']
        for i in f['instructions']:
            address = int(i['address'],16); raw = bytes.fromhex(i['bytes'])
            assert body[address:address+len(raw)] == raw
    assert u(0x11514) == 0xae30 and u(0x11724) == 0x134c4
    checked = accepted = rejected = second_slot = 0
    cases = itertools.chain(
        itertools.product(range(512),(0,1),(0,1),(1,2,255),(0,80,255),(0,)),
        itertools.product((0,1,79,80,511),(0,1,2,15,255),(0,1,255),(0,1,2,16,255),(0,1,80,255),(0,0xfffe0000)))
    for code, selector, enabled, count, cap, upper in cases:
        argument = upper | selector<<9 | code
        response,writes = replay(body,instructions,argument,enabled,count,cap)
        ok = bool(enabled) and selector < count
        expected = {0xaf28+2*selector:min(code,cap) if cap else code} if ok else {}
        assert response == (1 if ok else 0xfd) and writes == expected
        checked += 1; accepted += int(ok); rejected += int(not ok)
        second_slot += int(ok and selector == 1)
    # A5 reads a mailbox argument into a10, then does not use it before a10 is
    # overwritten with the mailbox index. The output comes from two fixed
    # indexed float loads, not from a target-selected 61h storage element.
    between = [instructions[a]['text'] for a in sorted(instructions) if 0x25d94 <= a < 0x25daf]
    assert all('a10' not in instruction for instruction in between)
    assert instructions[0x25daf]['text'] == 'mov.n a10,a2'
    assert instructions[0x25d97]['text'] == 'l16ui a13,a15,0xb0'
    assert instructions[0x25d9a]['text'] == 'l16ui a14,a15,0x74'
    assert u(0x115c0) == 0x9d20
    # 6Eh's fixed scalar is distinct from 61h's selected halfword array. This
    # does not establish a physical rail getter or prove all possible callers.
    scalar = functions[0x2a7c8]['instructions']
    assert any(i['text'].startswith('lsi ') and i['text'].endswith(',0xb4') for i in scalar)
    assert any(i['text'].endswith(',0x00011514') for i in scalar)
    report = dict(schema=1,source_sha256=SHA,hardware_execution=False,mailbox_helpers_stubbed=True,
        checked=checked,accepted=accepted,rejected=rejected,accepted_second_slot=second_slot,
        command='0x61',handler='0x2a644',selector_bits='16:9',code_bits='8:0',
        guard='byte[0xae68] != 0 and selector < byte[0xae67]',
        clamp='cap = byte[0x13549]; cap == 0 ? code : min(code, cap)',
        halfword_array='0xaf28 + 2 * selector',a5_argument_affects_output=False,
        a5_source='0x9d20 + 4 * ushort[0x9d20 + 0xb0/0x74]',
        command_6e_fixed_float='0xaee4',physical_rail_identity_verified=False,
        physical_units_verified=False,consumer_verified=False,rail_readback_protocol_verified=False,
        runtime_write_enabled=False)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Verified',checked,'synthetic 61h cases; no live setter/readback enabled')


if __name__ == '__main__': main()
