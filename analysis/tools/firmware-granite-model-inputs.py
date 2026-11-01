#!/usr/bin/env python3
"""Replay Granite model math and CCD configuration quantization, without MMIO.

The reference exponential table is read from the hash-pinned MP5 image. Model
coefficients and inputs are synthetic. Reciprocal helper results are explicit
correctly rounded fixtures; its refinement and FP exceptions are not emulated.
Recovering these operations does not identify calibration or physical units.
"""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
H = runpy.run_path(str(HERE / 'firmware-granite-semantics.py'))
T = runpy.run_path(str(HERE / 'firmware-granite-topology.py'))
Slice, f32 = H['Slice'], H['f32']
TABLE, ROWS = 0x18660, 41
bits = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]


class Kernels:
    def __init__(self, body, code):
        self.body, self.code = body, code
        self.reciprocal_fixtures = 0

    def vm(self, coefficients=()):
        vm = Slice(self.body, self.code)
        vm.r.update(a1=0x50000, a2=0x12924)
        for i, value in enumerate(coefficients):
            vm.putf(0x12924 + 4*i, value)
        return vm

    def reciprocal(self, value):
        assert math.isfinite(value) and value != 0
        self.reciprocal_fixtures += 1
        return f32(1/value)

    def check_call(self, address, target):
        assert self.code[address]['text'] == f'call8 0x{target:08x}'

    def exponential(self, value):
        vm = self.vm(); vm.f['f0'] = f32(value)
        for at in range(TABLE, TABLE + ROWS*24, 4):
            vm.put(at, struct.unpack_from('<I', self.body, at)[0])
        stop = 0x24c6b if value < 0 else 0x24c6e
        vm.run(0x24c20, stop)
        result = vm.f['f0']
        if value < 0:
            self.check_call(stop, 0x327b0)
            result = self.reciprocal(result)
        return result

    def power(self, base, exponent):
        vm = self.vm(); vm.f.update(f0=f32(base), f1=f32(exponent))
        stop = 0x24cab if exponent < 0 else 0x24cae
        vm.run(0x24c70, stop)
        result = vm.f['f0']
        if exponent < 0:
            self.check_call(stop, 0x327b0)
            result = self.reciprocal(result)
        return result

    def simple(self, coefficients, value, temperature, scale=1):
        vm = self.vm(coefficients)
        vm.f.update(f0=value, f1=temperature, f2=scale)
        vm.run(0x32684, 0x326ab); self.check_call(0x326ab, 0x24c20)
        vm.f['f0'] = self.exponential(vm.f['f0'])
        vm.run(0x326ae, 0x326ba)
        return vm.f['f0']

    def blended(self, coefficients, value, bound, temperature, activity, suppression):
        vm = self.vm(coefficients)
        vm.f.update(f0=value, f1=bound, f2=temperature, f3=activity)
        vm.putf(0x12e24, suppression)
        vm.run(0x2bfe8, 0x2c01b); self.check_call(0x2c01b, 0x24c20)
        vm.f['f0'] = self.exponential(vm.f['f0'])
        vm.run(0x2c01e, 0x2c048); self.check_call(0x2c048, 0x24c20)
        vm.f['f0'] = self.exponential(vm.f['f0'])
        vm.run(0x2c04b, 0x2c072)
        return vm.f['f0']

    def power_temperature(self, coefficients, value, temperature):
        vm = self.vm(coefficients); vm.f.update(f0=value, f1=temperature)
        vm.run(0x326bc, 0x326c5); self.check_call(0x326c5, 0x24c70)
        vm.f['f0'] = self.power(vm.f['f0'], vm.f['f1'])
        vm.run(0x326c8, 0x326d4); self.check_call(0x326d4, 0x327b0)
        vm.f['f0'] = self.reciprocal(vm.f['f0'])
        vm.run(0x326d7, 0x326dd); self.check_call(0x326dd, 0x24c20)
        vm.f['f0'] = self.exponential(vm.f['f0'])
        vm.run(0x326e0, 0x326ec)
        return vm.f['f0']


def exp_reference(body, value):
    """Independent table-polynomial expression; not the host exp function."""
    x = abs(f32(value)); row = min(int(f32(x + .5)), 40)
    center, *coefficients = struct.unpack_from('<6f', body, TABLE + 24*row)
    remainder = f32(x-center); power = remainder; result = coefficients[0]
    for coefficient in coefficients:
        result = f32(result + coefficient*power)
        power = f32(power*remainder)
    return f32(1/result) if value < 0 else result


