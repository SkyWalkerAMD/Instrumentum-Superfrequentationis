#!/usr/bin/env python3
"""Replay original common power/policy monitor vectors, without hardware.

The original labels and binary32 storage are the contract. These observations
do not establish physical units, policy enable bits or physical target IDs.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location('pm_limits', Path(__file__).with_name('legacy-amd-pm-limits.py'))
limits = importlib.util.module_from_spec(spec)
spec.loader.exec_module(limits)

# Vector members, labels and offsets are checked independently against original
# constructor/get_pm_patch instructions, not generated from the replacement.
GROUPS = (
    ('power', 0x118, 0x60, (
        'CPU Power', 'SOC Power', 'Misc Power', 'Mem Power', '1.8V Power', 'ROC Power', 'Socket Power')),
    ('workload', 0x208, 0x40, (
        'PPT_WC', 'PPT_ACTUAL', 'TDC_0_WC', 'TDC_0_ACTUAL',
        'TDC_1_WC', 'TDC_1_ACTUAL', 'FIT_WC', 'FIT_ACTUAL')),
    ('power_model', 0x238, 0x7c, (
        'VDDCR_CPU_DYN_POWER_WC', 'VDDCR_CPU_LKG_POWER_WC',
        'VDDCR_SOC_DYN_POWER_WC', 'VDDCR_SOC_LKG_POWER_WC')),
    ('voltage_policy', 0x178, 0x8c, (
        'FIT PID0', 'LATCHUP0', 'PPT_VOLTAGE_0', 'TDC_VOLTAGE_0', 'THM_VOLTAGE_0',
        'FIT_VOLTAGE_1', 'LATCHUP_VOLTAGE_1', 'PPT_VOLTAGE_1', 'TDC_VOLTAGE_1', 'THM_VOLTAGE_1')),
    ('limit_contributors', 0x148, 0xd0, (
        'PPT0', 'TDC0', 'STAPM0', 'THM0', 'HTFMax0', 'VRHOT0', 'ProcHot0', 'Voltage0',
        'EDC0', 'CState0', 'PPT_SLOW0', 'PPT_APU0',
        'PPT1', 'TDC1', 'STAPM1', 'THM1', 'HTFMax1', 'VRHOT1', 'ProcHot1', 'Voltage1',
        'EDC1', 'CState1', 'PPT_SLOW1', 'PPT_APU1')),
)


def case(fixture, version, ccds, pattern):
    machine = limits.Machine(fixture, version, ccds, pattern)
    result = machine.run()
    fields = []
    for group, member, offset, labels in GROUPS:
        names = machine.u64(limits.OBJ + member)
        assert machine.u64(limits.OBJ + member + 8) - names == len(labels) * 32
        assert tuple(machine.cppstr(names + i * 32) for i in range(len(labels))) == labels
        values = machine.u64(limits.OBJ + member + 24)
        assert machine.u64(limits.OBJ + member + 32) - values == len(labels) * 4
        words = struct.unpack('<' + str(len(labels)) + 'I', machine.uc.mem_read(values, len(labels) * 4))
        for i, (name, word) in enumerate(zip(labels, words)):
            address = offset + 4 * i
            assert address in machine.reads and word == machine.bits(address)
            fields.append(dict(group=group, name=name, offset=hex(address), raw=hex(word)))
    assert len(fields) == 53
    result['package_fields'] = fields
    return result


def research():
    fixture = limits.load_fixture()
    cases = [case(fixture, v, c, p) for v in limits.VERSIONS
             for c in (1, 2, 4, 8, 10, 12, 14, 16) for p in (0, 1)]
    return dict(schema=1, source_sha256=limits.SOURCE_SHA, fixture_sha256=limits.FIXTURE_SHA,
        hardware_execution=False, units_verified=False, targets_verified=False,
        case_count=len(cases), field_count=53,
        groups=[dict(id=g, names_member=hex(m), values_member=hex(m+24),
            first_offset=hex(o), labels=list(n)) for g, m, o, n in GROUPS],
        scope='Original common monitor labels and binary32 words only. All transport, topology '
              'and C++ boundaries synthetic; calibration assumed complete. Stops before core/CCD '
              'processing. No scaling, unit, enable-state, setter or physical-target inference.',
        cases=cases)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = research()
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('128 original power/policy monitor cases, 53 fields each; no hardware executed')
