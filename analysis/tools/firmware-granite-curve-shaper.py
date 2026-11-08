#!/usr/bin/env python3
"""Byte-checked Granite Curve Shaper count storage and forwarding.

Synthetic RAM, fuse replies and transport boundaries only. No physical
frequency/temperature thresholds, voltage unit or hardware effect is assumed.
"""
import argparse
import bisect
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
H = runpy.run_path(str(HERE/'firmware-granite-semantics.py'))
T = runpy.run_path(str(HERE/'firmware-granite-topology.py'))
S = runpy.run_path(str(HERE/'firmware-granite-pbo-scalar.py'))
Slice = H['Slice']
f32 = H['f32']
BASELINE, STORED, MP5_STORED = 0x13984, 0x13993, 0x19bd4
signed8 = lambda v: struct.unpack('<b', bytes([v & 255]))[0]
clamp = lambda v: max(-50, min(30, signed8(v)))


def sext_cases(smu, scode, mp5, mcode):
    trace = hashlib.sha256(); count = 0; identities = []
    for body, code, pc, reg, raw in (
            (smu, scode, 0x2ab9a, 'a11', '00bb23'),
            (smu, scode, 0x2ab9d, 'a12', '00cc23'),
            (smu, scode, 0x2aba3, 'a12', '00cc23'),
            (mp5, mcode, 0x2a073, 'a12', '00cc23')):
        encoded = bytes.fromhex(raw); word = int.from_bytes(encoded, 'little')
        assert body[pc:pc+3] == encoded == bytes.fromhex(code[pc]['bytes'])
        # Independent RRR field decode. QEMU tp7 = unsigned t + 7, and
        # translate_sext extracts immediate+1 bits. No invented opcode data.
        assert word & 0xff000f == 0x230000
        assert (word >> 12) & 15 == (word >> 8) & 15 == int(reg[1:])
        assert ((word >> 4) & 15)+7 == 7
        assert code[pc]['text'] == f'sext {reg},{reg},7'
        identities.append(dict(address=hex(pc), bytes=raw, sign_bit=7, signed_bits=8))
        for low, high in itertools.product(range(256), (0, 0xa5a5a500, 0xffffff00)):
            vm = Slice(body, code); vm.r[reg] = high | low
            vm.run(pc, pc+3)
            assert vm.r[reg] == signed8(low) & 0xffffffff
            trace.update(struct.pack('<3I', pc, high | low, vm.r[reg])); count += 1
    return dict(cases=count, instruction_identities=identities,
        instruction_immediates_tested=[7], oracle='struct.unpack little-endian int8',
        trace_sha256=trace.hexdigest())


def getter(body, code, ram):
    vm = Slice(body, code); vm.ram = ram; vm.r.update(a1=0x50000, a2=0x1234)
    words = [0xdeadbeef]*6; writes = []; replies = []
    def output(v):
        assert v.r['a10'] == 0x1234
        index, value = v.r['a11'], v.r['a12']
        assert 0 <= index < 6; words[index] = value; writes.append(index)
    def response(v):
        assert v.r['a10'] == 0x1234; replies.append(v.r['a11'])
    vm.run(0x2ac14, None, hooks={0x15ac: output, 0x14ec: response})
    assert writes == list(range(5)) and replies == [1] and words[5] == 0xdeadbeef
    return words


def getter_cases(body, code):
    trace = hashlib.sha256(); count = 0
    for value in range(256):
        vm = Slice(body, code)
        margins = [(value+11*i) & 255 for i in range(15)]
        for i, margin in enumerate(margins): vm.put(STORED+i, margin, 1)
        before = vm.ram.copy(); actual = getter(body, code, vm.ram)
        expected = [i | sum(margins[3*i+j] << (8*(j+1)) for j in range(3)) for i in range(5)]
        assert actual[:5] == expected
        assert all(vm.get(STORED+i, 1) == before[STORED+i] for i in range(15))
        trace.update(struct.pack('<6I', value, *actual[:5])); count += 1
    return dict(cases=count, handler='0x2ac14', command='0x84',
        offset_storage=hex(STORED), returned_words=5, sixth_argument_untouched=True,
        all_byte_encodings_each_cell=True, getter_copies_stored_bytes_before_baseline=True,
        transport_acknowledgement=1, physical_device_getter=False,
        trace_sha256=trace.hexdigest())