def power_reference(base, exponent):
    x, y = f32(base), abs(f32(exponent)); integer = int(y)
    low = x
    for _ in range(max(integer-1, 0)):
        low = f32(low*x)
    difference = -f32(low - x*low)
    result = f32(low + f32(y-f32(integer))*difference)
    return f32(1/result) if exponent < 0 else result


def math_kernels(k):
    trace = hashlib.sha256(); maximum_error = 0.0
    # Complete table plus boundaries immediately around each half-integer.
    values = {f32(i/16) for i in range(-640, 641)}
    for row in range(41):
        center = f32(row + .5); raw = bits(center)
        for delta in (-1, 0, 1):
            x = struct.unpack('<f', struct.pack('<I', raw+delta))[0]
            values.update((x, -x))
    values.update((-64.0, -48.0, -41.0, 41.0, 48.0, 64.0))
    for row in range(ROWS):
        center, *coefficients = struct.unpack_from('<6f', k.body, TABLE+24*row)
        assert center == row
        # Independent recognition of the coefficient scale, not its physical unit.
        for degree, coefficient in enumerate(coefficients):
            assert math.isclose(coefficient, math.exp(row)/math.factorial(degree+1), rel_tol=2e-7)
    for value in sorted(values):
        actual = k.exponential(value); expected = exp_reference(k.body, value)
        assert bits(actual) == bits(expected), ('exponential', value, actual, expected)
        if abs(value) <= 40.5:
            error = abs(actual/math.exp(value)-1)
            maximum_error = max(maximum_error, error)
            assert error < 4e-5
        trace.update(struct.pack('<2f', value, actual))
    powers = zero_exponent = 0
    for base, exponent in itertools.product((.25,.5,.75,1,1.25,1.5,2,4),
            (-8,-4,-2.75,-2,-1.5,-1,-.75,-.25,0,.25,.75,1,1.5,2,2.75,4,8)):
        actual = k.power(base, exponent); expected = power_reference(base, exponent)
        assert bits(actual) == bits(expected), ('power', base, exponent, actual, expected)
        if exponent == 0:
            assert actual == base; zero_exponent += 1
        trace.update(struct.pack('<3f', base, exponent, actual)); powers += 1
    return dict(exponential_cases=len(values), table_rows=ROWS,
        table_sha256=hashlib.sha256(k.body[TABLE:TABLE+24*ROWS]).hexdigest(),
        maximum_sampled_relative_error_within_40_5=maximum_error,
        power_cases=powers, zero_exponent_returns_base_cases=zero_exponent,
        table_index_cap_is_not_output_clamping=True, not_host_exp_or_pow=True,
        trace_sha256=trace.hexdigest())


def model_inputs(k):
    trace = hashlib.sha256(); simple = blended = powered = 0
    tables = ((.125,-.25,2,.03125,.0625,32),(-.25,.125,.5,-.03125,.0625,32))
    def argument(c, v, t):
        a = f32(c[4] + c[3]*v); b = f32(c[0] + c[1]*v)
        return f32(f32(a*f32(t-c[5])) + v*b)
    for c,v,t,activity,suppression in itertools.product(tables,(.5,1,1.5),(0,32,64),
            (0,.25,1),(0,.5,1)):
        value = k.simple(c,v,t,activity)
        expected = f32(activity*f32(c[2]*exp_reference(k.body,argument(c,v,t))))
        assert bits(value) == bits(expected); simple += 1
        bound = 1.25
        value2 = k.blended(c,v,bound,t,activity,suppression)
        first = f32(c[2]*exp_reference(k.body,argument(c,v,t)))
        second = f32(c[2]*exp_reference(k.body,argument(c,bound,t)))
        weight = f32(f32(1-activity)*f32(1-suppression))
        expected2 = f32(f32(second*weight)+activity*first)
        assert bits(value2) == bits(expected2)
        trace.update(struct.pack('<2f',value,value2)); blended += 1
    for c,v,t in itertools.product(((.5,4,16,1.5),(2,-4,16,.75)),(.5,1,1.5),(0,16,32,64)):
        value = k.power_temperature(c,v,t)
        exponent = f32(c[1]*f32(1/f32(t+c[2])))
        expected = f32(power_reference(v,c[3])*f32(c[0]*exp_reference(k.body,exponent)))
        assert bits(value) == bits(expected)
        trace.update(struct.pack('<f',value)); powered += 1
    # Bind the independently replayed helper entry points to the actual producers.
    for caller, callee in ((0x2c7e8,0x326bc),(0x2c7ff,0x2bfe8),
            (0x2cccd,0x32684),(0x2cce7,0x326bc),(0x2cde6,0x32684)):
        k.check_call(caller,callee)
    return dict(simple_model_cases=simple, blended_model_cases=blended,
        power_temperature_cases=powered, core_rows=[0], ccd_rows=[0,1],
        exponential_outputs_are_fixtures=False, coefficient_inputs_are_synthetic=True,
        calibration_and_physical_units_verified=False, trace_sha256=trace.hexdigest())


