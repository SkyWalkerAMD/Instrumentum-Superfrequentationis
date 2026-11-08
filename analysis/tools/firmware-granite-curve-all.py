#!/usr/bin/env python3
"""Byte-checked Granite all-core CO input, two-target route and MP5 state.

Only synthetic RAM and scripted completion/fuse replies are used. Local
eight-core state writes do not establish OS/physical core mapping, a host
readback contract, electrical units or a successful hardware transition.
"""
import argparse
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
B = runpy.run_path(str(HERE/'firmware-granite-config-route.py'))
Slice = H['Slice']
COUNTS = (-32768, -32767, -51, -50, -49, -1, 0, 1, 29, 30, 31, 49, 50, 51, 32766, 32767)
MODES = ((0, 0, 0, 0, 0), (1, 0x80000000, 0, 0, 0),
         (1, 0, 0, 0, 4), (1, 0, 1, 0, 0), (1, 0, 1, 0, 4), (1, 0, 0, 1, 4))
signed16 = lambda value: struct.unpack('<h', struct.pack('<H', value & 0xffff))[0]


def sext_cases(smu, scode, mp5, mcode):
    trace = hashlib.sha256(); cases = 0; identities = []
    for body, code, address, register, raw_hex in (
            (smu, scode, 0x2bd17, 'a4', '804423'), (mp5, mcode, 0x2f055, 'a10', '80aa23')):
        # Independent RRR field decode from the pinned QEMU ISA module:
        # opcode 0x23xxxx with low nibble 0; r=15:12, s=11:8, t=7:4.
        # OperandSem_opnd_sem_tp7_decode adds 7 to t, then translate_sext
        # extracts immediate+1 signed bits. Thus raw t=8 means sign bit 15.
        raw = bytes.fromhex(raw_hex); word = int.from_bytes(raw, 'little')
        assert body[address:address+3] == raw == bytes.fromhex(code[address]['bytes'])
        assert word & 0xff000f == 0x230000
        assert (word >> 12) & 15 == (word >> 8) & 15 == int(register[1:])
        assert ((word >> 4) & 15)+7 == 15
        assert code[address]['text'] == f'sext {register},{register},15'
        identities.append(dict(address=hex(address), bytes=raw_hex, register=register,
            encoded_t=8, sign_bit=15, extracted_signed_bits=16))
        for low, high in itertools.product(range(65536), (0, 0xa5a50000, 0xffff0000)):
            word = high | low
            vm = Slice(body, code); vm.r[register] = word
            vm.run(address, address+3)
            assert vm.r[register] == signed16(word) & 0xffffffff
            trace.update(struct.pack('<3I', address, word, vm.r[register])); cases += 1
    return dict(cases=cases, all_low16_encodings=True, unrelated_high_bits=3,
        signed16_original_instructions=2, instruction_identities=identities,
        instruction_immediates_tested=[15], signed_result_oracle='struct.unpack little-endian int16',
        trace_sha256=trace.hexdigest())


def smu_case(body, code, argument, mode):
    gate, mmio, platform, variant, mask = mode
    vm = Slice(body, code); vm.r.update(a1=0x50000, a2=0x1234)
    vm.put(0x8197, gate, 1); vm.put(0x3210064, mmio)
    vm.put(0x817e, platform, 1); vm.put(0x8170, variant, 1)
    replies, forwarded, permissions = [], [], []
    def input_word(v):
        assert v.r['a10'] == 0x1234; v.r['a10'] = argument
    def permission(v):
        assert v.r['a10'] == 2
        value = S['capability'](body, code, vm, mask)
        permissions.append(value); v.r['a10'] = value
    def broadcast(v):
        assert v.r['a10'] == 0xf; forwarded.append(v.r['a11'])
    def response(v):
        assert v.r['a10'] == 0x1234; replies.append(v.r['a11'])
    vm.run(0x2bcdc, None, hooks={0x15dc: input_word, 0x2c910: permission,
        0x1d760: broadcast, 0x14ec: response})
    value = signed16(argument); constrained = gate and mmio != 0x80000000
    permitted = not constrained or (platform or variant == 1) and mask & 4
    selected = max(-50, min(30, value)) if constrained and permitted else value
    expected = (argument & 0xffff0000) | (selected & 0xffff)
    assert permissions == ([int(bool((platform or variant == 1) and mask & 4))] if constrained else [])
    assert replies == [1 if permitted else 0xff]
    assert forwarded == ([expected] if permitted else [])
    return forwarded, selected, replies[0], vm.trace


