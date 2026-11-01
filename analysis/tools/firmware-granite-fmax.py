#!/usr/bin/env python3
"""Byte-checked Granite 98.84.0 Fmax policy and capability experiments.

Only synthetic RAM is changed. Fuse values, mailbox access, policy permission
and MP5 transport are explicit fixtures; no hardware or firmware is executed.
6E is a stored policy ceiling in MHz, not an operating-clock measurement.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
H = runpy.run_path(str(HERE / 'firmware-granite-semantics.py'))
R = runpy.run_path(str(HERE / 'firmware-granite-registers.py'))
Slice, f32 = H['Slice'], H['f32']
STORAGE = 0xaee4
bits = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]


def policy(body, code):
    cases = accepted = rejected = clamped = downward = 0
    seen = set(); trace = hashlib.sha256()
    defaults = struct.unpack_from('<4f', body, 0x11208)
    assert defaults == (0, f32(.025), f32(.2), -1)
    # The alternative coefficients exercise the RAM-selected policy branch;
    # they are fixtures, not values observed on a customer's machine.
    for fuse, argument, profile, guard, coefficients in itertools.product(
            (0, 1, 20, 40, 200, 220, 240, 255),
            (0, 1, 24, 25, 26, 999, 1000, 1001, 3999, 4000, 4001,
             4999, 5000, 5001, 5499, 5500, 5501, 5699, 5700, 5701,
             6000, 6375, 6575, 6576, 1048575, 0xffffffff),
            (0, 1, 2), (False, True), (defaults, (0, .03125, .125, -.5))):
        vm = Slice(body, code); vm.r.update(a1=0x50000, a2=0x1234)
        vm.put(0x8197, int(profile != 0), 1)
        vm.put(0x3210064, 0x80000000 if profile == 1 else 0)
        for offset, value in enumerate(coefficients): vm.putf(0x11208+4*offset, value)
        vm.putf(STORAGE, 5.75); vm.putf(0xaf04, 3); vm.putf(0xaee8, 1.2)
        vm.put(0xb8aa, 100, 1)
        replies = []; broadcasts = []
        def input_word(v):
            assert v.r['a10'] == 0x1234
            v.r['a10'] = argument
        def fuse_word(v):
            assert v.r['a10'] == 0x2c33 and v.r['a11'] == 8
            v.r['a10'] = fuse
        def permission(v):
            assert v.r['a10'] == 2
            v.r['a10'] = int(guard)
        def reply(v):
            assert v.r['a10'] == 0x1234
            replies.append(v.r['a11'])
        def broadcast(v):
            assert (v.r['a10'], v.r['a11'], v.r['a12']) == (0x90, 0x50000, 6)
            broadcasts.append(v.getf(0x50008))
        vm.run(0x29e54, None, hooks={0x15dc:input_word, 0x1e22c:fuse_word,
            0x2c910:permission, 0x14ec:reply, 0x1d3e4:broadcast})
        requested = f32(f32(argument)*f32(.001))
        sentinel, factor, upper, lower = coefficients if profile == 2 else defaults
        base = f32(fuse*factor)
        selected = max(f32(base+lower), min(requested, f32(base+upper)))
        offset = f32(selected-base)
        permitted = argument != 0 and (profile != 2 or guard or (selected != sentinel and offset < sentinel))
        expected = base if selected == sentinel else selected
        assert replies == [1 if permitted else 0xfd]
        assert vm.getf(STORAGE) == (expected if permitted else 5.75)
        assert broadcasts == ([expected] if permitted else [])
        if permitted:
            assert all(vm.getf(0xae88+4*i) == expected for i in range(18))
            result = []
            def getter(v):
                assert v.r['a10'] == 0x1234
                result.append(v.r['a11'])
            vm.run(0x2a7c8, None, hooks={0x1580:getter, 0x14ec:reply})
            assert result == [round(f32(expected*1000)) & 0xffffffff]
            assert replies == [1, 1]
            accepted += 1; clamped += result[0] != argument
            downward += profile == 2 and not guard
        else:
            rejected += 1
        seen.update(vm.trace)
        trace.update(struct.pack('<6I', fuse, argument, profile, guard, replies[0], bits(vm.getf(STORAGE))))
        cases += 1
    # Every return, default/custom path, zero rejection and downward-only path
    # is exercised. The policy helper itself remains an explicit boolean input.
    assert {0x29f7e,0x29f94,0x29fbf,0x29fb4,0x29ef3,0x29eb2} <= seen
    return dict(cases=cases, accepted=accepted, rejected=rejected,
        clamped_or_sentinel_cases=clamped, downward_only_accepted=downward,
        shared_storage=hex(STORAGE), units='integer MHz',
        transport_executed=False, fuse_and_permission_are_fixtures=True,
        stored_policy_readback=True, operating_clock_measured=False,
        trace_sha256=trace.hexdigest())


def capabilities(body, code):
    ids = (0x2cd7,0x2cd9,0x2cda,0x2cdb,0x2cd8,0x2cdc,
           0x2cdd,0x2cde,0x2cdf,0x3706,0x3707)
    cases = 0; trace = hashlib.sha256()
    def check(mask, gate, suppress, manual):
        nonlocal cases
        vm = Slice(body, code); vm.r.update(a1=0x50000,a2=0x1234)
        vm.put(0x817e, int(gate == 0), 1); vm.put(0x8170, int(gate == 1), 1)
        vm.put(0x13548, suppress & 1, 1); vm.put(0x1351f, (suppress >> 1) & 1, 1)
        vm.put(0xae68, manual, 1)
        result = []; responses = []
        def fuse(v):
            assert v.r['a11'] == 1 and v.r['a10'] in ids
            v.r['a10'] = (mask >> ids.index(v.r['a10'])) & 1
        def arg(v):
            assert v.r['a10'] == 0x1234
            result.append(v.r['a11'])
        def reply(v):
            assert v.r['a10'] == 0x1234
            responses.append(v.r['a11'])
        vm.run(0x1f36c, None, hooks={0x1e22c:fuse,0x1580:arg,0x14ec:reply})
        expected = mask if gate < 2 else mask & 32
        if suppress & 1: expected &= ~1
        if suppress & 2: expected &= ~16
        expected |= 0x10000 if manual else 0
        assert result == [expected] and responses == [1]
        trace.update(struct.pack('<5I',mask,gate,suppress,manual,expected)); cases += 1
    for mask, manual in itertools.product(range(2048), (0,1)): check(mask,0,0,manual)
    for mask, gate, suppress, manual in itertools.product((0,4,0x7ff),(0,1,2),range(4),(0,1,255)):
        check(mask,gate,suppress,manual)
    # 5D/5E are examined for flag provenance only; neither is exposed by this
    # runtime feature. They set/clear the byte read into 6F bit 16.
    assert code[0x2a715]['text'] == 's8i a3,a4,0x38'
    assert code[0x2a6cb]['text'] == 'movi.n a3,0x1'
    assert code[0x2a786]['text'] == 's8i a9,a3,0x38'
    assert code[0x2a784]['text'] == 'movi.n a9,0x0'
    assert struct.unpack_from('<I',body,0x115ec)[0] == 0x10000
    return dict(cases=cases, pbo_bit=2, manual_oc_bit=16,
        pbo_fuse_id='0x2cda', manual_oc_state_byte='0xae68',
        trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu','smu-study','output'): p.add_argument('--'+name,type=Path,required=True)
    a = p.parse_args(); body = a.smu.read_bytes()
    assert hashlib.sha256(body).hexdigest() == R['SHA']
    study = json.loads(a.smu_study.read_text()); assert study['source_sha256'] == R['SHA']
    code = {int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
    for address, i in code.items():
        raw = bytes.fromhex(i['bytes']); assert body[address:address+len(raw)] == raw
    for command, handler in ((0x6e,0x2a7c8),(0x6f,0x1f36c),(0x70,0x29e54)):
        assert struct.unpack_from('<I',body,0x6fa4+8*command)[0] == handler
    report = dict(schema=1, smu_sha256=R['SHA'], hardware_execution=False,
        policy=policy(body,code), capabilities=capabilities(body,code),
        hardware_validation=False, independent_rail_readback=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