def producer_composition(k):
    """Connect caller loads, computed helper results and the three stored rows.

    Calls cross an explicit f0 return boundary, not an emulated register window.
    The two power-temperature results are parallel state, not the row-0 sum.
    """
    cases = words = 0; trace = hashlib.sha256()
    tables = ((.125,-.25,2,.03125,.0625,32),(-.25,.125,.5,-.03125,.0625,32))
    auxiliary = (.5,4,16,1.5)

    def call(vm, address, target):
        k.check_call(address, target)
        coefficients = tuple(vm.getf(vm.r['a10']+4*i) for i in range(4 if target==0x326bc else 6))
        if target == 0x326bc:
            result = k.power_temperature(coefficients,vm.f['f0'],vm.f['f1'])
        elif target == 0x32684:
            result = k.simple(coefficients,vm.f['f0'],vm.f['f1'],vm.f['f2'])
        else:
            assert target == 0x2bfe8
            result = k.blended(coefficients,vm.f['f0'],vm.f['f1'],vm.f['f2'],vm.f['f3'],vm.getf(0x12e24))
        vm.f['f0'] = result
        return result

    for c,v,t,activity,suppression,ceiling in itertools.product(tables,(.5,1,1.5),
            (0,32,64),(0,.25,f32(.3),1),(0,.5),(.75,1.25)):
        vm = k.vm(); core = H['CORE']
        for address,coefficients in ((0x12924,c),(0x12a0c,auxiliary),
                (0x12984,c),(0x12a1c,auxiliary),(0x129cc,c)):
            for i,value in enumerate(coefficients): vm.putf(address+4*i,value)
        for address,value in ((core,v),(core+0x7c,1),(core+0x20,t),
                (core+0x64,activity),(core+0xd0,activity),(core+0x10,19),
                (0x12b10,ceiling),(0x12e24,suppression)):
            vm.putf(address,value)
        vm.r.update(a2=core,a6=0x12924,a7=0x12a0c)
        vm.run(0x2c7da,0x2c7e8)
        core_aux = call(vm,0x2c7e8,0x326bc)
        vm.run(0x2c7eb,0x2c7ff)
        core_leak = call(vm,0x2c7ff,0x2bfe8)
        vm.run(0x2c802,0x2c808)
        assert vm.getf(core+0x1c)==core_aux and vm.getf(core+8)==core_leak
        vm.f.update(f1=0,f2=.25,f3=0,f7=.125,f8=ceiling)
        vm.run(0x2c83c,0x2c86f)
        dynamic_base = f32(v*f32(.25+v*.125))
        current = f32(core_leak+f32(activity*dynamic_base))
        expected = f32(max(0,f32(f32(ceiling-v)*current))+current*v)
        assert bits(vm.getf(core+0x10))==bits(expected)
        assert vm.getf(core+0x18)==19

        for address,value in ((0x12b10,v),(0x1280c,t),(0x1296c,.125),
                (0x12970,.25),(0x1286c,activity)):
            vm.putf(address,value)
        vm.put(0x7820,1,1)
        vm.run(0x2ccb8,0x2cccd)
        ccd0_leak = call(vm,0x2cccd,0x32684)
        vm.run(0x2ccd0,0x2cce7)
        ccd0_aux = call(vm,0x2cce7,0x326bc)
        vm.run(0x2ccea,0x2cd14)
        ccd0 = f32(f32(ccd0_leak+f32(activity*dynamic_base))*v)
        assert bits(vm.getf(0x12804))==bits(ccd0)
        assert vm.getf(0x12808)==ccd0_aux

        for address,value in ((0x8030,v),(0x129fc,.125),(0x12a00,.25),
                (0x12860,activity),(0x12b28,ceiling)):
            vm.putf(address,value)
        vm.run(0x2cdc8,0x2cde6)
        ccd1_leak = call(vm,0x2cde6,0x32684)
        vm.run(0x2cde9,0x2ce28)
        current1 = f32(ccd1_leak+activity*dynamic_base)
        ccd1 = f32(f32(current1*v)+max(0,f32(current1*f32(ceiling-v))))
        assert bits(vm.getf(0x12848))==bits(ccd1)
        trace.update(struct.pack('<5f',expected,ccd0,ccd1,core_aux,ccd0_aux))
        cases += 1; words += 3
    return dict(cases=cases,checked_model_words=words,core_rows=[0],ccd_rows=[0,1],
        model_helper_outputs_are_fixtures=False,coefficient_inputs_are_synthetic=True,
        auxiliary_power_results_feed_row0=False,complete_function_execution=False,
        physical_units_verified=False,trace_sha256=trace.hexdigest())


