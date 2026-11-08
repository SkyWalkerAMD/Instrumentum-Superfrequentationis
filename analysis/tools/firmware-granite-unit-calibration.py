#!/usr/bin/env python3
"""Separate anchored SVI power from uncalibrated Granite model aggregates.

Original byte-checked converter, MP5 aggregate and SMU consumer instructions
run on synthetic memory. Cache maintenance and transfer completion are explicit
boundaries. This proves dependencies, not absolute model calibration or units.
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
V = runpy.run_path(str(HERE/'firmware-granite-telemetry.py'))
Slice, CORE = H['Slice'], H['CORE']
OUTPUTS = (0xa388, 0xa38c, 0xa390)
OTHER = (
    ('0xa4f4', 0xa4f4, (1,1,1)), ('0xa704', 0xa704, (1,1,1)),
    ('0xa6fc', 0xa6fc, (1,1,0)), ('0xa520', 0xa520, (1,0,0)),
    ('0xa548', 0xa548, (1,0,0)), ('0xa470', 0xa470, (0,1,0)),
    ('0xa4a0', 0xa4a0, (0,0,1)), ('0xa528', 0xa528, (0,1,0)),
    ('0xa530', 0xa530, (0,0,1)), ('0xa554', 0xa554, (0,1,1)),
)


def basis_inputs():
    rows = []
    for channel in range(3):
        rows.append(dict(name=f'svi-channel-{channel}', kind='svi', channel=channel,
                         expected=[1,0,0]))
    for target in range(2):
        for slot in range(8):
            rows.append(dict(name=f'target-{target}-core-row-0-slot-{slot}',
                             kind='core', target=target, slot=slot, expected=[0,2,0]))
        for row in range(2):
            rows.append(dict(name=f'target-{target}-ccd-row-{row}', kind='ccd',
                             target=target, row=row, expected=[0,1,0]))
        for offset in (0x1c,0x20):
            rows.append(dict(name=f'target-{target}-alternate-{offset:x}',
                             kind='alternate', target=target, offset=offset, expected=[0,0,1]))
    for name,address,expected in OTHER:
        rows.append(dict(name=name, kind='other', address=address, expected=list(expected)))
    rows.append(dict(name='lookup-intercept', kind='lookup', expected=[0,0,1]))
    assert len(rows) == 38
    return rows


def replay(smu, sc, mp5, mc):
    # Pin the actual starting arguments, output bases and SVI loop's origin.
    for literal,value in ((0x11510,0xa1c0),(0x11514,0xae30),(0x11eb0,0xa34c),
                          (0x11eb4,0xa628),(0x11500,0x8164),(0x11eac,0xa200),
                          (0x11e74,0xaa50)):
        assert struct.unpack_from('<I',smu,literal)[0] == value
    assert sc[0x27ee1]['text'] == 'call8 0x00003534'
    assert sc[0x27f4f]['text'] == 'recip0.s f12,f13'
    assert struct.unpack_from('<4I',mp5,0x15960) == (0x08500001,0xe4,0x12a2c,0)
    smu_zero = {a:0 for a in range(0x8000,0xd800)}
    mp5_zero = {a:0 for a in range(CORE,0x12e58)}
    trace = hashlib.sha256()
    cases = cache_boundaries = reciprocal_fixtures = copied_bytes = 0
    all_instructions = set()
    input_rows = basis_inputs()
    observed = {row['name']:set() for row in input_rows}
    for basis,amplitude,presence,frequency in itertools.product(
            input_rows,(.25,1.0,8.0),range(4),(0.0,2.0,4.0)):
        vm = Slice(smu,sc); vm.ram = smu_zero.copy(); vm.r['a1'] = 0x50000
        vm.put(0x8172,2,1); vm.put(0x8188,presence)
        vm.putf(0xaed4,frequency)
        for address,value in ((0xac44,1.0),(0xac48,2.0),(0xac4c,3.0)):
            vm.putf(address,value)
        if basis['kind'] == 'lookup':
            for address in (0xac34,0xac38,0xac3c): vm.putf(address,amplitude)
        if basis['kind'] == 'other': vm.putf(basis['address'],amplitude)
        # The calibrated converter is executed, rather than supplying its
        # power register. At V=1 and scale=.25, one raw current code is .25 A.
        raws = [(int(amplitude*4) if basis['kind']=='svi' and basis['channel']==c else 0,
                 200,80+c) for c in range(3)]
        telemetry = V['convert'](vm,0,raws,[.25,.25,.25])
        assert all(row[0] == 1 for row in telemetry)
        assert sum(row[2] for row in telemetry) == (amplitude if basis['kind']=='svi' else 0)
        for target in range(2):
            child = Slice(mp5,mc); child.ram = mp5_zero.copy()
            if basis['kind']=='core' and basis['target']==target:
                child.putf(CORE+0x1bc*basis['slot']+0x10,amplitude)
            if basis['kind']=='ccd' and basis['target']==target:
                child.putf((0x12804,0x12848)[basis['row']],amplitude)
            child.run(0x2ce90,0x2cf35)
            # These alternate fields still have synthetic producer values;
            # their upstream physical meaning is intentionally not inferred.
            if basis['kind']=='alternate' and basis['target']==target:
                child.putf(0x12a2c+basis['offset'],amplitude)
            for offset in range(0xe4):
                vm.put(0xa1c0+0xe4*target+offset,child.get(0x12a2c+offset,1),1)
                copied_bytes += 1
        cache_calls = []
        def cache_boundary(v):
            assert (v.r['a10'],v.r['a11']) == (0xa1c0,0x1c8)
            cache_calls.append((v.r['a10'],v.r['a11']))
        vm.run(0x27ed4,0x27f4f,hooks={0x3534:cache_boundary})
        assert cache_calls == [(0xa1c0,0x1c8)] and vm.f['f13']==2.0
        cache_boundaries += 1
        vm.f['f12'] = .5; reciprocal_fixtures += 1
        vm.run(0x27f52,0x28064)
        actual = tuple(vm.getf(address) for address in OUTPUTS)
        expected = tuple(amplitude*factor for factor in basis['expected'])
        assert actual == expected, (basis,amplitude,presence,frequency,actual,expected)
        # All fixtures are exact dyadic arithmetic. This is a dependency test,
        # not a claim that the real FPU reciprocal is correctly rounded.
        observed[basis['name']].add(tuple(value/amplitude for value in actual))
        all_instructions.update(vm.trace)
        trace.update(basis['name'].encode('ascii')+b'\0')
        trace.update(struct.pack('<fIf3f',amplitude,presence,frequency,*actual))
        cases += 1
    for basis in input_rows:
        assert observed[basis['name']] == {tuple(basis['expected'])}
    return dict(cases=cases,input_basis_count=len(input_rows),
        outputs=[hex(a) for a in OUTPUTS],dependency_matrix=input_rows,
        amplitudes=[.25,1,8],presence_masks=list(range(4)),lookup_inputs=[0,2,4],
        smu_slice=['0x27ed4','0x28064'],reciprocal_fixture_address='0x27f4f',
        reciprocal_fixtures=reciprocal_fixtures,reciprocal_fixture_value=.5,
        cache_maintenance_boundaries=cache_boundaries,
        transfer_copy_is_fixture=True,transfer_bytes_copied=copied_bytes,
        reached_smu_instruction_count=len(all_instructions),
        svi_power_sum_direct_destination='0xa388',
        model_composite='0xa38c',alternate_composite='0xa390',
        svi_power_changes_model_composite_in_checked_prefix=False,
        shared_additive_sources=[name for name,_,factors in OTHER if factors[0] and factors[1]],
        absolute_model_calibration_verified=False,
        trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu','smu-study','mp5','mp5-study','output'):
        p.add_argument('--'+name,type=Path,required=True)
    args = p.parse_args(); smu = args.smu.read_bytes(); mp5 = args.mp5.read_bytes()
    sc = T['instructions'](args.smu_study,smu,T['SMU_SHA'])
    mc = T['instructions'](args.mp5_study,mp5,T['MP5_SHA'])
    report = dict(schema=1,hardware_execution=False,
        source_sha256=dict(smu=T['SMU_SHA'],mp5=T['MP5_SHA']),
        study_sha256=dict(smu=hashlib.sha256(args.smu_study.read_bytes()).hexdigest(),
                          mp5=hashlib.sha256(args.mp5_study.read_bytes()).hexdigest()),
        calibration_consumer=replay(smu,sc,mp5,mc),new_physical_units={},
        physical_mapping_verified=False,hardware_readback_verified=False,
        runtime_write_enabled=False,
        limitations=['Model/coefficient calibration remains unknown; shared addition is not an absolute unit anchor.',
                     'Cache maintenance, DMA completion, scheduler and peripherals are not emulated.',
                     'The reciprocal instruction receives an exact .5 fixture; FPU approximation/exceptions are not modeled.',
                     'Alternate aggregate producers and the other additive fields are independent synthetic inputs.',
                     'Dependency findings cover this selected finite path, not every runtime writer or consumer.'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
