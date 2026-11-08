#!/usr/bin/env python3
"""Byte-checked MP5 time conversion and deadline semantics, without Hz claims.

The integer helpers execute original instructions. Timer words and register
replies are fixtures. No external clock, event identity or physical unit is
inferred from the conversion constants or from synthetic peripheral behavior.
"""
import argparse
from collections import deque
import hashlib
import itertools
import json
from pathlib import Path
import random
import runpy
import struct

HERE = Path(__file__).parent
T = runpy.run_path(str(HERE / 'firmware-granite-topology.py'))
MASK, TIMER = 0xffffffff, 0x32001c0


class IntegerSlice:
    """Bounded integer subset; CALL8 crosses explicit register-window frames.

    This does not model interrupt delivery or timer latching. PS changes are
    recorded, not asserted to make two peripheral loads atomic. Unsupported
    instructions, undeclared memory and unmodeled calls fail immediately.
    """
    def __init__(self, body, code):
        self.body, self.code = body, code
        self.r = {f'a{i}': 0 for i in range(16)}
        self.r['a1'] = 0x50000
        self.ram, self.events, self.trace = {}, [], []
        self.sar, self.ps = 0, 0
        self.timer, self.replies = deque(), deque()

    def put(self, address, value, size=4):
        assert 0 <= address < 0x100000
        for i in range(size): self.ram[address+i] = value >> (8*i) & 255

    def get(self, address):
        if address in (TIMER, TIMER+4):
            assert self.timer, 'Undeclared timer read'
            expected, value = self.timer.popleft()
            assert expected == address
            self.events.append(('timer', address, value))
            return value
        return sum(self.ram[address+i] << (8*i) for i in range(4))

    def call(self, address):
        if address == 0x21a4:
            assert self.r['a10'] == 0 and self.r['a12'] == 2 and self.replies
            self.r['a10'] = self.replies.popleft()
            self.events.append(('register', self.r['a11'], self.r['a10']))
            return
        assert address in (0x22c8, 0x22e4, 0x32748, 0x32440)
        parent = self.r
        self.r = {f'a{i}': 0 for i in range(16)}
        self.r['a1'] = parent['a1'] - 0x100
        for i in range(6): self.r[f'a{i+2}'] = parent[f'a{i+10}']
        self.run(address)
        for i in range(6): parent[f'a{i+10}'] = self.r[f'a{i+2}']
        self.r = parent

    def run(self, pc, stop=None, budget=4096):
        val = lambda x: self.r[x] if x.startswith('a') else int(x, 0)
        for _ in range(budget):
            if pc == stop: return
            ins = self.code[pc]; raw = bytes.fromhex(ins['bytes'])
            assert self.body[pc:pc+len(raw)] == raw
            op, _, operands = ins['text'].partition(' ')
            op = op.removesuffix('.n')
            a = [x.strip() for x in operands.split(',') if x.strip()]
            nxt = pc + len(raw); self.trace.append(pc)
            if op in ('entry', 'memw', 'nop'): pass
            elif op == 'retw': return
            elif op == 'call8': self.call(val(a[0]))
            elif op in ('mov', 'movi'): self.r[a[0]] = val(a[1]) & MASK
            elif op == 'l32r': self.r[a[0]] = struct.unpack_from('<I', self.body, val(a[1]))[0]
            elif op == 'l32i': self.r[a[0]] = self.get(val(a[1])+val(a[2]))
            elif op == 's32i': self.put(val(a[1])+val(a[2]), val(a[0]))
            elif op == 'extui': self.r[a[0]] = val(a[1]) >> val(a[2]) & ((1 << val(a[3]))-1)
            elif op == 'nsau': self.r[a[0]] = 32-val(a[1]).bit_length()
            elif op in ('add', 'addi', 'sub', 'mull', 'mul16u', 'quou', 'remu', 'and', 'or'):
                x, y = val(a[1]), val(a[2])
                if op in ('quou', 'remu'): assert y, 'Division by zero is outside this proof'
                z = x+y if op in ('add', 'addi') else x-y if op=='sub' else x*y if op=='mull' else (x&65535)*(y&65535) if op=='mul16u' else x//y if op=='quou' else x%y if op=='remu' else x&y if op=='and' else x|y
                self.r[a[0]] = z & MASK
            elif op == 'moveqz':
                if not val(a[2]): self.r[a[0]] = val(a[1])
            elif op == 'ssai': self.sar = val(a[0])
            elif op == 'ssr': self.sar = val(a[0]) & 31
            elif op == 'ssl': self.sar = 32-(val(a[0]) & 31)
            elif op == 'sll': self.r[a[0]] = (val(a[1]) << ((32-self.sar)&63)) & MASK
            elif op == 'srl': self.r[a[0]] = val(a[1]) >> self.sar
            elif op == 'slli': self.r[a[0]] = val(a[1]) << val(a[2]) & MASK
            elif op == 'src': self.r[a[0]] = ((val(a[1])<<32 | val(a[2])) >> self.sar) & MASK
            elif op in ('beqz', 'bnez'):
                if bool(val(a[0])) == (op=='bnez'): nxt = val(a[1])
            elif op in ('bgeu', 'bltu', 'bne', 'beq'):
                x,y = val(a[0]),val(a[1])
                if x>=y if op=='bgeu' else x<y if op=='bltu' else x!=y if op=='bne' else x==y: nxt = val(a[2])
            elif op == 'j': nxt = val(a[0])
            elif op == 'rsil':
                self.r[a[0]] = self.ps; self.ps = self.ps & ~15 | val(a[1])
                self.events.append(('ps', self.ps))
            elif op == 'wsr':
                assert a[1] == 'PS'; self.ps = val(a[0]); self.events.append(('ps', self.ps))
            else: raise ValueError(f'Unsupported instruction at {pc:x}: {ins["text"]}')
            pc = nxt
        raise ValueError('Bounded instruction budget exceeded')


