#!/usr/bin/env python3
"""Replay pinned 70h/6Eh handlers with synthetic RAM, fuses and mailbox helpers.

Binary32 arithmetic uses round-to-nearest-even for these finite fixtures.
No hardware/firmware is executed. Broadcast and policy helpers are recorded,
not emulated; readback proves a stored ceiling, never an operating frequency.
The firmware's FP update-store variant is unresolved. Replay both pre- and
post-increment interpretations and compare the shared ceiling independently.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct

SMU_SHA = 'a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7'
HANDLERS = {0x70: 0x30054, 0x6e: 0x30924}
STORAGE = 0xe1fc
f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
bits = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]
floating = lambda x: struct.unpack('<f', struct.pack('<I', x))[0]


def replay(body, instructions, command, argument=0, fuse=1100, profile=0, guard=True, stored=5.5,
           fp_update_mode='pre'):
    assert fp_update_mode in ('pre', 'post')
    reg = {f'a{i}': 0 for i in range(16)}
    fp = {f'f{i}': 0.0 for i in range(16)}
    boolean = {f'b{i}': False for i in range(16)}
    reg['a1'], reg['a2'] = 0x100000, 0x1234
    ram, writes, calls, trace, update_stores = {}, [], [], [], []
    def put(address, value, size=4):
        for i, byte in enumerate(value.to_bytes(size, 'little')): ram[address+i] = byte
    def get(address, size=4):
        return int.from_bytes(bytes(ram.get(address+i, body[address+i] if address+i < len(body) else 0)
                                    for i in range(size)), 'little')
    put(0x8894+0x57, int(profile != 0), 1)
    put(0x320fe00+0x264, 0x80000000 if profile == 1 else 0)
    put(STORAGE, bits(stored))
    response = result = None
    pc = HANDLERS[command]
    loop = None
    value = lambda a: reg[a] if a in reg else int(a, 0)
    signed = lambda a: a if a < 0x80000000 else a-0x100000000
    for _ in range(1000):
        ins = instructions[pc]
        raw = bytes.fromhex(ins['bytes'])
        assert body[pc:pc+len(raw)] == raw
        op, _, operands = ins['text'].partition(' ')
        a = operands.split(',') if operands else []
        next_pc = pc+len(raw); trace.append(pc)
        if op in ('entry', 'memw'): pass
        elif op in ('mov.n', 'movi', 'movi.n'):
            v = value(a[1])
            if op == 'movi.n' and 0xe0 <= v <= 0xff: v -= 0x100
            reg[a[0]] = v & 0xffffffff
        elif op == 'l32r': reg[a[0]] = struct.unpack_from('<I', body, value(a[1]))[0]
        elif op in ('l32i', 'l8ui'): reg[a[0]] = get(value(a[1])+value(a[2]), 1 if op == 'l8ui' else 4)
        elif op in ('addi', 'addi.n', 'sub'):
            reg[a[0]] = (value(a[1])+value(a[2])*( -1 if op == 'sub' else 1)) & 0xffffffff
        elif op == 'wfr': fp[a[0]] = floating(value(a[1]))
        elif op in ('float.s', 'ufloat.s'):
            v = signed(value(a[1])) if op == 'float.s' else value(a[1])
            fp[a[0]] = f32(f32(v)/(1 << value(a[2])))
        elif op == 'const.s': fp[a[0]] = (0.0, 1.0, 2.0, 0.5)[value(a[1])]
        elif op == 'lsi': fp[a[0]] = floating(get(value(a[1])+value(a[2])))
        elif op in ('ssi', 'ssiu'):
            updated = value(a[1])+value(a[2])
            # 0xc003 is SSIU (pre-update) in the generic Ghidra language,
            # but SSIP (post-update) in QEMU's core-de233_fpu definition.
            address = value(a[1]) if op == 'ssiu' and fp_update_mode == 'post' else updated
            put(address, bits(fp[a[0]]))
            writes.append((address, bits(fp[a[0]])))
            if op == 'ssiu':
                reg[a[1]] = updated
                update_stores.append((address, bits(fp[a[0]])))
        elif op in ('add.s', 'sub.s', 'mul.s', 'madd.s'):
            x,y = fp[a[1]],fp[a[2]]
            v = x+y if op == 'add.s' else x-y if op == 'sub.s' else x*y
            if op == 'madd.s': v += fp[a[0]]
            fp[a[0]] = f32(v)
        elif op in ('oeq.s', 'olt.s'):
            boolean[a[0]] = fp[a[1]] == fp[a[2]] if op == 'oeq.s' else fp[a[1]] < fp[a[2]]
        elif op == 'mov.s': fp[a[0]] = fp[a[1]]
        elif op in ('movt.s', 'movf.s'):
            if boolean[a[2]] == (op == 'movt.s'): fp[a[0]] = fp[a[1]]
        elif op == 'round.s':
            v = f32(fp[a[1]] * (1 << value(a[2])))
            assert -2147483648 <= v < 2147483648
            reg[a[0]] = round(v) & 0xffffffff
        elif op in ('bt', 'bf'):
            if boolean[a[0]] == (op == 'bt'): next_pc = value(a[1])
        elif op in ('beqz', 'beqz.n'):
            if not value(a[0]): next_pc = value(a[1])
        elif op in ('beq', 'bnei'):
            if (value(a[0]) == value(a[1])) == (op == 'beq'): next_pc = value(a[2])
        elif op == 'j': next_pc = value(a[0])
        elif op == 'loopgtz':
            count = signed(value(a[0]))
            if count <= 0: next_pc = value(a[1])
            else: loop = [next_pc, value(a[1]), count]
        elif op == 'call8':
            destination = value(a[0]); calls.append((destination,reg['a10'],reg['a11'],reg['a12']))
            if destination == 0x15a4:
                assert reg['a10'] == 0x1234; reg['a10'] = argument
            elif destination == 0x21fd8:
                assert reg['a11'] == 11 and reg['a10'] in (0x33ea,0x33df)
                reg['a10'] = fuse if reg['a10'] == 0x33ea else 0
            elif destination == 0x321f8:
                assert reg['a10'] == 2; reg['a10'] = int(guard)
            elif destination == 0x158c:
                assert reg['a10'] == 0x1234; result = reg['a11']
            elif destination == 0x151c:
                assert reg['a10'] == 0x1234; response = reg['a11']
            elif destination == 0x21214:
                assert reg['a10'] == 0x72 and reg['a11'] == 0x100000 and reg['a12'] == 5
            else: assert destination == 0x30e88  # downstream policy update; not emulated
        elif op == 'retw.n':
            assert response is not None
            return dict(response=response, result=result, stored=get(STORAGE), writes=writes, calls=calls, trace=trace,
                        fp_update_mode=fp_update_mode, fp_update_stores=update_stores)
        else: raise ValueError('Unsupported instruction: '+ins['text'])
        if loop and next_pc == loop[1]:
            loop[2] -= 1
            if loop[2]: next_pc = loop[0]
            else: loop = None
        pc = next_pc
    raise ValueError('Instruction budget exceeded')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smu', required=True, type=Path)
    parser.add_argument('--study', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(); body = args.smu.read_bytes()
    assert hashlib.sha256(body).hexdigest() == SMU_SHA
    study = json.loads(args.study.read_text(encoding='utf-8'))
    assert study['source_sha256'] == SMU_SHA and not study['function_selection_truncated']
    instructions = {}
    for command, entry in HANDLERS.items():
        assert struct.unpack_from('<I', body, 0x7c38+command*8)[0] == entry
        fn = next(f for f in study['functions'] if int(f['entry'],16) == entry)
        assert not fn['instructions_truncated'] and 'halt_baddata' not in fn['pseudocode']
        instructions.update({int(i['address'],16): i for i in fn['instructions']})
    assert struct.unpack_from('<I', body, 0x17d48)[0]+0x8c == STORAGE
    assert struct.unpack_from('<I', body, 0x16e98)[0]+0x1bc == STORAGE
    covered = set(); digest = hashlib.sha256(); accepted = rejected = clamped = 0
    variant_digest = hashlib.sha256(); variant_cases = variant_differences = 0
    policy_base = struct.unpack_from('<I', body, 0x17d88)[0]-0x28
    assert policy_base == 0xe15c and not policy_base <= STORAGE <= policy_base+32*4
    examples=[]
    for fuse,profile,guard in itertools.product((0,1,100,1000,1100,2047),range(3),(False,True)):
        base=f32(f32(fuse)*f32(0.005))
        requests={0,1,4999,5000,5500,6000,0xfffff,0xffffffff}
        for delta in (-1001,-1000,-999,-1,0,1,499,500,501):
            requests.add(max(0,round(base*1000)+delta))
        for requested in sorted(requests):
            r=replay(body,instructions,0x70,requested,fuse,profile,guard)
            post=replay(body,instructions,0x70,requested,fuse,profile,guard,fp_update_mode='post')
            for key in ('response', 'result', 'stored', 'calls', 'trace'):
                assert r[key] == post[key], ('FP update variant changed ceiling/control flow', key)
            for model, start in ((r,policy_base+4),(post,policy_base)):
                expected_addresses=list(range(start,start+32*4,4)) if model['response']==1 else []
                assert [a for a,_ in model['fp_update_stores']] == expected_addresses
                assert all(not a <= STORAGE < a+4 for a,_ in model['fp_update_stores'])
            variant_cases += 1
            variant_differences += r['fp_update_stores'] != post['fp_update_stores']
            variant_digest.update(json.dumps([requested,fuse,profile,guard,post],sort_keys=True).encode())
            desired=f32(f32(requested)*f32(0.001))
            bounded=max(f32(base-1),min(desired,f32(base+0.5)))
            reject=not requested or (profile==2 and not guard and (bounded==0 or f32(bounded-base)>=0))
            expected=base if bounded==0 else bounded
            assert r['response']==(0xfd if reject else 1)
            stores=[w for w in r['writes'] if w[0]==STORAGE]
            if reject:
                assert not stores and not any(c[0] in (0x21214,0x30e88) for c in r['calls'])
                assert r['stored']==bits(5.5)
                rejected+=1
            else:
                assert stores==[(STORAGE,bits(expected))]
                q=replay(body,instructions,0x6e,stored=floating(r['stored']))
                assert q['response']==1 and q['result']==round(f32(expected*1000)) and not q['writes']
                covered.update(q['trace']); accepted+=1
                clamped+=q['result']!=requested
                if fuse==1100 and profile==2 and guard and requested in (1,5000,5500,6000,0xfffff):
                    examples.append(dict(requested_mhz=requested,readback_mhz=q['result']))
            covered.update(r['trace']); digest.update(json.dumps([requested,fuse,profile,guard,r],sort_keys=True).encode())
    report=dict(schema=1,source_sha256=SMU_SHA,hardware_execution=False,
        firmware_version='0x00732e00',handlers={hex(k):hex(v) for k,v in HANDLERS.items()},
        setter_storage='0xe170 + 0x8c',getter_storage='0xe040 + 0x1bc',shared_address=hex(STORAGE),
        setter_cases=accepted+rejected,accepted=accepted,rejected=rejected,readbacks=accepted,
        readbacks_different_from_request=clamped,covered_instructions=len(covered),
        total_handler_instructions=len(instructions),uncovered=[hex(x) for x in sorted(set(instructions)-covered)],
        helpers='synthetic fuse reads, eligibility, mailbox replies; policy update and MP5 broadcast recorded only',
        arithmetic='finite binary32 round-to-nearest-even fixtures; ROUND.S scale 0 nearest-even integer',
        fp_update_variant=dict(resolved=False,base_encoding='0xc003',instruction='0x3018c',
            compared_setter_cases=variant_cases,policy_array_address_differences=variant_differences,
            pre_addresses='0xe160..0xe1dc, 32 words',post_addresses='0xe15c..0xe1d8, 32 words',
            stored_ceiling_and_response_identical=True,
            post_result_trace_sha256=variant_digest.hexdigest(),
            limitation='Both address interpretations are tested; downstream policy helper and MP5 effects remain unmodeled.'),
        limits='No MP5 consumption, physical clock measurement, exceptional floating-point cases, or resolved FP update-store variant',
        examples=examples,result_trace_sha256=digest.hexdigest())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:report[k] for k in ('setter_cases','accepted','rejected','readbacks','covered_instructions','uncovered')}))


if __name__ == '__main__': main()
