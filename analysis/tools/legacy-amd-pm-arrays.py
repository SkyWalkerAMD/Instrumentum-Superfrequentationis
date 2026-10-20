#!/usr/bin/env python3
"""Replay original GMI/DIMM/PCIe storage, without inferring physical targets."""
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
    ('gmi_storage', 0x418, 0x294,
     ('GMI_FULL_WIDTH', 'GMI_HALF_WIDTH', 'GMI_QUARTER_WIDTH'), (12, 12, 12)),
    ('dimm_storage', 0x448, 0x324, ('DIMM_POWER', 'DIMM_TEMP'), (24, 24)),
    ('pcie_storage', 0x478, 0x3e4,
     ('PCIE_ALLOWED', 'PCIE_ACTIVE', 'PCIE_UP_THRESHOLD', 'PCIE_DOWN_THRESHOLD',
      'PCIE_IOHC_BANDWIDTH[4]'), (1, 1, 1, 1, 4)),
)


def case(fixture, version, ccds, pattern):
    machine = limits.Machine(fixture, version, ccds, pattern)
    result = machine.run()
    fields = []
    for group, member, offset, labels, widths in GROUPS:
        names = machine.u64(limits.OBJ + member)
        assert machine.u64(limits.OBJ + member + 8) - names == len(labels) * 32
        assert tuple(machine.cppstr(names + i * 32) for i in range(len(labels))) == labels
        values = machine.u64(limits.OBJ + member + 24)
        assert machine.u64(limits.OBJ + member + 32) - values == sum(widths) * 4
        words = struct.unpack('<' + str(sum(widths)) + 'I',
                              machine.uc.mem_read(values, sum(widths) * 4))
        index = 0
        for name, width in zip(labels, widths):
            for slot in range(width):
                address, word = offset + 4 * index, words[index]
                assert address in machine.reads and word == machine.bits(address)
                fields.append(dict(group=group, name=name, slot=slot, slot_count=width,
                                   offset=hex(address), raw=hex(word)))
                index += 1
    assert len(fields) == 92
    result['array_fields'] = fields
    return result


def research():
    fixture = limits.load_fixture()
    cases = [case(fixture, v, c, p) for v in limits.VERSIONS
             for c in (1, 2, 4, 8, 10, 12, 14, 16) for p in (0, 1)]
    return dict(schema=1, source_sha256=limits.SOURCE_SHA,
        fixture_sha256=limits.FIXTURE_SHA, hardware_execution=False,
        units_verified=False, targets_verified=False, case_count=len(cases), field_count=92,
        groups=[dict(id=g, names_member=hex(m), values_member=hex(m+24),
                     first_offset=hex(o), labels=list(n), widths=list(w)) for g, m, o, n, w in GROUPS],
        scope='Original constructor labels and common decoder binary32 storage only. '
              'Transport/topology/C++ boundaries synthetic; calibration assumed complete; '
              'stops before core/CCD processing. Slot counts do not establish installed '
              'DIMMs, physical links or ports, units, active states or firmware setters.',
        cases=cases)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.output.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(research(), indent=2) + '\n')
    print('128 original array-monitor cases, 92 stored words each; no hardware executed')