def smu_policy(body, code):
    trace = hashlib.sha256(); cases = rejected = clamped = 0; seen = set()
    for value, high, mode in itertools.product(COUNTS, (0, 0x00010000, 0xa5a50000, 0xffff0000), MODES):
        argument = high | (value & 0xffff)
        forwarded, selected, response, visited = smu_case(body, code, argument, mode)
        cases += 1; rejected += response != 1; clamped += bool(forwarded) and selected != value
        seen.update(visited)
        trace.update(struct.pack('<9I', argument, *mode, response,
            forwarded[0] if forwarded else 0, selected & 0xffffffff))
    assert {0x2bd07, 0x2bd0d, 0x2bd17, 0x2bd23, 0x2bd2c} <= seen
    return dict(cases=cases, rejected=rejected, clamped_cases=clamped,
        command='0x07', handler='0x2bcdc', permission_mask=4,
        constrained_min_counts=-50, constrained_max_counts=30,
        forwarded_high16_preserved=True, count_is_not_voltage=True,
        trace_sha256=trace.hexdigest())


def routing(body, code):
    trace = hashlib.sha256(); cases = non_success = 0
    payloads = (0, 30, 31, 0xffce, 0xffff, 0xa5a5ffce)
    for first_mask, second_mask, payload, response, delay in itertools.product(
            range(4), range(4), payloads, (1, 2, 0xfd, 0xff), (0, 1, 3)):
        vm = B['Broadcast'](body, code, first_mask, second_mask, payload, response, delay)
        vm.r['a2'] = 0xf; events = []
        def lock(v):
            assert v.r['a10'] == 3; events.append('lock')
        def unlock(v):
            assert v.r['a10'] == 3; events.append('unlock')
        vm.run(0x1d760, None, hooks={0x3bb64: lock, 0x3bc38: unlock})
        targets = [i for i in range(2) if second_mask & (1 << i)]
        expected = []
        for target in targets:
            base = 0xf8010600+(target << 20)
            expected += [(vm.ports[target], 0), (base+0x398, payload), (base+0x300, 0xf)]
        assert vm.writes == expected and events == ['lock', 'unlock']
        assert vm.polls == [delay+1 if i in targets else 0 for i in range(2)]
        trace.update(struct.pack('<5I', first_mask, second_mask, payload, response, delay))
        for address, word in vm.writes: trace.update(struct.pack('<2I', address, word))
        non_success += bool(second_mask) and response != 1; cases += 1
    stalled = 0
    for target in (0, 1):
        vm = B['Broadcast'](body, code, 0, 3, 0xffce, 1, stall=target); vm.r['a2'] = 0xf
        events = []
        try:
            vm.run(0x1d760, None, budget=256, hooks={
                0x3bb64: lambda v: events.append('lock'),
                0x3bc38: lambda v: events.append('unlock')})
        except ValueError as error:
            assert str(error) == 'Bounded instruction budget exceeded'
        else: raise AssertionError('Stalled completion unexpectedly returned')
        assert len(vm.writes) == 6 and events == ['lock'] and vm.polls[target] > 0
        stalled += 1
    return dict(cases=cases, routing_mask_address='0x6816', ignored_neighbor_mask='0x6814',
        firmware_targets=2, mp5_message='0x0f', non_success_nonzero_completions=non_success,
        stalled_target_cases=stalled, real_transport_executed=False,
        nonzero_completion_is_not_success_validation=True, trace_sha256=trace.hexdigest())


