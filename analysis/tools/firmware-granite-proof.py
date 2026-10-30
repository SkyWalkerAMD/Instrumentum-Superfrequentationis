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

MP5_SHA = 'fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556'
SMU_SHA = '5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596'


def replay(body, instructions, slots, slot):
    registers = {'a'+str(i): 0 for i in range(16)}
    registers.update(a1=0x50000, a2=slots, a5=0x19d20)
    ram = {0x50004: slot, 0x19d20+0x3c: 0x20000}
    for i in range(156):
        ram[0x1af84+4*i] = 0x3f000000+i
    writes, trace = {}, []
    pc, loop = 0x2f5af, None

    def read(address):
        if address in ram:
            return ram[address]
        if 0 <= address <= len(body)-4:
            return struct.unpack_from('<I', body, address)[0]
        raise ValueError('Unmapped synthetic read: '+hex(address))

    for _ in range(10000):
        if pc == 0x2f657:
            assert len(writes) == 156
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
                offset = address-0x1300000-0x20000+0x4b4
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
        elif pc in (0x2f5cc,0x2f5cf,0x2f5d8) and op in ('lsi','mul.s','ssi'):
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


def metadata(body, instructions, entry, slots=2, variant=0, address=0x123456000):
    """Replay three metadata handlers; mailbox helpers are explicit stubs.

    This does not execute firmware or model a live mailbox/concurrent transfer.
    Xtensa call8 caller a10/a11/a12 become callee a2/a3/a4.
    """
    regs = {'a'+str(i): 0 for i in range(16)}
    regs['a2'] = 0  # Synthetic mailbox index.
    ram = {0x8164+0x2a: slots, 0x8164+0xc: variant,
           0x7fd0+0x14: address & 0xffffffff, 0x7fd0+0x10: address >> 32}
    result, response, trace = {}, None, []
    pc = entry
    for _ in range(100):
        ins = instructions[pc]
        raw = bytes.fromhex(ins['bytes'])
        assert raw == body[pc:pc+len(raw)]
        op, _, operands = ins['text'].partition(' ')
        a = [v.strip() for v in operands.split(',') if v.strip()]
        op = op.removesuffix('.n')
        next_pc = pc+len(raw)
        val = lambda v: regs[v] if v.startswith('a') else int(v,0)
        trace.append(pc)
        if op == 'retw':
            assert response == 1
            return result, trace
        if op == 'entry': pass
        elif op in ('mov','movi'): regs[a[0]] = val(a[1])
        elif op == 'l32r': regs[a[0]] = struct.unpack_from('<I',body,int(a[1],0))[0]
        elif op in ('l32i','l16ui','l8ui'):
            regs[a[0]] = ram[val(a[1])+int(a[2],0)]
        elif op == 'moveqz':
            if not val(a[2]): regs[a[0]] = val(a[1])
        elif op == 'bnei':
            if val(a[0]) != val(a[1]): next_pc = int(a[2],0)
        elif op == 'j': next_pc = int(a[0],0)
        elif op == 's32i':
            location = val(a[1])+int(a[2],0)
            assert location in (0x60000,0x60004)
            result[(location-0x60000)//4] = val(a[0])
        elif op == 'call8':
            target = int(a[0],0)
            if target == 0x1580: result[0] = regs['a11']
            elif target == 0x15ac: result[regs['a11']] = regs['a12']
            elif target == 0x1608: regs['a10'] = 0x60000
            elif target == 0x14ec: response = regs['a11']
            else: raise ValueError('Unmodelled helper: '+hex(target))
        else: raise ValueError('Unmodelled metadata instruction: '+ins['text'])
        pc = next_pc
    raise ValueError('Metadata replay budget exceeded')


def main():
    p = argparse.ArgumentParser()
    for key in ('mp5','smu','mp5-study','smu-study','output'):
        p.add_argument('--'+key,type=Path,required=True)
    args = p.parse_args()
    mp5, smu = args.mp5.read_bytes(), args.smu.read_bytes()
    assert hashlib.sha256(mp5).hexdigest() == MP5_SHA
    assert hashlib.sha256(smu).hexdigest() == SMU_SHA
    def load(path, body, sha):
        study = json.loads(path.read_text(encoding='utf-8'))
        assert study['source_sha256'] == sha
        functions = {int(f['entry'],16): f for f in study['functions']}
        instructions = {int(i['address'],16): i for f in functions.values() for i in f['instructions']}
        for address, ins in instructions.items():
            raw = bytes.fromhex(ins['bytes'])
            assert body[address:address+len(raw)] == raw
        return instructions
    mi, si = load(args.mp5_study,mp5,MP5_SHA), load(args.smu_study,smu,SMU_SHA)
    # Cross-check command dispatch independently of the handler replay.
    u = lambda a: struct.unpack_from('<I',smu,a)[0]
    for command, entry in ((2,0x1f2d0),(3,0x2ce2c),(4,0x2d1f0),(5,0x2d1a0)):
        assert u(0x6fa4+8*command) == entry
    assert struct.unpack_from('<I',mp5,0x13298+8*0x27)[0] == 0x2f54c
    assert u(0x11704) == 0x625400 and u(0x11500) == 0x8164 and u(0x117c4) == 0x7fd0
    assert si[0x2d16b]['text'] == 'movi a12,0x4b4'
    assert si[0x2d184]['text'] == 'movi a12,0x4e0'
    cases, metadata_cases = [], []
    for n, length in ((1,1828),(2,2452)):
        combined = {}
        for slot in range(n):
            writes, trace = replay(mp5,mi,n,slot)
            assert not combined.keys() & writes.keys()
            combined.update(writes)
            cases.append(dict(storage_slots=n,ordinal=slot,writes=len(writes),
                instructions=len(trace),first=min(writes),last=max(writes),
                trace_sha256=hashlib.sha256(json.dumps(trace).encode()).hexdigest()))
        assert set(combined) == set(range(0x4b4,length,4))
        for row in range(18):
            for slot in range(n):
                for core in range(8):
                    assert combined[0x4b4+(row*n*8+slot*8+core)*4] == 0x3f000000+row*8+core
        for row in range(12):
            for slot in range(n):
                assert combined[0x4b4+18*n*32+(row*n+slot)*4] == 0x3f000000+144+row
    for n in (0,1,2,3,65535):
        for variant in (0,1,255):
            result, trace = metadata(smu,si,0x2d1a0,n,variant)
            version = (0x620105 if n == 1 else 0x620205) if not variant else (0x621101 if n == 1 else 0x621201)
            expected = {0: version,1:1828 if n == 1 else 2452} if n in (1,2) else {0:0,1:0}
            assert result == expected
            metadata_cases.append(dict(slots=n,variant=variant,result=result,steps=len(trace)))
    for address in (0,0x123456000,0x123456789abcdef0,0xffffffffffffffff):
        result, _ = metadata(smu,si,0x2d1f0,address=address)
        assert result[0] | (result[1]<<32) == address
    assert metadata(smu,si,0x1f2d0)[0] == {0:0x625400}
    report = dict(schema=1,hardware_execution=False,smu_sha256=SMU_SHA,mp5_sha256=MP5_SHA,
        firmware_version='98.84.0',float_normalization_executed=False,units_verified=False,
        physical_mapping_verified=False,live_protocol_verified=False,mailbox_helpers_stubbed=True,
        common_bytes=1204,core_rows=18,cores_per_storage_slot=8,ccd_rows=12,
        slice_start='0x2f5af',slice_end_exclusive='0x2f657',copy_cases=cases,
        metadata_cases=metadata_cases,address_cases=4,smu_version_cases=1)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Verified 3 scatter cases and 20 metadata cases; no hardware execution')


if __name__ == '__main__': main()