def mp5_apply(body, code, word, ram):
    vm = Slice(body, code); vm.ram = ram; vm.r.update(a1=0x4f000, a2=0x5678)
    events = []
    def input_word(v):
        assert v.r['a10'] == 0x5678; v.r['a10'] = word
    vm.run(0x2a048, None, hooks={0x11b4: input_word,
        0x29b14: lambda v: events.append('rebuild_fuse_plus_margin_model'),
        0x29f9c: lambda v: events.append('rebuild_core_interpolations'),
        0x2e834: lambda v: events.append('ack')})
    return events


def mp5_cases(body, code):
    trace = hashlib.sha256(); count = 0
    for tier, value in itertools.product(range(5), range(256)):
        vm = Slice(body, code)
        for j in range(3):
            for i in range(5): vm.putf(MP5_STORED+20*j+4*i, -999.)
        margins = [(value+j*79) & 255 for j in range(3)]
        word = tier | sum(x << (8*(j+1)) for j, x in enumerate(margins))
        assert mp5_apply(body, code, word, vm.ram) == ['ack']
        for j in range(3):
            for i in range(5):
                assert vm.getf(MP5_STORED+20*j+4*i) == (signed8(margins[j]) if i == tier else -999.)
        trace.update(struct.pack('<I', word)); count += 1
    commits = 0
    for word in (0x80, 0x81, 0xff, 0xdeadbeef, 0xffffff80):
        vm = Slice(body, code); vm.putf(MP5_STORED, -999.)
        assert mp5_apply(body, code, word, vm.ram) == [
            'rebuild_fuse_plus_margin_model', 'rebuild_core_interpolations', 'ack']
        assert vm.getf(MP5_STORED) == -999.
        commits += 1
    return dict(cases=count, commit_cases=commits, message='0x47', handler='0x2a048',
        storage=hex(MP5_STORED), signed_count_to_float=True,
        storage_matrix='address = 0x19bd4 + 4*tier + 20*column',
        setter_updates_three_cells_only=True, bit7_only_commit_selector=True,
        commit_helpers_are_explicit_boundaries=True, downstream_device_effect_verified=False,
        trace_sha256=trace.hexdigest())


