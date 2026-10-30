#!/usr/bin/env python3
"""Bounded, byte-checked Granite MP5 data-path experiments on synthetic RAM.

This is not a firmware emulator: peripherals and reciprocal refinement are not
executed. Exact dyadic input fixtures exercise the accumulation, postincrement
normalization and percentage paths without inventing hardware observations.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

SHA = 'fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556'
ACC = 0x1af84
CORE = 0x11a18


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class Slice:
    def __init__(self, body, code):
        self.body, self.code = body, code
        self.r = {f'a{i}': 0 for i in range(16)}
        self.f = {}
        self.b = {}
        self.ram = {}
        self.trace = []

    def put(self, address, value, size=4):
        for i in range(size):
            self.ram[address+i] = (value >> (8*i)) & 255

    def get(self, address, size=4):
        return sum(self.ram[address+i] << (8*i) for i in range(size))

    def putf(self, address, value):
        self.put(address, struct.unpack('<I', struct.pack('<f', value))[0])

    def getf(self, address):
        return struct.unpack('<f', struct.pack('<I', self.get(address)))[0]

    def run(self, pc, stop, budget=10000):
        loop = None
        val = lambda a: self.r[a] if a.startswith('a') else int(a, 0)
        for _ in range(budget):
            if pc == stop:
                return
            ins = self.code[pc]
            raw = bytes.fromhex(ins['bytes'])
            assert self.body[pc:pc+len(raw)] == raw
            op, _, rest = ins['text'].partition(' ')
            op = op.removesuffix('.n')
            a = [s.strip() for s in rest.split(',') if s.strip()]
            next_pc = pc+len(raw)
            self.trace.append(pc)
            if op in ('entry', 'memw'):
                pass
            elif op in ('mov', 'movi'):
                self.r[a[0]] = val(a[1]) & 0xffffffff
            elif op == 'l32r':
                self.r[a[0]] = struct.unpack_from('<I', self.body, val(a[1]))[0]
            elif op in ('l8ui', 'l16ui', 'l32i'):
                self.r[a[0]] = self.get(val(a[1])+val(a[2]), {'l8ui':1, 'l16ui':2, 'l32i':4}[op])
            elif op in ('s8i', 's16i', 's32i'):
                self.put(val(a[1])+val(a[2]), val(a[0]), {'s8i':1, 's16i':2, 's32i':4}[op])
            elif op in ('add', 'addi', 'addmi', 'addx4', 'mull', 'quou'):
                x, y = val(a[1]), val(a[2])
                self.r[a[0]] = (4*x+y if op=='addx4' else x*y if op=='mull' else x//y if op=='quou' else x+y) & 0xffffffff
            elif op == 'extui':
                self.r[a[0]] = (val(a[1]) >> val(a[2])) & ((1 << val(a[3]))-1)
            elif op in ('lsi', 'lsip', 'lsxp'):
                address = val(a[1])+(val(a[2]) if op=='lsi' else 0)
                self.f[a[0]] = self.getf(address)
                if op!='lsi': self.r[a[1]] = (val(a[1])+val(a[2])) & 0xffffffff
            elif op in ('ssi', 'ssip'):
                address = val(a[1])+(val(a[2]) if op=='ssi' else 0)
                self.putf(address, self.f[a[0]])
                if op=='ssip': self.r[a[1]] = (val(a[1])+val(a[2])) & 0xffffffff
            elif op == 'wfr':
                self.f[a[0]] = struct.unpack('<f', struct.pack('<I', val(a[1])))[0]
            elif op == 'ufloat.s':
                self.f[a[0]] = f32(val(a[1])/(1 << val(a[2])))
            elif op == 'mov.s':
                self.f[a[0]] = self.f[a[1]]
            elif op == 'movt.s':
                if self.b[a[2]]: self.f[a[0]] = self.f[a[1]]
            elif op in ('olt.s', 'ole.s'):
                self.b[a[0]] = self.f[a[1]] < self.f[a[2]] if op=='olt.s' else self.f[a[1]] <= self.f[a[2]]
            elif op in ('add.s', 'sub.s', 'mul.s', 'madd.s'):
                x, y = self.f[a[1]], self.f[a[2]]
                self.f[a[0]] = f32(x+y if op=='add.s' else x-y if op=='sub.s' else x*y+self.f[a[0]] if op=='madd.s' else x*y)
            elif op == 'loopgtz':
                assert loop is None
                count, end = val(a[0]), val(a[1])
                if count: loop = [next_pc, end, count]
                else: next_pc = end
            elif op == 'bbci':
                if not (val(a[0]) & (1 << val(a[1]))): next_pc = val(a[2])
            else:
                raise ValueError(f'Unmodeled instruction at {pc:x}: {ins["text"]}')
            if loop and next_pc == loop[1]:
                loop[2] -= 1
                if loop[2]: next_pc = loop[0]
                else: loop = None
            pc = next_pc
        raise ValueError('Bounded instruction budget exceeded')


def temperature(body, code):
    # No simulated peripheral: seed the completed raw sensor response, then
    # replay just conversion and per-core sensor selection from 2c0e3 onward.
    cases = 0
    for start in range(0, 4096, 64):
        vm = Slice(body, code)
        for i in range(64):
            vm.put(0x12b44+4*i, ((start+i)<<11)|(1<<10))
            vm.putf(0x12c44+4*i, -999)
        for core in range(8):
            for j in range(3): vm.put(CORE+0x1bc*core+0x84+j, core*7+j, 1)
        vm.run(0x2c0e3, 0x2c15f)
        for core in range(8):
            expected = (start+core*7+2)/8-49
            assert vm.getf(CORE+0x1bc*core+0x28) == expected
            cases += 1
        assert vm.getf(0x12d44) == (start+63)/8-49
    return cases


def accumulation(body, code):
    vm = Slice(body, code)
    for offset in range(0, 0x274, 4): vm.putf(ACC+offset, 0)
    core_offsets = (0x10,0x28,0x68,0xf4,0x70,0x74,0x78,0xf8,0x108,0x104,0xe8,0xec,0xdc)
    for core in range(8):
        for off in core_offsets: vm.putf(CORE+core*0x1bc+off, 0.5)
        vm.putf(CORE+core*0x1bc+0x28, 40+core/8)
        for off, fraction in ((0x70,0.5),(0x74,0.125),(0x78,0.375)):
            vm.putf(CORE+core*0x1bc+off, fraction)
        vm.putf(0xee44+0x1fc+core*0x520, 1.25)
        vm.put(0xeb28+2+core*0x520, core, 1)
        vm.put(0x15884+0x2c+core, core+1, 1)
        for off in (0x1f4,0x204): vm.putf(0x11278+core*0x3c+off, 2)
    literal = lambda a: struct.unpack_from('<I',body,a)[0]
    for off in (0x1ac,0x1f0,0x224,0x1b4,0x1b8,0x23c,0x230,0x250): vm.putf(0x12658+off, 0.5)
    vm.putf(literal(0x191b8)+0x100, 1)
    vm.put(literal(0x191b8)+4, 4<<16)
    vm.putf(0x19cf0+0xc,0.25); vm.putf(0x7e34+0x1fc,1.25); vm.putf(0x11128+0x318,64)
    for _ in range(4): vm.run(0x2f370,0x2f536)
    assert vm.get(ACC+0x270)==4
    before=[vm.getf(ACC+4*i) for i in range(156)]
    # Explicit exact reciprocal fixture. The firmware calls reciprocal seed
    # and refinement; this experiment does not emulate their numeric accuracy.
    vm.f['f0']=0.25
    vm.run(0x2f56f,0x2f5af)
    for i in range(156):
        scale=100 if 40<=i<64 else 1
        assert vm.getf(ACC+4*i)==before[i]/4*scale
    for core in range(8):
        assert vm.getf(ACC+0x40+core*4)==40+core/8
        for off, percent in ((0xa0,50),(0xc0,12.5),(0xe0,37.5)):
            assert vm.getf(ACC+off+core*4)==percent
    assert vm.get(ACC+0x270)==4  # SSIP does not overwrite the sample counter.
    return {'accumulated_samples':4,'normalized_words':156,'unit_checked_core_values':32,
            'trace_sha256':hashlib.sha256(json.dumps(vm.trace).encode()).hexdigest()}


def residency(body, code):
    cases=0
    for first in (0,500,1000,2000,4000,5000):
        for second in (0,500,1000,2000,4000,5000):
            vm=Slice(body,code); vm.r.update(a4=CORE,a3=1000)
            scale=struct.unpack_from('<f',body,0x19498)[0]
            threshold=struct.unpack_from('<f',body,0x194a0)[0]
            vm.f.update(f0=0,f1=1,f5=threshold,f6=scale)
            vm.put(CORE+0x128,first); vm.put(CORE+0x12c,second)
            vm.run(0x2c670,0x2c6be)
            a=f32((first*2500//1000)*scale); b=f32((second*2500//1000)*scale)
            a=1 if a>threshold else a; b=1 if b>threshold else b
            assert vm.getf(CORE+0x78)==a and vm.getf(CORE+0x74)==b
            assert vm.getf(CORE+0x70)==max(0,f32(1-f32(a+b)))
            assert all(0<=vm.getf(CORE+o)<=1 for o in (0x70,0x74,0x78))
            cases+=1
    return cases


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('mp5','study','output'): p.add_argument('--'+key,required=True,type=Path)
    args=p.parse_args(); body=args.mp5.read_bytes()
    assert hashlib.sha256(body).hexdigest()==SHA
    study=json.loads(args.study.read_text(encoding='utf-8')); assert study['source_sha256']==SHA
    functions={int(f['entry'],16):f for f in study['functions']}
    code={int(i['address'],16):i for f in functions.values() for i in f['instructions']}
    for address,i in code.items():
        raw=bytes.fromhex(i['bytes']); assert body[address:address+len(raw)]==raw
    assert code[0x2f57e]['text']=='ssip f1,a9,0x4'
    assert code[0x2f39d]['text']=='lsxp f2,a5,a11'
    assert code[0x2f565]['text']=='call8 0x000327b0'
    reciprocal=[i['text'].split(' ')[0] for i in functions[0x327b0]['instructions']]
    assert reciprocal.count('recip0.s')==1 and reciprocal.count('maddn.s')==2
    report={'schema':1,'hardware_execution':False,'source_sha256':SHA,
            'temperature_sensor_formula':'((raw >> 11) & 0xfff) / 8 - 49; validity bit 10',
            'temperature_cases':temperature(body,code),'residency_cases':residency(body,code),
            'accumulation':accumulation(body,code),
            'core_units':{'2':'deg C','5':'%','6':'%','7':'%'},
            'residency_labels':'C0/CC1/CC6 remain candidate names; fraction-to-percent scaling is traced',
            'sensor_acquisition_emulated':False,'reciprocal_numeric_accuracy_verified':False,
            'live_sensor_validity_verified':False,'physical_core_mapping_verified':False}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__=='__main__': main()
