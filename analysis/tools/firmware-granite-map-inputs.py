#!/usr/bin/env python3
"""Reference mapping inputs: original-byte fuse extraction and wide core masks.

The reads are explicit synthetic words in SMU-local address space. Neither a
host SMN alias nor a public acquisition route is inferred from those addresses.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import runpy
import struct

HERE = Path(__file__).parent
B = runpy.run_path(str(HERE/'firmware-granite-slot-binding.py'))
T, Slice = B['T'], B['Slice']
MASK = 0xffffffff


def shift64(body, code, entry, low, high, amount):
    """Execute the two small original 64-bit shift helpers, including SRC.

    SAR uses the architectural right-shift count; SSL stores 32-(count&31).
    Only 0..63 shifts are exercised. Each instruction and literal is checked.
    """
    assert entry in (0x3f840, 0x3f858) and 0 <= amount < 64
    r = {f'a{i}': 0 for i in range(16)}
    r.update(a2=low, a3=high, a4=amount)
    pc, sar, visited = entry, 0, []
    for _ in range(16):
        ins = code[pc]; raw = bytes.fromhex(ins['bytes'])
        assert body[pc:pc+len(raw)] == raw
        op, _, operands = ins['text'].partition(' ')
        op = op.removesuffix('.n')
        a = [x.strip() for x in operands.split(',') if x.strip()]
        v = lambda x: r[x] if x.startswith('a') else int(x, 0)
        nxt = pc+len(raw); visited.append(pc)
        if op == 'entry': pass
        elif op == 'retw': return r['a2'], r['a3'], visited
        elif op == 'ssl': sar = 32-(v(a[0]) & 31)
        elif op == 'ssr': sar = v(a[0]) & 31
        elif op == 'bgei':
            if v(a[0]) >= v(a[1]): nxt = v(a[2])
        elif op == 'src': r[a[0]] = ((v(a[1]) << 32 | v(a[2])) >> sar) & MASK
        elif op == 'sll': r[a[0]] = (v(a[1]) << ((32-sar) & 63)) & MASK
        elif op == 'srl': r[a[0]] = v(a[1]) >> sar
        elif op == 'movi':
            assert a[1] == '0x0'; r[a[0]] = 0
        else: raise ValueError(f'Unmodeled shift instruction {pc:x}: {ins["text"]}')
        pc = nxt
    raise ValueError('Shift helper instruction budget exceeded')


def fuse(body, code, bit_address, width, low, high):
    offset = (bit_address >> 3) & ~3
    reads, calls = [], []
    class FuseSlice(Slice):
        def get(self, address, size=4):
            assert size == 4 and address in (0xf885d000+offset, 0xf885d004+offset)
            value = high if address == 0xf885d004+offset else low
            reads.append((address, value)); return value
    vm = FuseSlice(body, code)
    vm.r.update(a1=0x50000, a2=bit_address, a3=width)
    def run_shift(entry):
        def hook(v):
            args = tuple(v.r[f'a{i}'] for i in (10,11,12))
            result = shift64(body, code, entry, *args)
            v.r['a10'], v.r['a11'] = result[:2]
            calls.append((entry, args, result))
        return hook
    vm.run(0x1e22c, None, hooks={x:run_shift(x) for x in (0x3f840,0x3f858)})
    expected = ((high << 32 | low) >> (bit_address & 31)) & ((1 << width)-1)
    assert vm.r['a2'] == expected
    assert reads == [(0xf885d004+offset,high),(0xf885d000+offset,low)]
    return expected, reads, calls


def presence_cases(body, code):
    rng = random.Random(0x1df6)
    words = [(0,0),(MASK,MASK),(0x3<<22,0),(0,0x3<<22)]
    words += [(1 << i,0) for i in range(32)]
    words += [(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(256)]
    trace=hashlib.sha256(); called=set()
    for low, high in words:
        result, reads, calls = fuse(body,code,0x1df6,2,low,high)
        assert result == (low >> 22) & 3
        called.update(pc for c in calls for pc in c[2][2])
        trace.update(json.dumps([low,high,result,reads,calls],separators=(',',':')).encode())
    # The same helper crosses a word boundary at bit 31; these cases test the
    # wide shift helper rather than assuming its behavior from a host formula.
    boundary=0
    for bit,width,low,high in itertools.product((0,22,30,31),(1,2,8),(0,1,MASK),(0,1,MASK)):
        _,_,calls=fuse(body,code,bit,width,low,high)
        called.update(pc for c in calls for pc in c[2][2]); boundary+=1
    return dict(cases=len(words), boundary_cases=boundary,
        entry='0x1e22c', bit_address='0x1df6', width=2,
        low_word_smu_local_address='0xf885d3bc', high_word_smu_local_address='0xf885d3c0',
        low_word_first_bit=22, high_word_read_before_low=True,
        high_word_does_not_affect_this_two_bit_result=True,
        arithmetic_helpers_executed=['0x3f840','0x3f858'],
        arithmetic_instruction_addresses=[hex(x) for x in sorted(called)],
        host_smn_alias_verified=False, peripheral_values_are_fixtures=True,
        trace_sha256=trace.hexdigest())


def wide_masks(body, code):
    cases=[]
    grid=(0,1,0x81,0xff,0x100,0x8000,0xff00,0xffff)
    trace=hashlib.sha256()
    for present,m0,m1,smt,upper in itertools.product((1,2,3),grid,grid,(0,1),(0,0xffff0000)):
        vm,table,result=B['initialize'](body,code,present,[m0,m1],smt,upper)
        for target in range(2):
            expected=[m0,m1][target] if present & (1 << target) else 0
            assert vm.get(0x8198+2*target,2)==expected
        trace.update(json.dumps([present,m0,m1,smt,upper,result],sort_keys=True,separators=(',',':')).encode())
        cases.append((present,m0,m1,smt,upper))
    return dict(cases=len(cases), table_words_checked=64*len(cases),
        register_reply_low16_stored=True, high16_discarded=True,
        bits8_to15_affect_active_target_rank=True, core_enumeration_uses_low8=True,
        absent_target_mask_not_read=True, trace_sha256=trace.hexdigest())


def vectors(body, code):
    inputs=[(3,0xff,0xff),(2,0xffffffff,0x81),(1,0x81,0xffffffff),
            (3,0,0xff),(3,0x100,1),(3,0xffff0000,0x80000081),
            (3,0xff00,0x8000),(3,0xffffffff,0xffffffff),(1,0,0)]
    result=[]
    for (presence,first,second),smt in itertools.product(inputs,(0,1)):
        masks=[first&0xffff,second&0xffff]
        _,table,record=B['initialize'](body,code,presence,masks,smt)
        result.append(dict(version=0x620205 if presence==3 else 0x620105,
            presence=presence,masks=[first,second],smt=bool(smt),
            active_mask=record['active_mask'],remap=table,
            core_records=record['core_records'],thread_records=record['thread_records']))
    return result


def header(records):
    lines=['// SPDX-License-Identifier: GPL-2.0-only',
        '// Synthetic oracle generated by firmware-granite-map-inputs.py.',
        '// Original-byte replay of SMU 98.84.0 initializer 0x1e8ec; no hardware.',
        '#pragma once','#include <cstdint>',
        'namespace amd_granite_mapping_vectors {',
        'struct Vector { std::uint32_t version,presence,masks[2]; bool smt; std::uint16_t remap[64]; };',
        'static const Vector values[] = {']
    for r in records:
        lines.append('    {0x%x,%d,{0x%08x,0x%08x},%s,{' % (r['version'],r['presence'],*r['masks'],'true' if r['smt'] else 'false'))
        for off in range(0,64,16): lines.append('        '+','.join('0xffff' if v==65535 else str(v) for v in r['remap'][off:off+16])+(',' if off<48 else ''))
        lines.append('    }},')
    return '\n'.join(lines+['};','}',''])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('smu','study','output'): parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--vectors',type=Path)
    args=parser.parse_args(); body=args.smu.read_bytes()
    code=T['instructions'](args.study,body,T['SMU_SHA'])
    assert struct.unpack_from('<II',body,0x115ac)==(0xf885d004,0xf885d000)
    records=vectors(body,code); golden=header(records)
    report=dict(schema=1,source_sha256=T['SMU_SHA'],hardware_execution=False,
        presence=presence_cases(body,code),wide_core_masks=wide_masks(body,code),
        vectors=records,vector_header_sha256=hashlib.sha256(golden.encode()).hexdigest(),
        reference_addresses_are_host_addresses=False,
        public_mask_acquisition_route_verified=False, physical_ccd_mapping_verified=False,
        os_cpu_mapping_verified=False, runtime_write_enabled=False,
        limits='Original instructions execute on explicit synthetic SMU-local words and register replies. No host address alias, native acquisition route, physical target or OS identity is authenticated.')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if args.vectors:
        args.vectors.parent.mkdir(parents=True,exist_ok=True)
        args.vectors.write_text(golden,encoding='utf-8',newline='\n')
    print(json.dumps({k:v for k,v in report.items() if k!='vectors'}))


if __name__=='__main__': main()