def configuration(k):
    # 75h first expands two unsigned halves; zero upper half reuses the lower.
    cases = selections = wrapped_codes = retained = 0; trace = hashlib.sha256()
    offset = struct.unpack_from('<f',k.body,0x18d6c)[0]
    gain = struct.unpack_from('<f',k.body,0x18d70)[0]
    assert bits(offset)==0xbefeb1e6 and bits(gain)==0x43a8fe98
    quantize = lambda v: int(f32(f32(v+offset)*gain))
    for low,high in itertools.product((498,499,500,750,1000,1250,1253,1300,2000,65535),
            (0,498,500,750,1000,1250,2000,65535)):
        vm=k.vm();vm.r['a10']=low|(high<<16)
        vm.run(0x2bebc,0x2bee8)
        first,second=f32(low*f32(.001)),f32((high or low)*f32(.001))
        codes=[]
        for call,begin,end,value in ((0x2bee8,0x2beeb,0x2bef1,first),(0x2bef1,0x2bef4,0x2bef7,second)):
            k.check_call(call,0x24984)
            leaf=k.vm();leaf.f['f0']=vm.f['f0'];leaf.run(0x24984,0x2499c)
            raw=leaf.r['a2'];assert raw==quantize(value)
            codes.append(raw&255);wrapped_codes+=raw>255
            vm.r['a10']=raw;vm.run(begin,end)
        assert vm.getf(0x119ac)==first and vm.getf(0x119b0)==second
        assert vm.get(0x119bc,1)==codes[0] and vm.get(0x119bd,1)==codes[1]
        for selector in (.5,1,1.5,2):
            vm.r.update(a4=0x113dc,a2=7)
            vm.putf(0x11428,selector);vm.putf(0x119b4,1)
            vm.put(0x32001c0,23)
            vm.run(0x2a494,0x2a4d8)
            chosen=int(selector>1)
            assert vm.getf(0x11728)==(second if chosen else first)
            assert vm.get(0x11725,1)==codes[chosen]
            # Equal quantized codes can conceal a distinct incoming float.
            vm.r.update(a3=0x11644,a4=0x1171c,a5=0x7820,a7=0x78c)
            vm.put(0x1164d,codes[chosen],1);vm.putf(0x8030,-17)
            vm.run(0x27c6f,0x27ca3);vm.run(0x27fa4,0x27fd3)
            assert vm.getf(0x8030)==-17
            retained+=1;selections+=1
        trace.update(struct.pack('<2I2f2I',low,high,first,second,*codes));cases+=1
    return dict(command_cases=cases, selector_cases=selections,
        unchanged_code_retained_cases=retained, truncated_to_byte_cases=wrapped_codes,
        code_formula='low8(trunc(float32(float32(value + offset) * gain)))',
        offset=offset,gain=gain,mp5_command='0x75',
        incoming_values_are_not_redecoded_from_code=True,
        no_electrical_range_inferred=True, peripheral_programming_replayed=False,
        physical_unit_verified=False, trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('mp5','study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();body=a.mp5.read_bytes();code=T['instructions'](a.study,body,H['SHA'])
    k=Kernels(body,code)
    report=dict(schema=1,source_sha256=H['SHA'],hardware_execution=False,
        math_kernels=math_kernels(k),model_inputs=model_inputs(k),
        producer_composition=producer_composition(k),configuration=configuration(k),
        reciprocal_results_are_fixtures=True,reciprocal_fixture_count=k.reciprocal_fixtures,
        fp_exceptions_emulated=False,new_physical_units={},runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__=='__main__':main()