def setter_case(smu, scode, mp5, mcode, tier, flag, triple, mode, baseline, prior):
    vm = Slice(smu, scode); vm.r.update(a1=0x50000, a2=0x1234)
    platform, variant, mask = mode
    vm.put(0x817e, platform, 1); vm.put(0x8170, variant, 1)
    for i in range(15):
        vm.put(BASELINE+i, baseline[i], 1); vm.put(STORED+i, prior[i], 1)
    word = tier | flag | sum((x & 255) << (8*(j+1)) for j, x in enumerate(triple))
    calls = []; replies = []; permissions = []
    def input_word(v):
        assert v.r['a10'] == 0x1234; v.r['a10'] = word
    def permission(v):
        assert v.r['a10'] == 2
        result = S['capability'](smu, scode, vm, mask)
        permissions.append(result); v.r['a10'] = result
    def response(v):
        assert v.r['a10'] == 0x1234; replies.append(v.r['a11'])
    def rebuild(v):
        child = Slice(smu, scode); child.ram = v.ram; child.r['a1'] = 0x4ff00
        def broadcast(c): calls.append((c.r['a10'], c.r['a11']))
        child.run(0x2ab74, None, hooks={0x1d760: broadcast})
    vm.run(0x2abcc, None, hooks={0x15dc: input_word, 0x2c910: permission,
        0x2ab74: rebuild, 0x14ec: response})
    permitted = bool((platform or variant == 1) and mask & 4)
    accepted = tier < 5 and permitted
    assert permissions == ([int(permitted)] if tier < 5 else [])
    assert replies == ([1] if accepted else [255])
    expected = list(prior)
    if accepted:
        expected[3*tier:3*tier+3] = [x & 255 for x in triple]
        expected = [clamp(x) & 255 for x in expected]
    assert [vm.get(STORED+i, 1) for i in range(15)] == expected
    assert [vm.get(BASELINE+i, 1) for i in range(15)] == list(baseline)
    packed = [i | sum(((signed8(baseline[3*i+j])+signed8(expected[3*i+j])) & 255)
        << (8*(j+1)) for j in range(3)) for i in range(5)]
    assert calls == ([(0x47, x) for x in packed]+[(0x47, 0x80)] if accepted else [])
    actual = getter(smu, scode, vm.ram)
    assert actual[:5] == [i | sum(expected[3*i+j] << (8*(j+1)) for j in range(3)) for i in range(5)]
    changed_unselected = sum(expected[i] != prior[i] for i in range(15) if i//3 != tier)
    wrapped = sum(not -128 <= signed8(baseline[i])+signed8(expected[i]) <= 127 for i in range(15))
    if accepted:
        target = Slice(mp5, mcode)
        for message, payload in calls:
            assert message == 0x47; mp5_apply(mp5, mcode, payload, target.ram)
        for i in range(15):
            assert target.getf(MP5_STORED+4*(i//3)+20*(i%3)) == signed8(signed8(baseline[i])+signed8(expected[i]))
    return word, accepted, actual[:5], changed_unselected, wrapped


def setter_cases(smu, scode, mp5, mcode):
    trace = hashlib.sha256(); count = rejects = changed = wraps = 0
    modes = ((0, 0, 4), (1, 0, 0), (1, 0, 4), (0, 1, 4))
    baselines = ((0,)*15, tuple((i*7-49) & 255 for i in range(15)), (127,)*15, (128,)*15)
    priors = ((0,)*15, tuple((128, 127, 31, 205, 0)[i%5] for i in range(15)))
    triples = ((0,0,0),(-50,0,30),(-128,-51,127),(-49,29,31),(127,127,127))
    for tier, flag, triple, mode, baseline, prior in itertools.product(
            (0,1,2,3,4,5,6,127), (0,128), triples, modes, baselines, priors):
        word, accepted, values, changed_other, wrapped = setter_case(
            smu, scode, mp5, mcode, tier, flag, triple, mode, baseline, prior)
        trace.update(struct.pack('<10I', word, *mode, accepted, *values))
        count += 1; rejects += not accepted
        changed += bool(accepted and changed_other); wraps += bool(accepted and wrapped)
    return dict(cases=count, rejected_cases=rejects, handler='0x2abcc', command='0xa6',
        permission_bit=2, accepted_margin_counts=[-50,30], input_bit7_ignored=True,
        whole_table_clamped_on_each_accepted_write=True,
        cases_changing_other_tiers=changed, baseline_addition_wrap_cases=wraps,
        forwarded_messages_per_accepted_write=6, all_five_tiers_then_commit=True,
        settings_readback_is_stored_offset_not_forwarded_combined_count=True,
        downstream_success_validation=False, trace_sha256=trace.hexdigest())


def signed_fuse(body, code, value, width):
    child = Slice(body, code); child.r.update(a2=value, a3=width)
    child.run(0x24a34, None, hooks={})
    return child.r['a2']


def signed_fuse_cases(body, code):
    trace = hashlib.sha256(); cases = 0
    for width in (8,10):
        for raw in range(1 << width):
            expected = raw if raw < 1 << (width-1) else raw-(1 << width)
            result = signed_fuse(body,code,raw,width)
            assert result == expected & 0xffffffff
            trace.update(struct.pack('<3I',width,raw,result)); cases += 1
    return dict(cases=cases, helper='0x24a34', signed_bits=[8,10],
        source_reply_is_width_limited_fixture=True, trace_sha256=trace.hexdigest())


def temp_anchors(body, code):
    trace = hashlib.sha256()
    for raw in range(256):
        vm = Slice(body,code); vm.r['a1'] = 0x4f000
        values = [(raw+37*j)&255 for j in range(3)]; calls = []
        def fuse(v):
            key,width = v.r['a10'],v.r['a11']; calls.append((key,width))
            assert width == 8 and key in (0x267d,0x2685,0x268d)
            v.r['a10'] = values[(key-0x267d)//8]
        def sign(v): v.r['a10'] = signed_fuse(body,code,v.r['a10'],v.r['a11'])
        vm.run(0x29b14,0x29b3d,hooks={0x2e554:fuse,0x24a34:sign})
        assert calls == [(0x267d+8*j,8) for j in range(3)]
        for j in range(3):
            assert vm.getf(0x19ae0+4*j) == vm.getf(0x19b34+4*j) == signed8(values[j])
        trace.update(struct.pack('<4I',raw,*(vm.get(0x19b34+4*j) for j in range(3))))
    return dict(cases=256, signed_temperature_anchor_fuses=['0x267d/8','0x2685/8','0x268d/8'],
        model_anchors='0x19b34 + 4*column', actual_target_fuse_values_known=False,
        fixed_minus5_50_90_claim=False, trace_sha256=trace.hexdigest())


def region_model(body,code,frequency_codes,fuse_counts,margins,slopes=False):
    vm = Slice(body,code); vm.r['a1'] = 0x4f000
    vm.put(0x18b6c,0x3dcccccd)
    for j in range(3):
        for tier in range(5): vm.putf(MP5_STORED+20*j+4*tier,margins[5*j+tier])
    calls=[]
    def fuse(v):
        key,width=v.r['a10'],v.r['a11']; calls.append((key,width))
        if width==6:
            assert key in tuple(0x25c9+6*i for i in range(5))
            v.r['a10']=frequency_codes[(key-0x25c9)//6]
        else:
            assert width==10
            matches=[(j,(key-base)//10) for j,base in enumerate((0x25e7,0x2619,0x264b))
                if 0<=key-base<=40 and (key-base)%10==0]
            assert len(matches)==1
            j,tier=matches[0];v.r['a10']=fuse_counts[5*j+tier]&1023
    def sign(v):v.r['a10']=signed_fuse(body,code,v.r['a10'],v.r['a11'])
    def reciprocal(v):
        # Explicit reciprocal-result boundary, not emulation of approximate
        # Xtensa RECIP0/Newton refinement or physical FPU flags. Fixtures use
        # nonzero dyadic differences with exactly representable reciprocals.
        denominator=v.f['f0'];result=1.0/denominator
        assert denominator and f32(result)==result
        v.f['f0']=result
    vm.run(0x29bd9,None if slopes else 0x29c57,
        hooks={0x2e554:fuse,0x24a34:sign,0x327b0:reciprocal})
    expected_calls=[]
    for tier in range(5):
        expected_calls.append((0x25c9+6*tier,6))
        expected_calls.extend((base+10*tier,10) for base in (0x25e7,0x2619,0x264b))
    assert calls==expected_calls
    for tier in range(5):
        assert vm.getf(0x19b48+4*tier)==f32(frequency_codes[tier]*f32(.1))
        for j in range(3):
            assert vm.getf(0x19b5c+20*j+4*tier)==fuse_counts[5*j+tier]+margins[5*j+tier]
    return vm


def region_cases(body,code):
    trace=hashlib.sha256();cases=0
    for raw in range(1024):
        freq=[(raw+11*i)&63 for i in range(5)]
        fuse_counts=[(((raw+37*i)&1023)^512)-512 for i in range(15)]
        margins=[signed8(raw+79*i) for i in range(15)]
        vm=region_model(body,code,freq,fuse_counts,margins)
        trace.update(struct.pack('<I',raw))
        for j in range(3):
            for tier in range(5):trace.update(struct.pack('<I',vm.get(0x19b5c+20*j+4*tier)))
        cases+=1
    return dict(cases=cases, rebuild_function='0x29b14',
        executed_slice=['0x29bd9','0x29c57'], all_frequency_codes_each_tier=True,
        all_signed10_fuse_codes_each_cell=True, all_signed8_margin_codes_each_cell=True,
        frequency_anchor_formula='float32(fuse[0x25c9 + 6*tier; width 6] * float32(0.1))',
        frequency_anchor_storage='0x19b48 + 4*tier',
        value_formula='signed10 fuse + signed8 baseline_plus_runtime_offset',
        three_fuse_rows=['0x25e7 + 10*tier','0x2619 + 10*tier','0x264b + 10*tier'],
        active_value_storage='0x19b5c + 20*column + 4*tier',
        physical_target_fuse_values_known=False, trace_sha256=trace.hexdigest())


def interpolation_cases(body,code):
    trace=hashlib.sha256();cases=0
    patterns=[[0]*15]+[[int(i==selected)*16 for i in range(15)] for selected in range(15)]
    patterns += [[16*j+4*tier for j in range(3) for tier in range(5)]]
    freq_sets=((0,10,20,40,60),(0,5,10,20,40),(10,20,30,40,50))
    temp_sets=((-32,0,32),(0,32,64),(-64,-32,0))
    for freq_codes,temps,margins in itertools.product(freq_sets,temp_sets,patterns):
        vm=region_model(body,code,freq_codes,[0]*15,margins,slopes=True)
        freq=[vm.getf(0x19b48+4*i) for i in range(5)]
        for j,temp in enumerate(temps):vm.putf(0x19b34+4*j,temp)
        for j in range(2):vm.putf(0x19b40+4*j,1/(temps[j+1]-temps[j]))
        # Neutralize the separate per-core base-curve term, so the exact
        # original 297a0 result isolates the 15-cell adjustment model.
        vm.put(0x19ade,2,1);vm.putf(0x19ad4,0);vm.putf(0x19ad8,1/32)
        core=0x60000
        for offset in (0x19c,0x1b4,0x1cc,0x1e4,0x1fc,0x210):vm.putf(core+offset,0)
        for temperature,frequency in itertools.product(range(-64,97,16),[i*.5 for i in range(17)]):
            child=Slice(body,code);child.ram=vm.ram;child.r['a2']=core
            child.f.update(f0=float(temperature),f1=frequency)
            child.run(0x297a0,None,hooks={})
            row=[]
            for j in range(3):
                values=margins[5*j:5*j+5]
                if frequency<=freq[0]:v=values[0]
                else:
                    left=max(0,min(3,bisect.bisect_left(freq,frequency)-1))
                    fraction=(frequency-freq[left])/(freq[left+1]-freq[left])
                    v=values[left]+fraction*(values[left+1]-values[left])
                row.append(v)
            low=0 if temperature<temps[1] else 1
            fraction=(temperature-temps[low])/(temps[low+1]-temps[low])
            expected=row[low]+fraction*(row[low+1]-row[low])
            assert child.f['f0']==expected,(freq,temps,margins,temperature,frequency,child.f['f0'],expected)
            trace.update(struct.pack('<3f',temperature,frequency,child.f['f0']));cases+=1
    return dict(cases=cases, original_function='0x297a0',
        input_f0_axis='temperature anchor coordinates',input_f1_axis='frequency anchor coordinates',
        interpolation='linear in frequency, then temperature',
        below_first_frequency_uses_first_value=True, above_last_frequency_extrapolates_last_segment=True,
        temperature_outside_anchors_extrapolates=True, adjacent_cells_overlap=True,
        separate_per_core_curve_term_is_neutral_fixture=True,
        reciprocal_results_are_exact_dyadic_boundaries=True,
        physical_hardware_behavior_observed=False, trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu','smu-study','mp5','mp5-study','output'):
        p.add_argument('--'+name, required=True, type=Path)
    a = p.parse_args(); smu = a.smu.read_bytes(); mp5 = a.mp5.read_bytes()
    scode = T['instructions'](a.smu_study, smu, T['SMU_SHA'])
    mcode = T['instructions'](a.mp5_study, mp5, H['SHA'])
    assert struct.unpack_from('<II', smu, 0x6fa4+8*0x84) == (0x2ac14,6)
    assert struct.unpack_from('<II', smu, 0x6fa4+8*0xa6) == (0x2abcc,6)
    assert struct.unpack_from('<II', mp5, 0x13298+8*0x47) == (0x2a048,2)
    assert struct.unpack_from('<I', smu, 0x12158)[0]+0x78 == BASELINE
    assert struct.unpack_from('<I', smu, 0x1215c)[0]+0x7f == STORED
    assert struct.unpack_from('<I', mp5, 0x191a8)[0] == MP5_STORED
    assert struct.unpack_from('<I', mp5, 0x18b6c)[0] == 0x3dcccccd
    report = dict(schema=1, source_sha256={'smu':T['SMU_SHA'],'mp5':H['SHA']},
        hardware_execution=False, signed8=sext_cases(smu,scode,mp5,mcode),
        getter=getter_cases(smu,scode), mp5_storage=mp5_cases(mp5,mcode),
        setter=setter_cases(smu,scode,mp5,mcode),
        signed_fuse=signed_fuse_cases(mp5,mcode),temperature_anchors=temp_anchors(mp5,mcode),
        region_model=region_cases(mp5,mcode),interpolation=interpolation_cases(mp5,mcode),
        precise_frequency_temperature_thresholds_verified=False,
        counts_are_not_voltage=True, physical_device_readback=False,
        application_write_protocol_enabled=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
