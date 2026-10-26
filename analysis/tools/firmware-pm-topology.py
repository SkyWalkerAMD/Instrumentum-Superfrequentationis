#!/usr/bin/env python3
"""Byte-checked synthetic replay of the Shimada PM CCD ordinal initializer.

No firmware or hardware execution. Replay both FP update-address variants;
neither affects the packet's identity/width/ordinal fields. Lock and mailbox
helpers are fixtures. A5's presence predicate is checked separately from its
frequency-refresh helpers; the latter are NOT a rail readback.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct

SMU_SHA = 'a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7'
MP5_SHA = 'b5e9c94ef3a27d55a95c1a0d703cc7041b734f9475d44b8eecd49a15794cdeaf'


def instructions(path, body, entries):
    study = json.loads(path.read_text(encoding='utf-8'))
    assert study['source_sha256'] == hashlib.sha256(body).hexdigest()
    result = {}
    for entry in entries:
        f = next(f for f in study['functions'] if int(f['entry'], 16) == entry)
        assert not f['instructions_truncated']
        for i in f['instructions']:
            a = int(i['address'], 16)
            raw = bytes.fromhex(i['bytes'])
            assert body[a:a+len(raw)] == raw
            result[a] = i
    return result


def replay(body, code, present, active, mode):
    reg = {f'a{i}': 0 for i in range(16)}
    reg['a1'] = 0x100000
    ram, remote, fp, trace = {}, {}, {}, []
    def put(a, v, size=4):
        for n in range(size): ram[a+n] = (v >> (8*n)) & 255
    def get(a, size=4):
        return sum(ram.get(a+n, body[a+n] if a+n < len(body) else 0) << (8*n) for n in range(size))
    literal = lambda a: struct.unpack_from('<I', body, a)[0]
    put(0x7444, present, 2); put(0x7446, active, 2)
    put(0x8894+0x45, present.bit_count(), 1)
    put(literal(0x16e80)+0xe1, 1, 1)  # avoid unrelated fuse-read helper
    put(literal(0x16e7c), 1, 1)
    for n in range(16):
        put(0x7444+8+4*n, 0x19b80)
        put(0x8894+0x58+2*n, n & 1, 2)
    value = lambda x: reg[x] if x in reg else int(x, 0)
    pc, loop, shift = 0x215a8, None, 0
    for _ in range(20000):
        if pc == 0x2182f:
            return remote, trace
        ins = code[pc]
        op, _, operands = ins['text'].partition(' ')
        a = [s.strip() for s in operands.split(',') if s.strip()]
        next_pc = pc + len(bytes.fromhex(ins['bytes']))
        op = op.removesuffix('.n'); trace.append(pc)
        if op in ('entry', 'memw'): pass
        elif op in ('mov', 'movi'): reg[a[0]] = value(a[1]) & 0xffffffff
        elif op == 'l32r': reg[a[0]] = literal(value(a[1]))
        elif op in ('l32i', 'l16ui', 'l8ui'):
            reg[a[0]] = get(value(a[1])+value(a[2]), {'l32i':4, 'l16ui':2, 'l8ui':1}[op])
        elif op in ('s32i', 's8i'):
            address = (value(a[1])+value(a[2])) & 0xffffffff
            size = 4 if op == 's32i' else 1
            put(address, value(a[0]), size)
            if 0xf9000000 <= address < 0xfa000000:
                assert op == 's32i' and address not in remote
                remote[address] = value(a[0])
        elif op in ('add', 'addi', 'addmi', 'and', 'or', 'slli'):
            x, y = value(a[1]), value(a[2])
            reg[a[0]] = (x+y if op in ('add','addi','addmi') else x&y if op=='and' else x|y if op=='or' else x<<y) & 0xffffffff
        elif op in ('moveqz', 'movnez'):
            if (value(a[2]) == 0) == (op == 'moveqz'): reg[a[0]] = value(a[1])
        elif op == 'extui': reg[a[0]] = (value(a[1]) >> value(a[2])) & ((1 << value(a[3]))-1)
        elif op == 'ssl': shift = value(a[0]) & 31
        elif op == 'sll': reg[a[0]] = (value(a[1]) << shift) & 0xffffffff
        elif op in ('beqz', 'bnez'):
            if (value(a[0]) == 0) == (op == 'beqz'): next_pc = value(a[1])
        elif op in ('bany', 'bnone'):
            if bool(value(a[1]) & value(a[2])) == (op == 'bany'): next_pc = value(a[0])
        elif op == 'j': next_pc = value(a[0])
        elif op == 'loopgtz':
            assert loop is None
            if value(a[0]): loop = [next_pc, value(a[1]), value(a[0])]
            else: next_pc = value(a[1])
        elif op in ('lsiu', 'ssiu'):
            updated = (value(a[1])+value(a[2])) & 0xffffffff
            address = updated if mode == 'pre' else value(a[1])
            if op == 'lsiu': fp[a[0]] = get(address)
            else: put(address, fp[a[0]])
            reg[a[1]] = updated
        elif op == 'call8':
            # Lock/unlock only. Never execute the firmware or peripherals.
            assert value(a[0]) in (0x3e274, 0x3e384)
        else: raise ValueError(ins)
        if loop and next_pc == loop[1]:
            loop[2] -= 1
            if loop[2]: next_pc = loop[0]
            else: loop = None
        pc = next_pc
    raise ValueError('Instruction budget exceeded')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu','mp5','smu-study','mp5-study','output'): p.add_argument('--'+name, type=Path, required=True)
    args = p.parse_args()
    smu, mp5 = args.smu.read_bytes(), args.mp5.read_bytes()
    assert hashlib.sha256(smu).hexdigest() == SMU_SHA
    assert hashlib.sha256(mp5).hexdigest() == MP5_SHA
    code = instructions(args.smu_study, smu, (0x215a8,0x21910,0x2f828))
    producer = instructions(args.mp5_study, mp5, (0xfdc,0x37d40))
    u = lambda a: struct.unpack_from('<I', smu, a)[0]
    assert u(0x7c38+0xa5*8) == 0x2f828 and u(0x16e54) == 0x7444
    assert struct.unpack_from('<I', mp5, 0x18630)[0] == 0x19b80
    for pc, text in {
        0x2f833:'l16ui a8,a8,0x0', 0x2f836:'extui a3,a10,0x0,0x4',
        0x2f839:'ssr a3', 0x2f83c:'sra a8,a8', 0x2f83f:'bbci a8,0x0,0x0002f88e',
        0x2f887:'movi.n a11,0x1', 0x2f890:'movi a11,0xfe',
    }.items(): assert code[pc]['text'] == text
    assert producer[0x37d49]['text'] == 'l8ui a2,a5,0xd'
    assert producer[0x37d4f]['text'] == 'l8ui a8,a5,0x14'
    # MP5 requests its 0x70-byte packet; the SMU records each destination and
    # initializes the packets once the expected present targets have arrived.
    assert struct.unpack_from('<I', mp5, 0xbb8)[0] == 0x19b80
    for pc, text in {0x1052:'l32r a4,0x00000bb8',0x1055:'movi a11,0x70',
                     0x105d:'movi.n a10,0x3',0x105f:'mov.n a11,a4',
                     0x1061:'call8 0x00036c88'}.items(): assert producer[pc]['text'] == text
    for pc, text in {0x2191a:'addi a9,a2,-0x6',0x21926:'l16ui a8,a12,0x0',
                     0x21937:'addx4 a13,a9,a12',0x2193a:'s32i.n a10,a13,0x8',
                     0x21942:'bne a14,a11,0x0002195a',0x21945:'call8 0x000215a8'}.items(): assert code[pc]['text'] == text
    rng = random.Random(0x732e00)
    masks = {0,0xffff,0x8001,0xaaaa,0x5555,0x1084}
    masks.update(1 << n for n in range(16))
    masks.update(0xffff ^ (1 << n) for n in range(16))
    masks.update(rng.randrange(65536) for _ in range(64))
    count, digest, coverage, examples = 0, hashlib.sha256(), set(), []
    for present in sorted(masks):
        ids = [n for n in range(16) if present & (1 << n)]
        for active in sorted({0, present, present & 0x5555, present & 0xaaaa}):
            for mode in ('pre','post'):
                writes, trace = replay(smu, code, present, active, mode)
                assert len(writes) == len(ids)*28
                actual = []
                for ordinal, target in enumerate(ids):
                    address = 0xf8000000 + ((target+16) << 20) + 0x19b80
                    packet = b''.join(writes[address+4*n].to_bytes(4,'little') for n in range(28))
                    assert packet[0] == target and packet[0x14] == ordinal
                    assert packet[2] == (active & ((1 << target)-1)).bit_count()
                    assert packet[0xd] == len(ids)
                    assert int.from_bytes(packet[0x38:0x3c], 'little') == 0x1abc0
                    actual.append(dict(slot=ordinal,firmware_ccd=target,active_ordinal=packet[2]))
                digest.update(json.dumps([present,active,mode,actual,trace],sort_keys=True).encode())
                coverage.update(trace); count += 1
                if present == 0x8001 and active == present and mode == 'post': examples = actual
    result = dict(schema=1,hardware_execution=False,smu_sha256=SMU_SHA,mp5_sha256=MP5_SHA,
        firmware_version='0x00732e00',cases=count,covered_instructions=len(coverage),
        initializer='0x215a8..0x2182f',presence_command='0xa5',presence_handler='0x2f828',
        present_mask_address='0x7444',active_mask_address='0x7446',
        packet=dict(bytes=0x70,firmware_target=0,active_ordinal=2,table_width=13,present_ordinal=20,pm_destination=56),
        presence_responses=dict(present=1,absent=254),mapping='ascending set bits of present mask',
        fp_update_variants_tested=['pre','post'],mapping_identical_between_fp_variants=True,
        example_sparse=examples,physical_cpu_mapping_verified=False,core_enable_mask_verified=False,
        frequency_helpers_emulated=False,second_rail_readback=False,result_trace_sha256=digest.hexdigest())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Verified',count,'synthetic PM CCD mapping cases; no hardware execution')


if __name__ == '__main__': main()