def mp5_case(body, code, argument, gate, sentinel):
    vm = Slice(body, code); vm.r.update(a1=0x50000, a2=0x1234)
    vm.put(0x19cf0, gate); vm.put(0x3210064, 0x80000000 if sentinel else 0)
    addresses = [0xee3c+0x520*i+0x1fc for i in range(8)]
    for address in addresses:
        vm.put(address, 0x12345678); vm.put(address-4, 0xa5a55a5a); vm.put(address+4, 0x5a5aa5a5)
    replies = []
    def input_word(v):
        assert v.r['a10'] == 0x1234; v.r['a10'] = argument
    def response(v):
        assert v.r['a10'] == 0x1234; replies.append(v.r['a11'])
    vm.run(0x2f02c, None, hooks={0x11b4: input_word, 0x1178: response})
    selected = min(signed16(argument), 30) if gate and not sentinel else signed16(argument)
    assert replies == [1]
    for address in addresses:
        assert vm.get(address) == selected & 0xffffffff
        assert vm.get(address-4) == 0xa5a55a5a and vm.get(address+4) == 0x5a5aa5a5
    return selected


def mp5_state(body, code):
    trace = hashlib.sha256(); cases = 0
    for value, high, gate, sentinel in itertools.product(
            COUNTS, (0, 0xa5a50000, 0xffff0000), (0, 1, 0xffffffff), (False, True)):
        argument = high | (value & 0xffff)
        selected = mp5_case(body, code, argument, gate, sentinel)
        trace.update(struct.pack('<4I', argument, gate, sentinel, selected & 0xffffffff)); cases += 1
    return dict(cases=cases, written_state_words=cases*8, handler='0x2f02c',
        first_core_state='0xf038', state_stride='0x520', local_core_slots=8,
        conditional_upper_clamp_counts=30, lower_clamp_in_this_handler=False,
        physical_core_mapping_verified=False, trace_sha256=trace.hexdigest())


def composition(smu, scode, mp5, mcode):
    trace = hashlib.sha256(); cases = 0
    # Compose original input policy with each target's original eight-slot
    # state handler. Transport selection is checked independently above.
    for counts, mode, gate, sentinel in itertools.product(range(-50, 31), MODES, (0, 1), (False, True)):
        payloads, selected, response, _ = smu_case(smu, scode, counts & 0xffff, mode)
        if response != 1: continue
        assert len(payloads) == 1 and payloads[0] >> 16 == 0
        actual = mp5_case(mp5, mcode, payloads[0], gate, sentinel)
        assert actual == counts == selected
        trace.update(struct.pack('<8I', counts & 0xffffffff, *mode, gate, sentinel)); cases += 1
    return dict(cases=cases, application_counts_min=-50, application_counts_max=30,
        covered_each_application_integer=True, setting_readback_verified=False,
        trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu', 'smu-study', 'mp5', 'mp5-study', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args(); smu = a.smu.read_bytes(); mp5 = a.mp5.read_bytes()
    scode = T['instructions'](a.smu_study, smu, T['SMU_SHA'])
    mcode = T['instructions'](a.mp5_study, mp5, H['SHA'])
    assert struct.unpack_from('<II', smu, 0x6fa4+8*7) == (0x2bcdc, 6)
    assert struct.unpack_from('<II', mp5, 0x13298+8*15) == (0x2f02c, 6)
    report = dict(schema=1, source_sha256={'smu':T['SMU_SHA'], 'mp5':H['SHA']},
        hardware_execution=False, signed16=sext_cases(smu, scode, mp5, mcode),
        smu_policy=smu_policy(smu, scode), routing=routing(smu, scode),
        mp5_state=mp5_state(mp5, mcode), composition=composition(smu, scode, mp5, mcode),
        host_readback_protocol_verified=False, target_hardware_accepted=False,
        physical_core_mapping_verified=False, electrical_limits_verified=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
