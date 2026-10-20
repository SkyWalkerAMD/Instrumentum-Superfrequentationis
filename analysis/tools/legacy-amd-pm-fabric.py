#!/usr/bin/env python3
"""Replay original I/O threshold and internal-clock monitor storage mappings.

No clock units, active link state, physical targets or firmware writes inferred.
The original FCLK/UCLK/MCLK vector with conflicting offsets remains excluded.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location(
    'pm_limits', Path(__file__).with_name('legacy-amd-pm-limits.py'))
limits = importlib.util.module_from_spec(spec)
spec.loader.exec_module(limits)

GROUPS = (
    ('link_bandwidth', 0x2c8, 0x154, (
        'FCLK_PCIE_UP_THRESHOLD', 'FCLK_PCIE_BANDWIDTH', 'FCLK_PCIE_DOWN_THRESHOLD',
        'FCLK_XGMI_UP_THRESHOLD', 'FCLK_XGMI_BANDWIDTH', 'FCLK_XGMI_DOWN_THRESHOLD',
        'FCLK_CXL_UP_THRESHOLD', 'FCLK_CXL_BANDWIDTH', 'FCLK_CXL_DOWN_THRESHOLD')),
    ('internal_clocks', 0x3e8, 0x24c, (
        'SMNCLK', 'SMNCLK_EFF', 'WAFLCLK', 'WAFLCLK_EFF',
        'MP0CLK', 'MP0CLK_EFF', 'MP1CLK', 'MP1CLK_EFF',
        'MPIOCLK', 'MPIOCLK_EFF', 'ACLK', 'ACLK_EFF',
        'SVI3CLK', 'SVI3CLK_EFF', 'MPDMA_TF_CLK', 'MPDMA_TF_CLK_EFF',
        'MPDMA_PM_CLK', 'MPDMA_PM_CLK_EFF')),
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
        words = struct.unpack('<' + str(len(labels)) + 'I',
                              machine.uc.mem_read(values, len(labels) * 4))
        for i, (name, word) in enumerate(zip(labels, words)):
            address = offset + 4 * i
            assert address in machine.reads and word == machine.bits(address)
            fields.append(dict(group=group, name=name, offset=hex(address), raw=hex(word)))
    assert len(fields) == 27
    result['fabric_fields'] = fields
    return result


def research():
    fixture = limits.load_fixture()
    cases = [case(fixture, v, c, p) for v in limits.VERSIONS
             for c in (1, 2, 4, 8, 10, 12, 14, 16) for p in (0, 1)]
    return dict(schema=1, source_sha256=limits.SOURCE_SHA,
        fixture_sha256=limits.FIXTURE_SHA, hardware_execution=False,
        units_verified=False, targets_verified=False, case_count=len(cases), field_count=27,
        groups=[dict(id=g, names_member=hex(m), values_member=hex(m+24),
                     first_offset=hex(o), labels=list(n)) for g, m, o, n in GROUPS],
        scope='Original constructor labels and common decoder binary32 mappings only. '
              'Transport/topology/C++ boundaries synthetic; calibration assumed complete; '
              'stops before core/CCD processing. No physical unit, link/clock state, '
              'BCLK correction, target identity or new hardware operation is established.',
        cases=cases)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.output.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(research(), indent=2) + '\n')
    print('128 original fabric-monitor cases, 27 fields each; no hardware executed')
