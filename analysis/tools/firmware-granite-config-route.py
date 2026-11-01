#!/usr/bin/env python3
"""Byte-checked RSMU 72 -> MP5 75 configuration routing on synthetic memory.

No mailbox is accessed. Completion words and locks are scripted inputs; the
actual route, payload, polling branches and selector instructions are replayed.
The two payload halves are alternatives broadcast to present firmware targets,
not independent physical rails or an OS/CPUID core selection.
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
T = runpy.run_path(str(HERE / 'firmware-granite-topology.py'))
M = runpy.run_path(str(HERE / 'firmware-granite-model-inputs.py'))
Slice, f32 = H['Slice'], H['f32']


class Broadcast(Slice):
    def __init__(self, body, code, mask, active_mask, payload, response, delay=0, stall=None):
        super().__init__(body, code)
        self.r.update(a1=0x50000, a2=0x75, a3=payload)
        self.put(0x6814, mask, 2)
        self.put(0x6816, active_mask, 2)
        self.ports = [0x51000, 0x51004]
        for target, port in enumerate(self.ports):
            self.put(0x6abc+0x58+12*target, port)
        self.response, self.delay, self.stall = response, delay, stall
        self.polls = [0, 0]
        self.writes = []

    def get(self, address, size=4):
        if address in self.ports:
            assert size == 4
            target = self.ports.index(address)
            self.polls[target] += 1
            return 0 if target == self.stall or self.polls[target] <= self.delay else self.response
        return super().get(address, size)

    def put(self, address, value, size=4):
        if hasattr(self, 'ports') and (address in self.ports or address >= 0xf8000000):
            assert size == 4
            self.writes.append((address, value & 0xffffffff))
        else:
            super().put(address, value, size)


def dispatch(body, code):
    u = lambda address: struct.unpack_from('<I', body, address)[0]
    assert u(0x6fa4+8*0x72) == 0x1dcc0
    assert u(0x6fa4+8*0x72+4) == 0x206
    cases = stalled = non_success = 0
    trace = hashlib.sha256()
    payloads = (0, 1, 0xffff, 0x10000, 0x03e80320, 0x032003e8, 0xffffffff)
    # Present and active masks deliberately disagree. This path uses presence,
    # not the number of enabled cores, CPUID IDs or compact PM storage ranks.
    for mask, active, payload, response, delay in itertools.product(
            range(4), range(4), payloads, (1, 2, 0xfd, 0xff), (0, 1, 3)):
        vm = Slice(body, code)
        vm.r.update(a1=0x50000, a2=0x1234)
        replies = []; routed = []; locks = []

        def argument(v):
            assert v.r['a10'] == 0x1234
            v.r['a10'] = payload

        def reply(v):
            assert v.r['a10'] == 0x1234
            replies.append(v.r['a11'])

        def route(v):
            assert (v.r['a10'], v.r['a11']) == (0x75, payload)
            child = Broadcast(body, code, mask, active, payload, response, delay)
            def lock(w):
                assert w.r['a10'] == 3
                locks.append('lock')
            def unlock(w):
                assert w.r['a10'] == 3
                locks.append('unlock')
            child.run(0x1d690, None, hooks={0x3bb64:lock, 0x3bc38:unlock})
            targets = [t for t in range(2) if mask & (1 << t)]
            expected = []
            for target in targets:
                base = 0xf8010600+(target << 20)
                expected += [(child.ports[target], 0), (base+0x398, payload), (base+0x300, 0x75)]
            assert child.writes == expected
            assert all(child.polls[t] == (delay+1 if t in targets else 0) for t in range(2))
            routed.extend(targets)
            for address, value in child.writes:
                trace.update(struct.pack('<2I', address, value))

        vm.run(0x1dcc0, None, hooks={0x15dc:argument, 0x1d690:route, 0x14ec:reply})
        assert replies == [1] and locks == ['lock', 'unlock']
        assert routed == [t for t in range(2) if mask & (1 << t)]
        trace.update(struct.pack('<6I', mask, active, payload, response, delay, replies[0]))
        non_success += bool(mask) and response != 1
        cases += 1
    # Both commands are submitted before either completion wait. A failed or
    # stalled target does not prevent the other target from receiving a write.
    for target in (0, 1):
        vm = Broadcast(body, code, 3, 0, 0x03e80320, 1, stall=target)
        locks = []
        try:
            vm.run(0x1d690, None, budget=256,
                   hooks={0x3bb64:lambda v:locks.append('lock'),
                          0x3bc38:lambda v:locks.append('unlock')})
        except ValueError as error:
            assert str(error) == 'Bounded instruction budget exceeded'
        else:
            raise AssertionError('Stalled completion unexpectedly returned')
        assert len(vm.writes) == 6 and locks == ['lock']
        assert vm.polls[target] > 0
        stalled += 1
    return dict(cases=cases, non_success_nonzero_responses_acknowledged=non_success,
        stalled_targets=stalled, rsmu_command='0x72', handler='0x1dcc0',
        broadcaster='0x1d690', mp5_command='0x75', presence_mask='ushort[0x6814]',
        active_mask_used=False, completion_test='nonzero, not equality with success code 1',
        caller_reply_after_completions=1, all_commands_sent_before_completion_wait=True,
        no_present_target_still_acknowledges=True, payload_returned_to_host=False,
        timeout_in_selected_firmware_loop=False, completion_values_are_fixtures=True,
        lock_helpers_are_fixtures=True, trace_sha256=trace.hexdigest())


def alternatives(body, code):
    k = M['Kernels'](body, code)
    assert struct.unpack_from('<I', body, 0x13754)[0] == 0x13298
    assert struct.unpack_from('<2I', body, 0x13298+8*0x75) == (0x2beb4, 6)
    assert code[0x2beb9]['text'] == 'call8 0x000011b4'
    assert code[0x2bef7]['text'] == 'call8 0x0002e834'
    cases = selected = retained = 0; trace = hashlib.sha256()
    for low, high in itertools.product((498, 500, 800, 1000, 1250, 2000, 65535),
                                      (0, 498, 800, 1000, 1250, 65535)):
        for first_selector, second_selector in itertools.product((.5, 1, 1.5), repeat=2):
            values = []
            for target, selector in enumerate((first_selector, second_selector)):
                vm = k.vm(); vm.r['a10'] = low | high << 16
                vm.run(0x2bebc, 0x2bee8)
                for call, begin, end in ((0x2bee8,0x2beeb,0x2bef1), (0x2bef1,0x2bef4,0x2bef7)):
                    k.check_call(call, 0x24984)
                    leaf = k.vm(); leaf.f['f0'] = vm.f['f0']
                    leaf.run(0x24984, 0x2499c)
                    vm.r['a10'] = leaf.r['a2']; vm.run(begin, end)
                first, second = f32(low*f32(.001)), f32((high or low)*f32(.001))
                assert vm.getf(0x119ac) == first and vm.getf(0x119b0) == second
                # The MP5 handler replies immediately after updating input
                # storage, before the later policy selector/transition gates.
                reply = k.vm(); reply.put(0x19d98, 0x51000)
                reply.run(0x2e834, 0x2e843)
                assert reply.get(0x51000) == 1
                vm.r.update(a4=0x113dc, a2=7)
                vm.putf(0x11428, selector); vm.putf(0x119b4, 1)
                vm.put(0x32001c0, 23)
                vm.run(0x2a494, 0x2a4d8)
                expected = second if selector > 1 else first
                value = vm.getf(0x11728); assert value == expected
                values.append(value); selected += 1
                # A selected input is not necessarily the exported row value.
                # An equal quantized byte keeps old 8030 in both transition gates.
                vm.r.update(a3=0x11644, a4=0x1171c, a5=0x7820, a7=0x78c)
                vm.put(0x1164d, vm.get(0x11725,1), 1); vm.putf(0x8030, .625)
                vm.run(0x27c6f,0x27ca3); vm.run(0x27fa4,0x27fd3)
                assert vm.getf(0x8030) == .625
                retained += 1
                trace.update(struct.pack('<I4f', target, selector, first, second, value))
            if first_selector == second_selector:
                assert values[0] == values[1]
            cases += 1
    return dict(paired_target_cases=cases, selections=selected, retained_row_cases=retained,
        input_count=2, selector='float[0x11428] > float[0x119b4]',
        selected_pair_addresses=['0x11725', '0x11728'], exported_source='0x8030',
        zero_upper_half_reuses_lower=True, same_payload_sent_to_each_present_target=True,
        mp5_ack_precedes_policy_selection=True,
        target_selectors_are_independent_fixtures=True, physical_unit_verified=False,
        physical_rail_identity_verified=False, device_readback_verified=False,
        trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('smu','mp5','smu-study','mp5-study','output'):
        p.add_argument('--'+key, type=Path, required=True)
    a = p.parse_args(); smu = a.smu.read_bytes(); mp5 = a.mp5.read_bytes()
    sc = T['instructions'](a.smu_study, smu, T['SMU_SHA'])
    mc = T['instructions'](a.mp5_study, mp5, T['MP5_SHA'])
    report = dict(schema=1, hardware_execution=False,
        source_sha256=dict(smu=T['SMU_SHA'], mp5=T['MP5_SHA']),
        dispatch=dispatch(smu, sc), alternatives=alternatives(mp5, mc),
        physical_or_os_core_mapping_verified=False, independent_second_rail_verified=False,
        new_physical_units={}, runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
