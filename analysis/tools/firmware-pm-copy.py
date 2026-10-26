#!/usr/bin/env python3
"""Replay the MP5 integer copy slice against synthetic RAM, never hardware.

Ghidra's pseudocode drops Xtensa zero-overhead loops. Use its raw-byte-checked
instructions instead. Floating-point normalization is deliberately excluded;
this proves addresses/storage widths, not units, labels or physical topology.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MP5_SHA = 'b5e9c94ef3a27d55a95c1a0d703cc7041b734f9475d44b8eecd49a15794cdeaf'
SMU_SHA = 'a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7'


def replay(body, instructions, slots, slot):
    registers = {'a'+str(i): 0 for i in range(16)}
    registers.update(a1=0x50000, a2=slots, a5=0x19b80)
    ram = {0x50004: slot, 0x19b80+0x38: 0x1abc0}
    for i in range(157):
        ram[0x1ade4+4*i] = 0x3f000000+i
    writes, trace = {}, []
    pc, loop = 0x37da5, None

    def read(address):
        if address in ram:
            return ram[address]
        if 0 <= address <= len(body)-4:
            return struct.unpack_from('<I', body, address)[0]
        raise ValueError('Unmapped synthetic read: '+hex(address))

    for _ in range(10000):
        if pc == 0x37e52:
            assert len(writes) == 157
            return writes, trace
        ins = instructions[pc]
        raw = bytes.fromhex(ins['bytes'])
        assert raw == body[pc:pc+len(raw)]
        op, _, operands = ins['text'].partition(' ')
        a = [v.strip() for v in operands.split(',') if v.strip()]
        op = op.removesuffix('.n')
        next_pc = pc+len(raw)
        value = lambda x: registers[x] if x.startswith('a') else int(x, 0)
        if op in ('mov', 'movi'):
            registers[a[0]] = value(a[1])
        elif op == 'l32r':
            registers[a[0]] = read(int(a[1], 0))
        elif op == 'l32i':
            registers[a[0]] = read(value(a[1])+int(a[2], 0))
        elif op == 's32i':
            address = value(a[1])+int(a[2], 0)
            if address >= 0x1300000:
                offset = address-0x1300000-0x1abc0+0x634
                assert offset not in writes
                writes[offset] = value(a[0])
            else:
                assert 0x50000 <= address < 0x50030
                ram[address] = value(a[0])
        elif op in ('add', 'addi', 'and', 'mull', 'slli', 'addx4', 'addx8'):
            x, y = value(a[1]), value(a[2])
            z = {'add':lambda:x+y, 'addi':lambda:x+y, 'and':lambda:x&y,
                 'mull':lambda:x*y, 'slli':lambda:x<<y,
                 'addx4':lambda:4*x+y, 'addx8':lambda:8*x+y}[op]()
            registers[a[0]] = z & 0xffffffff
        elif op == 'loopgtz':
            count, end = value(a[0]), int(a[1], 0)
            assert loop is None and count > 0
            loop = [next_pc, end, count]
        elif op == 'bne':
            if value(a[0]) != value(a[1]):
                next_pc = int(a[2], 0)
        elif op == 'memw':
            pass
        elif pc in (0x37dc2,0x37dc5,0x37dcf) and op in ('lsi','mul.s','ssi'):
            # The synthetic source is already normalized. These FP operations
            # do not determine destinations and are outside this experiment.
            pass
        else:
            raise ValueError('Unsupported instruction: '+ins['text'])
        trace.append(pc)
        if loop and next_pc == loop[1]:
            loop[2] -= 1
            if loop[2]:
                next_pc = loop[0]
            else:
                loop = None
        pc = next_pc
    raise ValueError('Synthetic instruction budget exceeded')


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--mp5',type=Path,required=True)
    p.add_argument('--smu',type=Path,required=True)
    p.add_argument('--study',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    mp5,smu=args.mp5.read_bytes(),args.smu.read_bytes()
    assert hashlib.sha256(mp5).hexdigest()==MP5_SHA
    assert hashlib.sha256(smu).hexdigest()==SMU_SHA
    study=json.loads(args.study.read_text(encoding='utf-8'))
    assert study['source_sha256']==MP5_SHA
    fn=next(f for f in study['functions'] if f['entry']=='00037d40')
    instructions={int(i['address'],16):i for i in fn['instructions']}
    cases=[]
    starts=(0xab4,0xf34,0x13b4,0x1834,0x1cb4,0x2134,0x25b4,0x2a34)
    lengths=(2844,4100,5356,6612,7868,9124,10380,11636)
    for n,start,length in zip(range(2,17,2),starts,lengths):
        combined={}
        for slot in range(n):
            writes,trace=replay(mp5,instructions,n,slot)
            assert not combined.keys() & writes.keys()
            combined.update(writes)
            cases.append(dict(storage_slots=n,ordinal=slot,writes=len(writes),
                instructions=len(trace),first=min(writes),last=max(writes),
                trace_sha256=hashlib.sha256(json.dumps(trace).encode()).hexdigest()))
        assert set(combined)==set(range(0x634,length,4))
        # Independently compare the scatter result with row order, including
        # all eight source-core words and the separate 11+2 CCD loops.
        for row in range(18):
            for ordinal in range(n):
                for core in range(8):
                    assert combined[0x634+(row*n*8+ordinal*8+core)*4]==0x3f000000+row*8+core
        for row in range(13):
            for ordinal in range(n):
                assert combined[start+(row*n+ordinal)*4]==0x3f000000+144+row
    u=lambda a:struct.unpack_from('<I',smu,a)[0]
    assert u(0x16f38)==0x10000
    assert u(0x16e98)+0xfc==u(0x17de8)+0x34==0xe13c
    assert u(0x17dc4)+0x37==u(0x17de8)+0x33==0xe13b
    report=dict(schema=1,hardware_execution=False,mp5_sha256=MP5_SHA,smu_sha256=SMU_SHA,
        function='0x37d40',slice_start='0x37da5',slice_end_exclusive='0x37e52',
        float_normalization_executed=False,units_verified=False,physical_mapping_verified=False,
        core_rows=18,cores_per_storage_slot=8,ccd_rows=13,common_bytes=0x634,
        mode_flag_mask='0x10000',rails_guard_alias='0xe13c',cases=cases)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Verified',len(cases),'MP5 synthetic copy cases; no hardware execution')


if __name__=='__main__':
    main()
