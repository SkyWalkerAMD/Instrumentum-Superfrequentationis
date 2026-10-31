#!/usr/bin/env python3
"""Byte-checked counter units and complete PM export; synthetic memory only.

The counter event identities, clock source, live validity and OS core mapping
remain unknown. Reciprocal instructions are explicit exact-factor fixtures,
not approximate FPU emulation. No firmware or peripheral is run on hardware.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

H = runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))
Slice, f32, CORE, ACC = (H[k] for k in ('Slice', 'f32', 'CORE', 'ACC'))
SHA = H['SHA']


def export_path(body, code):
    cases = words = 0
    trace = hashlib.sha256()
    for width, ordinal in ((1, 0), (2, 0), (2, 1)):
        for samples in (0, 1, 2, 4, 8, 16):
            vm = Slice(body, code)
            before = [(i+1)/16*max(samples, 1) for i in range(156)]
            for i, value in enumerate(before): vm.putf(ACC+4*i, value)
            vm.put(ACC+0x270, samples)
            values = H['export_snapshot'](vm, width, ordinal)
            for i, actual in enumerate(values):
                expected = before[i]/max(samples, 1)*(100 if 40 <= i < 64 or i == 155 else 1)
                assert actual == expected, (i, actual, expected)
                trace.update(struct.pack('<4If', width, ordinal, samples, i, actual))
                words += 1
            assert vm.get(ACC+0x270) == samples
            assert vm.trace.count(0x2f5cf) == 1  # once, not once per core row
            assert all(pc in vm.trace for pc in (0x2f5cc, 0x2f5cf, 0x2f5d8, 0x2f650))
            cases += 1
    return dict(cases=cases, exported_words=words, sample_counts=[0,1,2,4,8,16],
                ccd_row_11_scale=100, late_scale_pc='0x2f5cf',
                float_instructions_skipped=False, reciprocal_helper_stubbed=True,
                trace_sha256=trace.hexdigest())


def event_percentages(body, code):
    cases = wrapped = zero_denominator = 0
    trace = hashlib.sha256()
    for elapsed, denominator, variant in itertools.product((100,400,1600), (0,1,8,64,256), (0,1)):
        nums = (0, 1, denominator//2, denominator, 2*denominator)
        for first, second in itertools.product(nums, repeat=2):
            vm = Slice(body, code); vm.r['a4'] = CORE
            vm.f.update(f0=0, f1=1, f2=100/elapsed, f7=100,
                        f9=struct.unpack_from('<f',body,0x18d48)[0])
            # Two starting points exercise ordinary and wrapping uint32 deltas.
            old = 100 if not variant else 0xfffffffc
            deltas = (denominator, denominator//4, denominator//2, denominator, denominator*2)
            for (prior, current), delta in zip(((0x110,0x160),(0x10c,0x158),
                    (0x114,0x168),(0x118,0x170),(0x11c,0x188)), deltas):
                vm.put(CORE+prior, old); vm.put(CORE+current, (old+delta)&0xffffffff)
            vm.put(CORE+0x1a8, first); vm.put(CORE+0x1ac, second)
            vm.run(0x2c54b, 0x2c56f if denominator else 0x2c578)
            if denominator:
                rate = (100/elapsed)*denominator
                assert vm.f['f3'] == rate
                vm.f['f3'] = 1/rate  # dyadic, exact reciprocal fixture
                vm.run(0x2c572, 0x2c578)
            else:
                assert vm.f['f3'] == 0
                zero_denominator += 1
            vm.run(0x2c578, 0x2c617)
            expected = [min(100, v*100/denominator) if denominator else 0 for v in (first,second)]
            actual = [vm.getf(CORE+offset) for offset in (0xe8,0xec)]
            assert actual == expected, (elapsed, denominator, first, second, actual, expected)
            for (prior,current), delta in zip(((0x110,0x160),(0x10c,0x158),
                    (0x114,0x168),(0x118,0x170),(0x11c,0x188)), deltas):
                assert vm.get(CORE+prior) == vm.get(CORE+current)
            cases += 1; wrapped += int(variant and denominator > 4)
            trace.update(struct.pack('<5I2f', elapsed, denominator, variant, first, second, *actual))
    return dict(cases=cases, wrapped_counter_cases=wrapped, zero_denominator_cases=zero_denominator,
                core_rows=[15,16], state_offsets=['0xe8','0xec'], unit='%',
                formula='min(100, counter * 100 / reference counter delta); zero reciprocal branch gives zero',
                reciprocal_is_exact_fixture=True, counter_event_meanings_verified=False,
                trace_sha256=trace.hexdigest())


def partition_fractions(body, code):
    cases = 0; trace = hashlib.sha256()
    for policy, threshold, selected, other, residency in itertools.product(
            (0,1), (0.25,0.5,1), (0,0.25,0.75,1), (0,0.25,0.75,1), (0,0.25,0.75,1)):
        vm = Slice(body, code); vm.r.update(a4=CORE, a6=policy, a9=0x12dac)
        vm.putf(0x12dac+0x74, threshold)
        vm.f.update(f0=0, f1=1, f2=selected, f4=selected, f3=other, f11=residency)
        if selected <= threshold:
            pc = 0x2c6c9 if policy else 0x2c6e4
            vm.run(0x2c6be, pc)
            vm.f['f13' if policy else 'f15'] = 1/threshold
            vm.run(pc+3, 0x2c734)
            combined = other*selected/threshold
        else:
            vm.run(0x2c6be, 0x2c734)
            combined = max(selected, other)
        first = min(1, combined*residency)
        second = max(0, min(1, residency-first))
        assert vm.getf(CORE+0x108) == first and vm.getf(CORE+0x104) == second
        assert first+second == residency
        cases += 1
        trace.update(struct.pack('<I6f',policy,threshold,selected,other,residency,first,second))
    return dict(cases=cases, core_rows=[13,14], state_offsets=['0x108','0x104'], unit='ratio',
                interpretation='bounded partitions of the C0 candidate fraction; event identities unknown',
                policy_selector_meaning_verified=False, reciprocal_is_exact_fixture=True,
                trace_sha256=trace.hexdigest())


def quantized_percentage(body, code):
    cases = retained = wrapped = 0; trace = hashlib.sha256()
    assert struct.unpack_from('<I',body,0x194ac)[0] == 12800
    assert struct.unpack_from('<I',body,0x19008)[0] == 0x40000
    values = (0,1,2,127,128,129,4096,65535,131072,262143)
    for numerator, denominator, old in itertools.product(values,values,(0,7,262140)):
        vm = Slice(body,code); current = (old+denominator)&0x3ffff
        prior_num = (old+13)&0x3ffff; current_num = (prior_num+numerator)&0x3ffff
        vm.r.update(a4=CORE,a14=current,a3=old,a15=current_num)
        vm.f['f15']=123.5  # independent preceding rate, stored at +0xd0
        vm.put(CORE+0xd8,prior_num); vm.putf(CORE+0xdc,17.25)
        vm.run(0x2c51b,0x2c54b)
        expected = f32(((numerator*12800)//denominator)/128) if denominator else 17.25
        assert vm.getf(CORE+0xdc) == expected
        assert vm.get(CORE+0xd8) == current_num
        assert vm.getf(CORE+0xd0) == 123.5
        cases += 1; retained += int(not denominator)
        wrapped += int(current < old or current_num < prior_num)
        trace.update(struct.pack('<3If',numerator,denominator,old,expected))
    return dict(cases=cases, wrapped_counter_cases=wrapped, zero_denominator_retained_cases=retained,
                core_row=17, state_offset='0xdc', unit='%',
                formula='floor(delta_numerator * 12800 / delta_denominator) / 128',
                counter_width_bits=18, integer_stage_percentage_quantum='1/128; then binary32 rounding', clamp_to_100=False,
                zero_denominator='retain previous value',counter_event_meanings_verified=False,
                trace_sha256=trace.hexdigest())


def accumulator_bridge(body, code):
    cases = words = 0; trace = hashlib.sha256()
    sources = {13:(0x108,0.625),14:(0x104,0.125),15:(0xe8,25),16:(0xec,75),17:(0xdc,150)}
    for core in range(8):
        vm=Slice(body,code); H['seed_accumulation'](vm)
        for row,(offset,value) in sources.items(): vm.putf(CORE+0x1bc*core+offset,value)
        for _ in range(4): vm.run(0x2f370,0x2f536)
        result=H['export_snapshot'](vm,2,core%2)
        for row,(_,value) in sources.items():
            assert result[row*8+core] == value
            words += 1; trace.update(struct.pack('<2If',core,row,value))
        cases += 1
    return dict(cases=cases, new_unit_words=words, samples_per_case=4,
                core_rows=list(sources), additional_scale_applied=False,
                trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('mp5','study','output'): p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();body=a.mp5.read_bytes();assert hashlib.sha256(body).hexdigest()==SHA
    study=json.loads(a.study.read_text());assert study['source_sha256']==SHA
    code={int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
    for pc,ins in code.items():
        raw=bytes.fromhex(ins['bytes']);assert body[pc:pc+len(raw)]==raw
    report=dict(schema=1,source_sha256=SHA,hardware_execution=False,
        export=export_path(body,code),event_percentages=event_percentages(body,code),
        partition_fractions=partition_fractions(body,code),quantized_percentage=quantized_percentage(body,code),
        accumulator_bridge=accumulator_bridge(body,code),
        new_core_units={'13':'ratio','14':'ratio','15':'%','16':'%','17':'%'},
        corrected_ccd_units={'11':'%'},physical_core_mapping_verified=False,
        counter_event_meanings_verified=False,live_validity_verified=False,
        reciprocal_numeric_accuracy_verified=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__=='__main__':main()
