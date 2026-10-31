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
        self.shift = 0

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
            elif op in ('add', 'addi', 'addmi', 'addx2', 'addx4', 'addx8', 'mull', 'quou', 'sub'):
                x, y = val(a[1]), val(a[2])
                self.r[a[0]] = (2*x+y if op=='addx2' else 4*x+y if op=='addx4' else 8*x+y if op=='addx8' else x*y if op=='mull' else x//y if op=='quou' else x-y if op=='sub' else x+y) & 0xffffffff
            elif op == 'moveqz':
                if not val(a[2]): self.r[a[0]] = val(a[1])
            elif op == 'movnez':
                if val(a[2]): self.r[a[0]] = val(a[1])
            elif op in ('minu', 'maxu'):
                self.r[a[0]] = (min if op=='minu' else max)(val(a[1]),val(a[2]))
            elif op in ('and', 'or'):
                self.r[a[0]] = val(a[1]) & val(a[2]) if op=='and' else val(a[1]) | val(a[2])
            elif op == 'ssl':
                self.shift = val(a[0]) & 31
            elif op == 'sll':
                self.r[a[0]] = (val(a[1]) << self.shift) & 0xffffffff
            elif op == 'slli':
                self.r[a[0]] = (val(a[1]) << val(a[2])) & 0xffffffff
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
            elif op == 'rfr':
                self.r[a[0]] = struct.unpack('<I',struct.pack('<f',self.f[a[1]]))[0]
            elif op == 'ufloat.s':
                self.f[a[0]] = f32(val(a[1])/(1 << val(a[2])))
            elif op == 'float.s':
                signed = val(a[1]); signed -= (1 << 32) if signed & (1 << 31) else 0
                self.f[a[0]] = f32(signed/(1 << val(a[2])))
            elif op == 'const.s':
                assert val(a[1]) in (0, 1), 'Only zero and one constants are modeled'
                self.f[a[0]] = float(val(a[1]))
            elif op == 'mov.s':
                self.f[a[0]] = self.f[a[1]]
            elif op in ('movt.s','movf.s'):
                if self.b[a[2]] == (op=='movt.s'): self.f[a[0]] = self.f[a[1]]
            elif op == 'round.s':
                value=f32(self.f[a[1]]*(1<<val(a[2])))
                assert -2147483648 <= value < 2147483648
                self.r[a[0]]=round(value)&0xffffffff
            elif op == 'movt':
                if self.b[a[2]]: self.r[a[0]] = val(a[1])
            elif op in ('olt.s', 'ole.s', 'oeq.s'):
                self.b[a[0]] = self.f[a[1]] < self.f[a[2]] if op=='olt.s' else self.f[a[1]] <= self.f[a[2]] if op=='ole.s' else self.f[a[1]] == self.f[a[2]]
            elif op in ('bt', 'bf'):
                if self.b[a[0]] == (op == 'bt'): next_pc = val(a[1])
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
            elif op == 'bbc':
                if not (val(a[0]) & (1 << (val(a[1])&31))): next_pc = val(a[2])
            elif op == 'beqz':
                if not val(a[0]): next_pc = val(a[1])
            elif op == 'bnez':
                if val(a[0]): next_pc = val(a[1])
            elif op in ('bne', 'bnei'):
                if val(a[0]) != val(a[1]): next_pc = val(a[2])
            elif op == 'beq':
                if val(a[0]) == val(a[1]): next_pc = val(a[2])
            elif op in ('bltu','bltui'):
                if val(a[0]) < val(a[1]): next_pc = val(a[2])
            elif op == 'bgeu':
                if val(a[0]) >= val(a[1]): next_pc = val(a[2])
            elif op in ('bany','bnone'):
                # Ghidra renders these branch targets before their registers.
                matches = bool(val(a[1]) & val(a[2]))
                if matches == (op=='bany'): next_pc = val(a[0])
            elif op == 'j':
                next_pc = val(a[0])
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
        for i in range(64): assert vm.getf(0x12c44+4*i) == (start+i)/8-49
        for core in range(8):
            expected = (start+core*7+2)/8-49
            assert vm.getf(CORE+0x1bc*core+0x28) == expected
            cases += 1
        assert vm.getf(0x12d44) == (start+63)/8-49
    retained = 0
    for valid_mask in (0,0x5555555555555555,0xaaaaaaaaaaaaaaaa):
        vm = Slice(body,code)
        expected = []
        valid_values = []
        for i in range(64):
            valid = (valid_mask>>i)&1
            vm.put(0x12b44+4*i,((392+i)<<11)|(valid<<10))
            vm.putf(0x12c44+4*i,400+i/8)
            expected.append(i/8 if valid else 400+i/8)
            if valid: valid_values.append(i/8)
            else: retained += 1
        for core in range(8):
            for j in range(3): vm.put(CORE+0x1bc*core+0x84+j,core*7+j,1)
        vm.run(0x2c0e3,0x2c15f)
        for i,value in enumerate(expected): assert vm.getf(0x12c44+4*i)==value
        empty_max = struct.unpack_from('<f',body,0x1946c)[0]
        assert vm.getf(0x12d44)==max(valid_values,default=empty_max)
        # Per-core selection does not re-check validity: retained old values
        # can participate in the maximum. Do not present it as a fresh sensor.
        for core in range(8):
            maximum=max(expected[core*7:core*7+3])
            for off in (0x20,0x24,0x28): assert vm.getf(CORE+0x1bc*core+off)==maximum
    return dict(core_max_cases=cases,raw_sensor_codes_checked=4096,
        invalid_mask_cases=3,retained_invalid_sensor_values=retained,
        invalid_sensor_behavior='previous converted value retained; per-core maximum may reuse it')


def export_snapshot(vm, width, ordinal):
    """Normalization and complete scatter, including the late CCD-11 x100.

    The reciprocal helper is an explicit exact-factor fixture. Everything from
    its return to the final PM write is executed, with no floating op skipped.
    Output MMIO addresses are dictionary keys, never real device accesses.
    """
    assert width in (1, 2) and 0 <= ordinal < width
    vm.r['a1'] = 0x50000
    vm.put(0x19d20+0x11, width, 1)
    vm.put(0x19d20+0x18, ordinal, 1)
    vm.put(0x19d20+0x3c, 0x20000)
    samples = vm.get(ACC+0x270)
    vm.run(0x2f54c, 0x2f565 if samples else 0x2f56f)
    if samples:
        assert vm.f['f0'] == samples
        vm.f['f0'] = f32(1/samples)
    vm.run(0x2f568 if samples else 0x2f56f, 0x2f657)
    addresses = [0x1320000+4*(row*8*width+ordinal*8+core)
                 for row in range(18) for core in range(8)]
    addresses += [0x1320000+4*(144*width+row*width+ordinal) for row in range(12)]
    return [vm.getf(address) for address in addresses]


def seed_accumulation(vm):
    body = vm.body
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
    vm.putf(0x1280c,61.25); vm.putf(0x12810,72.5)
    vm.putf(literal(0x191b8)+0x100, 1)
    vm.put(literal(0x191b8)+4, 4<<16)
    vm.putf(0x19cf0+0xc,0.25); vm.putf(0x7e34+0x1fc,1.25); vm.putf(0x11128+0x318,64)


def accumulation(body, code):
    vm = Slice(body, code)
    seed_accumulation(vm)
    for _ in range(4): vm.run(0x2f370,0x2f536)
    assert vm.get(ACC+0x270)==4
    before=[vm.getf(ACC+4*i) for i in range(156)]
    exported=export_snapshot(vm,2,1)
    for i in range(156):
        scale=100 if 40<=i<64 or i==155 else 1
        assert vm.getf(ACC+4*i)==before[i]/4*scale
        assert exported[i]==vm.getf(ACC+4*i)
    for core in range(8):
        assert vm.getf(ACC+0x20+core*4)==1.25
        assert vm.getf(ACC+0x40+core*4)==40+core/8
        for off, percent in ((0xa0,50),(0xc0,12.5),(0xe0,37.5)):
            assert vm.getf(ACC+off+core*4)==percent
    assert vm.getf(ACC+0x24c)==61.25 and vm.getf(ACC+0x250)==72.5
    assert vm.getf(ACC+0x26c)==25
    assert vm.get(ACC+0x270)==4  # SSIP does not overwrite the sample counter.
    return {'accumulated_samples':4,'normalized_words':156,'exported_words':156,
            'late_ccd_row_11_scale':100,'unit_checked_core_values':40,'unit_checked_ccd_values':3,
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


def ccd_temperature(body, code):
    # Include the core selection loop to preserve the real register state at
    # the CCD tail. Sensor responses are synthetic, not peripheral execution.
    selectors=[list(body[CORE+c*0x1bc+0x84:CORE+c*0x1bc+0x87]) for c in range(8)]
    ccd_selectors=list(body[0x12864:0x1286c])
    assert ccd_selectors==[13,14,15,16,25,26,27,28]
    cases=core_cases=retained=0; trace=hashlib.sha256()
    for rotation in range(64):
        for enabled in (0,1):
            for valid_mask in (0xffffffffffffffff,0,0x5555555555555555):
                vm=Slice(body,code); values=[]
                for i in range(64):
                    v=(i+rotation)%64; valid=(valid_mask>>i)&1
                    vm.put(0x12b44+4*i,((392+v)<<11)|(valid<<10))
                    vm.putf(0x12c44+4*i,100+v/8)
                    values.append(v/8 if valid else 100+v/8)
                for c,indices in enumerate(selectors):
                    for j,index in enumerate(indices): vm.put(CORE+c*0x1bc+0x84+j,index,1)
                for j,index in enumerate(ccd_selectors): vm.put(0x12864+j,index,1)
                vm.put(0x7829,enabled,1); vm.putf(0x12810,321.25)
                vm.run(0x2c0e3,0x2c19d)
                for c,indices in enumerate(selectors):
                    assert vm.getf(CORE+c*0x1bc+0x28)==max(values[i] for i in indices)
                    core_cases+=1
                a=max(values[i] for i in ccd_selectors)
                b=max(values[45:53]) if enabled else 321.25
                assert vm.getf(0x1280c)==a and vm.getf(0x12810)==b
                retained+=int(not enabled); cases+=1
                trace.update(struct.pack('<IIIff',rotation,enabled,valid_mask&0xffffffff,a,b))
    return dict(cases=cases,core_maxima=core_cases,disabled_row4_retained_cases=retained,
        initial_core_selectors=selectors,initial_ccd_row3_selectors=ccd_selectors,
        ccd_row4_sensor_indices=list(range(45,53)),runtime_selector_overrides_excluded=False,
        trace_sha256=trace.hexdigest())


def interval_ratio(body, code):
    # Check the copy chain into CCD row 11, then replay the counter arithmetic.
    # Clock/peripheral acquisition, interrupt masking and the reciprocal call
    # are explicit boundaries. A ratio is not CPU utilization or temperature.
    for pc,text in {0x31b56:'call8 0x000022a8',0x31b75:'call8 0x000327b0',
            0x3221d:'call8 0x00031b50',0x32223:'ssi f0,a2,0x0',
            0x2d77d:'call8 0x00032214',0x2d783:'lsi f0,a1,0x0',
            0x2d786:'ssi f0,a8,0xc'}.items(): assert code[pc]['text']==text
    assert struct.unpack_from('<I',body,0x19100)[0]==0x19cf0
    count=wraps=0; trace=hashlib.sha256()
    for elapsed in (8,256,65536):
        for now in (4,100000,0xfffffffc):
            for active in (0,1):
                for accumulated in (0,elapsed//4,elapsed):
                    vm=Slice(body,code); state=0x1000
                    pending=elapsed//8 if active else 0
                    vm.r.update(a2=state,a10=now)
                    vm.put(state,active,1); vm.put(state+4,(now-pending)&0xffffffff)
                    vm.put(state+8,(now-elapsed)&0xffffffff); vm.put(state+12,accumulated)
                    vm.run(0x31b59,0x31b75)
                    assert vm.f['f0']==elapsed and vm.r['a4']==accumulated+pending
                    vm.f['f0']=1/elapsed  # exact reciprocal fixture only
                    vm.run(0x31b78,0x31b87)
                    ratio=(accumulated+pending)/elapsed
                    assert vm.getf(state+16)==ratio and vm.get(state+12)==0 and vm.get(state+8)==now
                    assert vm.get(state+4)==(now if active else (now-pending)&0xffffffff)
                    # Replay the caller stores with explicit ABI hand-off.
                    vm.r['a2']=0x2000; vm.run(0x32220,0x32226)
                    vm.r['a1']=0x2000; vm.run(0x2d780,0x2d789)
                    assert vm.getf(0x19cfc)==ratio
                    count+=1; wraps+=int(now<elapsed)
                    trace.update(struct.pack('<5If',elapsed,now,active,accumulated,pending,ratio))
    return dict(cases=count,wrapped_interval_cases=wraps,producer_unit='ratio',pm_unit='%',pm_scale=100,
        numerator='accumulated ticks plus active interval if flagged',denominator='elapsed ticks',
        zero_elapsed_behavior_verified=False,counter_meaning_verified=False,
        trace_sha256=trace.hexdigest())


def voltage_model(body, code):
    # Seed completed fuse reads. The 9-bit code conversion and 5-bit grid step
    # are replayed; electrical off-code semantics are deliberately not imported.
    vf=0x19ac0; stack=0x1000
    assert struct.unpack_from('<I',body,0x19144)[0]==vf
    assert struct.unpack_from('<I',body,0x1914c)[0]+0x24==vf+0x34
    assert code[0x29bbb]['text']=='call8 0x00024ae4'
    offset,scale=(struct.unpack_from('<f',body,p)[0] for p in (0x18dac,0x18da8))
    assert abs(offset-.245)<1e-8 and abs(scale-.005)<1e-9
    conversions=grids=interpolations=0; trace=hashlib.sha256()
    for vid in range(512):
        vm=Slice(body,code); vm.r['a2']=vid; vm.run(0x24ae4,0x24af9)
        base=f32(offset+vid*scale); assert vm.f['f0']==base; conversions+=1
        for step_code in (0,1,16,31):
            vm.r.update(a1=stack,a4=vf,a10=step_code)
            vm.run(0x29b9a,0x29bac)
            step=f32(step_code*scale); assert vm.getf(vf+4)==step
            vm.f['f0']=base; vm.run(0x29bbe,0x29bd9)
            expected=base
            for i in range(16):
                assert vm.getf(vf+0x34+4*i)==expected
                expected=f32(expected+step)
            grids+=1
            trace.update(struct.pack('<IIff',vid,step_code,base,vm.getf(vf+0x34+60)))
    # Bounded interpolation slice: exact power-of-two reciprocal fixture,
    # with a distinct source axis. Do not emulate recip0 or index selection.
    for index in range(15):
        for fraction in (0,0.25,0.5,1):
            vm=Slice(body,code); vm.r.update(a3=0x3000,a4=index,a13=vf,a6=0xec40)
            axis=1+index/32
            vm.putf(vf+0x34+4*index,axis)
            vm.putf(0x3200,0.5); vm.putf(0x3080,2)
            vm.f.update(f2=4,f3=2,f0=2+fraction*2,f7=0.03125)
            vm.run(0x2976b,0x2977a); assert vm.f['f8']==2
            vm.f['f1']=0.5; vm.run(0x2977d,0x29789)
            expected=f32(axis+0.03125*fraction); assert vm.f['f5']==expected
            # The target register a6 at the common store is 0xec40 + slot*520.
            vm.run(0x29748,0x2974e)
            assert vm.getf(0xf040)==expected; interpolations+=1
    return dict(vid_conversions=conversions,voltage_grids=grids,points_per_grid=16,
        interpolation_cases=interpolations,unit='V',code_formula='0.245 + code * 0.005',
        measured_voltage=False,complete_model_emulated=False,zero_code_electrical_off_inferred=False,
        trace_sha256=trace.hexdigest())


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
    temperature_report=temperature(body,code)
    report={'schema':4,'hardware_execution':False,'source_sha256':SHA,
            'temperature_sensor_formula':'((raw >> 11) & 0xfff) / 8 - 49; validity bit 10',
            'temperature_cases':temperature_report['core_max_cases'],'temperature':temperature_report,
            'residency_cases':residency(body,code),
            'ccd_temperature':ccd_temperature(body,code),
            'interval_ratio':interval_ratio(body,code),'voltage_model':voltage_model(body,code),
            'accumulation':accumulation(body,code),
            'core_units':{'1':'V','2':'deg C','5':'%','6':'%','7':'%'},
            'ccd_units':{'3':'deg C','4':'deg C','11':'%'},
            'residency_labels':'C0/CC1/CC6 remain candidate names; fraction-to-percent scaling is traced',
            'sensor_acquisition_emulated':False,'reciprocal_numeric_accuracy_verified':False,
            'live_sensor_validity_verified':False,'physical_core_mapping_verified':False}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__=='__main__': main()