def arithmetic(body, code):
    values = [0,1,2,65535,65536,65537,0x7fffffff,0x80000000,0xfffffffe,MASK]
    rng = random.Random(0x6284)
    pairs = list(itertools.product(values, repeat=2)) + [(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(1024)]
    trace=hashlib.sha256()
    for x,y in pairs:
        vm=IntegerSlice(body,code);vm.r.update(a2=x,a3=y);vm.run(0x32748)
        assert vm.r['a2'] | vm.r['a3']<<32 == x*y
        trace.update(struct.pack('<IIQ',x,y,x*y))
    divisions = [(rng.getrandbits(64),d) for d in (1000000,1000000000) for _ in range(1024)]
    divisions += [(n,d) for n in (0,1,0xffffffff,0x100000000,0xffffffffffffffff) for d in (1000000,1000000000)]
    for n,d in divisions:
        vm=IntegerSlice(body,code);vm.r.update(a2=n&MASK,a3=n>>32,a4=d,a5=0);vm.run(0x32440)
        assert vm.r['a2'] | vm.r['a3']<<32 == n//d
        trace.update(struct.pack('<QQQ',n,d,n//d))
    return dict(multiply_cases=len(pairs),division_cases=len(divisions),
        executed_helpers=['0x32748','0x32440'],divisors=[1000000,1000000000],
        arithmetic_helper_results_are_fixtures=False,trace_sha256=trace.hexdigest())


def timer_read(body,code):
    trace=hashlib.sha256();cases=0
    for low,high in itertools.product((0,1,0x7fffffff,0xfffffffe,MASK),(0,1,0x7fffffff,MASK)):
        vm=IntegerSlice(body,code);vm.ps=0x432101
        vm.timer.extend(((TIMER,low),(TIMER+4,high)));vm.run(0x22c8)
        assert vm.r['a2']==low and vm.r['a3']==high and vm.ps==0x432101 and not vm.timer
        assert vm.events==[('ps',0x432102),('timer',TIMER,low),('timer',TIMER+4,high),('ps',0x432101)]
        trace.update(struct.pack('<II',low,high));cases+=1
    return dict(cases=cases,entry='0x22c8',load_order=['0x32001c0','0x32001c4'],
        low_then_high_without_retry=True,interrupt_state_restored=True,
        hardware_latching_or_atomicity_verified=False,trace_sha256=trace.hexdigest())


def deadlines(body,code):
    trace=hashlib.sha256();cases=0;inclusive=0;expired=0;wrapped=0
    values=(0,1,2,9,10,11,19,20,21,65535,1000000,0x7fffffff,MASK)
    for duration,start,phase in itertools.product(values,(0,0xfffffff0,0x1fffffff0,0xfffffffffffffffe),('before','equal','after')):
        ticks=(duration*100000000+999999999)//1000000000
        deadline=(start+ticks)&0xffffffffffffffff
        now=max(0,deadline-1) if phase=='before' else deadline if phase=='equal' else min(0xffffffffffffffff,deadline+1)
        vm=IntegerSlice(body,code);vm.r.update(a2=0x1600134,a3=0xa5,a4=0xff,a5=duration)
        for t in (start,now):vm.timer.extend(((TIMER,t&MASK),(TIMER+4,t>>32)))
        vm.replies.append(0x123400a5);vm.run(0x305bc)
        polling=duration==0 or now<=deadline
        reads=[e for e in vm.events if e[0]=='register']
        assert bool(reads)==polling and not vm.timer
        assert vm.r['a3']==deadline&MASK and vm.r['a6']==deadline>>32
        assert vm.r['a2']==0xa5  # Both return paths retain masked expected bits.
        assert (not vm.replies)==polling
        inclusive+=phase=='equal' and polling;expired+=not polling;wrapped+=start+ticks>0xffffffffffffffff
        trace.update(struct.pack('<IQQQB',duration,start,deadline,now,polling));cases+=1
    # A zero-duration request ignores a later timer and continues until match.
    vm=IntegerSlice(body,code);vm.r.update(a2=0x1600134,a3=7,a4=0xff,a5=0)
    for t in (42,42,999,0xffffffffffffffff):vm.timer.extend(((TIMER,t&MASK),(TIMER+4,t>>32)))
    vm.replies.extend((1,2,7));vm.run(0x305bc)
    assert not vm.replies and not vm.timer
    assert [e[2] for e in vm.events if e[0]=='register']==[1,2,7]
    mismatch_cases=0
    for start in (0,0xfffffff0,0x1fffffff0):
        vm=IntegerSlice(body,code);vm.r.update(a2=0x1600134,a3=0xabcda5,a4=0xff,a5=21)
        for t in (start,start+2,start+3,start+4):vm.timer.extend(((TIMER,t&MASK),(TIMER+4,t>>32)))
        vm.replies.extend((0x100000a4,0xffffffff));vm.run(0x305bc)
        assert not vm.timer and not vm.replies and vm.r['a2']==0xa5
        assert len([e for e in vm.events if e[0]=='register'])==2
        mismatch_cases+=1
    # Absolute unsigned comparison does not implement a wrapping interval.
    vm=IntegerSlice(body,code);vm.r.update(a2=0x1600134,a3=7,a4=0xff,a5=21)
    for t in (0xfffffffffffffffe,0xfffffffffffffffe):vm.timer.extend(((TIMER,t&MASK),(TIMER+4,t>>32)))
    vm.run(0x305bc)
    assert not vm.timer and not any(e[0]=='register' for e in vm.events)
    return dict(cases=cases,inclusive_deadline_reads=inclusive,expired_without_register_read=expired,
        wrapping_deadline_cases=wrapped,zero_duration_three_read_cases=1,
        mismatching_register_timeout_cases=mismatch_cases,wrapped_deadline_immediately_expires_cases=1,
        entry='0x305bc',tick_formula='ceil(uint32(duration) * 100000000 / 1000000000)',
        zero_duration_disables_deadline=True,deadline_comparison='unsigned now <= wrapped uint64 deadline',
        timeout_has_distinct_status_return=False,absolute_timer_hz_verified=False,
        argument_physical_unit_verified=False,trace_sha256=trace.hexdigest())


def delay_conversion(body,code):
    trace=hashlib.sha256();values=[0,1,2,65535,65536,42949672,42949673,0x7fffffff,MASK]
    for value in values:
        vm=IntegerSlice(body,code);vm.put(0xe448,value)
        vm.run(0x29002,0x29015)
        ticks=value*100
        assert vm.r['a10']==ticks&MASK and vm.r['a11']==ticks>>32
        # Original delay receives only the low word in a2 across CALL8.
        low=ticks&MASK
        vm.timer.extend(((TIMER,0xfffffffe),(TIMER,(0xfffffffe+low)&MASK)))
        vm.call(0x22e4);assert not vm.timer
        trace.update(struct.pack('<IQI',value,ticks,low))
    return dict(cases=len(values),conversion_slice='0x29002..0x29015',
        tick_formula='low32(uint32(input) * 100000000 / 1000000)',
        first_truncating_input=42949673,delay_uses_uint32_subtraction=True,
        input_physical_unit_verified=False,trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('mp5','study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();body=a.mp5.read_bytes();code=T['instructions'](a.study,body,T['MP5_SHA'])
    result=dict(schema=1,source_sha256=T['MP5_SHA'],hardware_execution=False,
        arithmetic=arithmetic(body,code),timer_reader=timer_read(body,code),
        polling_deadlines=deadlines(body,code),delay_conversion=delay_conversion(body,code),
        new_core_or_ccd_units={},timer_frequency_verified=False,counter_events_verified=False,
        physical_core_mapping_verified=False,runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))


if __name__=='__main__':main()
