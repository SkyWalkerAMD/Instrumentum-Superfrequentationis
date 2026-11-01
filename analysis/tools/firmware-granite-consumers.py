#!/usr/bin/env python3
"""Trace the three uncalibrated PM models into their SMU consumer slices.

The descriptor copy is an explicit byte-copy fixture, not DMA emulation.
Synthetic independent model streams distinguish a nearby PPT feedback input
from the uncalibrated exported rows. No physical unit follows from adjacency.
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
Slice, CORE = H['Slice'], H['CORE']


def consumers(smu, sc, mp5, mc):
    assert struct.unpack_from('<4I', mp5, 0x15960) == (0x08500001,0xe4,0x12a2c,0)
    # Pin both consumer base literals, not just the selected disassembly text.
    assert struct.unpack_from('<I', smu, 0x11eac)[0] == 0xa200
    assert struct.unpack_from('<I', smu, 0x11eb0)[0] == 0xa34c
    cases = selection_cases = 0; trace = hashlib.sha256()
    for seed, active in itertools.product(range(32), range(4)):
        vm = Slice(smu, sc)
        for address in range(0xa1c0, 0xa800): vm.put(address, 0, 1)
        expected_composite = 7.0
        alternate_power = 0.0
        for target in (0, 1):
            child = Slice(mp5, mc)
            for address in range(CORE, 0x12e58): child.put(address, 0, 1)
            values = [(seed+1)*(target+1)*(local+1)/8 for local in range(8)]
            first, second = (seed+1)/4, (target+1)/2
            for local, value in enumerate(values): child.putf(CORE+local*0x1bc+0x10, value)
            child.putf(0x12804, first); child.putf(0x12848, second)
            child.run(0x2ce90, 0x2cf35)
            assert child.getf(0x12a2c) == sum(values)
            assert child.getf(0x12a30) == sum(values)+first+second
            # The consumer's separate stream comes from producer offsets not
            # computed by this slice. It is explicitly independent test data.
            child.putf(0x12a2c+0x1c, 11+target)
            child.putf(0x12a2c+0x20, 17+target)
            child.putf(0x12a2c+0x24, 23+target)
            alternate_power += 28+2*target
            base = 0xa1c0+target*0xe4
            for offset in range(0xe4): vm.put(base+offset, child.get(0x12a2c+offset,1), 1)
            expected_composite += 2*sum(values)+first+second
        vm.r.update(a2=0xa1c0, a3=0, a4=0xa628, a8=2, a10=0xe4,
                    a11=0xa200, a12=0xa34c, a14=0xa200, a15=0x8164)
        vm.f.update({f'f{i}':0.0 for i in range(16)})
        vm.f.update(f8=3.0, f9=7.0)
        vm.put(0x8188, active)
        vm.run(0x2800d, 0x280bf)
        assert vm.getf(0xa38c) == expected_composite
        assert vm.getf(0xa390) == 3+alternate_power
        # Active mask affects the later current/min/max reduction, not the
        # preceding two-target composite. It cannot bind a physical OS core.
        assert vm.getf(0xa39c) == 0
        assert vm.getf(0xa3a0) == sum(23+t for t in range(2) if active & (1 << t))
        trace.update(struct.pack('<2I4f', seed, active, vm.getf(0xa38c),
                                 vm.getf(0xa390), vm.getf(0xa39c), vm.getf(0xa3a0)))
        for policy, baseline, gain, offset in itertools.product((0,1,2),(20,80),(.5,1),(0,2)):
            vm.r.update(a2=0xa1c0, a3=0xa4b8, a4=0xa628,
                        a13=0xd1c0, a14=0xa200, a15=0x8164, a10=0xe4)
            vm.f.update(f1=0, f7=0)
            vm.put(0x8195, policy, 1)
            vm.putf(0xa388, baseline); vm.putf(0xa628, 12.5)
            vm.putf(0xa74c, gain); vm.putf(0xd1c4, offset)
            vm.run(0x28121, 0x28189)
            selected = max(baseline, 3+alternate_power) if policy == 1 else baseline
            assert vm.getf(0xa398) == selected*gain
            assert vm.getf(0xa394) == selected*gain+offset
            assert vm.getf(0xa38c) == expected_composite
            trace.update(struct.pack('<I4f', policy, baseline, gain, offset, vm.getf(0xa394)))
            selection_cases += 1
        cases += 1
    return dict(paired_target_cases=cases, feedback_selection_cases=selection_cases,
        mp5_aggregate='0x12a2c', transfer_bytes=0xe4,
        smu_target_bases=['0xa1c0','0xa2a4'], consumer_slice='0x2800d..0x280bf',
        composite='0xa38c', composite_contribution_per_target='2*sum(core row 0)+CCD row 0+CCD row 1',
        composite_initial_other_contributors_are_fixtures=True,
        alternate_feedback='0xa390', alternate_producer_offsets=['0x1c','0x20'],
        feedback_selection_slice='0x28121..0x28189', feedback_before_gain='0xa398', feedback_after_bias='0xa394',
        composite_is_not_selected_feedback_in_checked_path=True,
        active_mask_does_not_filter_first_composite=True,
        copy_is_explicit_fixture=True, other_model_stream_is_fixture=True,
        complete_scheduler_or_peripheral_execution=False, physical_units_verified=False,
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
        consumers=consumers(smu,sc,mp5,mc), new_physical_units={},
        physical_or_os_core_mapping_verified=False, runtime_write_enabled=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
