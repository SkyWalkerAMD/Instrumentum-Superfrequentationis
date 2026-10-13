#!/usr/bin/env python3
"""Replay original ICCmax instructions against synthetic MSR/container boundaries."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
SCALAR_READ = '_Z9rd_iccmaxv'
SCALAR_FLAG = '_Z13is_iccmax_disv'
SCALAR_WRITE = '_Z6iccmaxv'
TOPOLOGY = '_Z15get_vr_topologyRSt6vectorIhSaIhEE'
SET = '_Z10set_iccmaxii'
UNLIMIT = '_Z14unlimit_iccmaxib'
MAXOUT = '_Z13maxout_iccmaxv'
READ = '_Z23get_vr_iccmax_unlimitedRSt6vectorIbSaIbEE'
SCALARS = (SCALAR_READ, SCALAR_FLAG, SCALAR_WRITE)
INDEXED = (SET, UNLIMIT, MAXOUT, READ)
PINS = {
    'legacy-intel-iccmax.json': '1d34735adc76ba8b419cf1bb2e58f0cad50e5a8a37799e07a608f6beb6775749',
    'legacy-intel-vr-topology.json': 'bd87d1d34b4cf749b423e3d6ff0b74b02add2b8b8293730e485c882f77a178bd',
}
spec = importlib.util.spec_from_file_location('iccmax_base', Path(__file__).with_name('emulate-legacy-dispatch.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


def load_fixture():
    merged = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, functions=[], objects={}, literals=[])
    for name, digest in PINS.items():
        raw = (ROOT / 'analysis/fixtures' / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError('fixture identity mismatch: ' + name)
        fixture = json.loads(raw)
        if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA:
            raise ValueError('fixture source mismatch: ' + name)
        merged['functions'].extend(fixture['functions'])
        assert not (set(merged['objects']) & set(fixture['objects']))
        merged['objects'].update(fixture['objects'])
        merged['literals'].extend(fixture['literals'])
    return merged


def topology_word(addresses=(1, 2, 3), flags=(0, 1, 0)):
    return (sum((value & 15) << shift for value, shift in zip(addresses, (8, 13, 25))) |
            sum((value & 1) << shift for value, shift in zip(flags, (12, 17, 29))))


class Machine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.trace, self.reads, self.phase_reads = [], 0, 0
        self.command, self.address = None, None

    def prepare(self, symbol):
        if symbol in (SET, UNLIMIT):
            self.reg('rdi', self.inputs.get('index', 0) & 0xffffffff)
            self.reg('rsi', self.inputs.get('value', 1) & 0xffffffff)
        elif symbol == TOPOLOGY:
            self.flags = self.alloc(24)
            self.reg('rsi', self.flags)
        elif symbol == READ:
            self.flags = self.alloc(40)
            self.reg('rsi', self.flags)
            self.put_bits(self.flags, self.inputs.get('prefix_flags', []))

    def values(self, obj, width=1):
        begin, end = self.u64(obj), self.u64(obj + 8)
        assert 0 <= end - begin <= 128 and (end - begin) % width == 0
        raw = bytes(self.uc.mem_read(begin, end - begin)) if begin else b''
        return list(struct.unpack('<{}{}'.format(len(raw) // width, 'I' if width == 4 else 'B'), raw))

    def append(self, obj, value, width):
        previous = self.values(obj, width)
        values = previous + [value]
        capacity = len(values) + self.inputs.get('vector_spare', 0)
        data = self.alloc(capacity * width)
        self.uc.mem_write(data, struct.pack('<{}{}'.format(len(values), 'I' if width == 4 else 'B'), *values))
        for offset, address in ((0, data), (8, data + len(values) * width), (16, data + capacity * width)):
            self.put64(obj + offset, address)
        return data + len(previous) * width

    def bits(self, obj):
        begin, begin_bit = self.u64(obj), self.u64(obj + 8)
        end, end_bit = self.u64(obj + 16), self.u64(obj + 24)
        assert begin_bit == 0 and 0 <= end_bit < 64
        count = (end - begin) * 8 + end_bit
        assert 0 <= count <= 128
        return [bool(self.u64(begin + (index // 64) * 8) & (1 << (index % 64))) for index in range(count)]

    def put_bits(self, obj, values):
        # Allocation is mocked; original inline vector<bool> bit operations run.
        capacity = (len(values) + 63) // 64
        data = self.alloc(capacity * 8) if capacity else 0
        for index, value in enumerate(values):
            if value:
                self.put64(data + (index // 64) * 8, self.u64(data + (index // 64) * 8) | (1 << (index % 64)))
        for offset, value in ((0, data), (8, 0), (16, data + (len(values) // 64) * 8),
                              (24, len(values) % 64), (32, data + capacity * 8)):
            self.put64(obj + offset, value)

    def external(self, name):
        self.calls.append(name)
        if name == '_Z5Wrmsrjjj':
            msr = self.reg('rdi')
            assert msr in (0x150, 0x601)
            low, high = self.reg('rsi') & 0xffffffff, self.reg('rdx') & 0xffffffff
            self.trace.append(dict(write=True, msr=msr, value=(high << 32) | low))
            if msr == 0x150:
                self.command, self.address = high & 255, (high >> 8) & 31
                self.phase_reads = 0
            self.ret(self.inputs.get('write_result', 1))
        elif name == '_Z5RdmsrjPjS_':
            assert self.reg('rdi') == 0x150 and self.command in (4, 0x16, 0x17)
            self.reads += 1
            self.phase_reads += 1
            if self.inputs.get('supply_data', True):
                raw = (self.inputs.get('topology', topology_word()) if self.command == 4 else
                       self.inputs.get('raw_by_address', {}).get(self.address, self.inputs.get('raw', 0)))
                busy = ((self.inputs.get('busy', False) and self.command == self.inputs.get('busy_command', 0x16)) or
                        self.phase_reads <= self.inputs.get('busy_reads', 0))
                self.put32(self.reg('rsi'), raw)
                self.put32(self.reg('rdx'), (0x80000000 if busy else 0) | self.inputs.get('status', 0))
            self.trace.append(dict(write=False, msr=0x150, command=self.command))
            self.ret(self.inputs.get('read_result', 1))
        elif name.startswith(('_ZNSt6vectorIhSaIhEE17_M_realloc_insert', '_ZNSt6vectorIiSaIiEE17_M_realloc_insert')):
            obj = self.reg('rdi')
            width = 1 if 'vectorIh' in name else 4
            assert self.reg('rsi') == self.u64(obj + 8) == self.u64(obj + 16)
            value = int.from_bytes(self.uc.mem_read(self.reg('rdx'), width), 'little')
            self.append(obj, value, width)
            self.ret()
        elif name.startswith(('_ZNSt6vectorIhSaIhEE12emplace_back', '_ZNSt6vectorIiSaIiEE12emplace_back')):
            width = 1 if 'vectorIh' in name else 4
            value = int.from_bytes(self.uc.mem_read(self.reg('rsi'), width), 'little')
            self.ret(self.append(self.reg('rdi'), value, width))
        elif name.startswith('_ZNSt6vectorIbSaIbEE13_M_insert_aux'):
            obj = self.reg('rdi')
            assert self.reg('rsi') == self.u64(obj + 16)
            assert self.reg('rdx') == self.u64(obj + 24)
            self.put_bits(obj, self.bits(obj) + [bool(self.reg('rcx'))])
            self.ret()
        elif name == '_ZdlPvm@plt':
            ptr = self.reg('rdi')
            assert ptr in self.allocations and ptr not in self.deleted
            assert self.reg('rsi') == self.allocations[ptr]
            self.deleted.append(ptr)
            self.ret()
        elif name == '_ZSt24__throw_out_of_range_fmtPKcz@plt':
            self.events.append(dict(index=self.reg('rsi'), size=self.reg('rdx')))
            self.stop('range-error-boundary')
        else:
            raise ValueError('unexpected external boundary: ' + name)


def investigate(fixture):
    rows = []

    def case(symbol, inputs, bounded=False, outcome='returned'):
        machine = Machine(fixture, inputs)
        result = machine.run(symbol, instruction_limit=20000 if not bounded else 3000,
                             allow_instruction_bound=bounded)
        assert result['outcome'] == outcome, (symbol, inputs, result)
        row = dict(symbol=symbol, inputs=inputs, outcome=result['outcome'], trace=machine.trace)
        if machine.events:
            row['events'] = machine.events
        rows.append(row)
        return machine, row

    patterns = [0, 0xffffffff, 0xa5c32110] + [1 << n for n in range(32)]
    for symbol, raw, wrapper, busy, status in itertools.product(SCALARS, patterns, (0, 1), (0, 2), (0, 1, 0xfe)):
        inputs = dict(raw=raw, read_result=wrapper, write_result=wrapper, busy_reads=busy, status=status)
        m, row = case(symbol, inputs)
        writes = [v for v in m.trace if v['write']]
        assert writes[0] == dict(write=True, msr=0x150, value=0x8000001600000000)
        if symbol in (SCALAR_READ, SCALAR_FLAG):
            value = raw & 0x3ff if symbol == SCALAR_READ else raw >> 31
            assert m.reg('rax') == value and len(writes) == 1
            row['result'] = value
        else:
            assert writes[1:] == [dict(write=True, msr=0x150, value=(0x80000017 << 32) | (raw | 0x800003ff)),
                                  dict(write=True, msr=0x601, value=0x1fff)]
            assert m.trace[-2:] == writes[1:]
    for symbol in SCALARS:
        for inputs in (dict(raw=0, busy=True), dict(raw=0, supply_data=False, read_result=0)):
            m, _ = case(symbol, inputs, True, 'instruction-limit')
            assert m.reads > 80 and sum(v['write'] for v in m.trace) == 1

    # All indexed cases execute the original nested topology decoder too.
    for raw, spare, wrapper in itertools.product(patterns, (0, 4), (0, 1)):
        m, row = case(TOPOLOGY, dict(topology=raw, vector_spare=spare, read_result=wrapper,
                                   write_result=wrapper, status=0xfe, busy_reads=2))
        addresses = [(raw >> shift) & 15 for shift in (8, 13, 25)]
        flags = [(raw >> shift) & 1 for shift in (12, 17, 29)]
        assert m.values(m.output) == addresses and m.values(m.flags) == flags
        assert m.reg('rax') == m.output
        assert [v for v in m.trace if v['write']] == [dict(write=True, msr=0x150, value=0x8000000400000000)]
        row.update(addresses=addresses, flags=flags)

    for symbol, index, raw, value in itertools.product((SET, UNLIMIT), range(3), patterns,
                                                       (0, 1, 1023, 1024, 2047, 2048, -1)):
        if symbol == UNLIMIT and value not in (0, 1):
            continue  # Canonical C++ bool ABI inputs only.
        inputs = dict(index=index, raw=raw, value=value, read_result=0, write_result=0,
                      status=0xfe, busy_reads=2, vector_spare=4)
        m, _ = case(symbol, inputs)
        target = index + 1
        data = ((raw & 0xfffff800) | (value & 0x7ff)) if symbol == SET else ((raw & 0x7fffffff) | (value << 31))
        assert [v for v in m.trace if v['write']] == [
            dict(write=True, msr=0x150, value=0x8000000400000000),
            dict(write=True, msr=0x150, value=((target << 8) | 0x16) << 32),
            dict(write=True, msr=0x150, value=(((target << 8) | 0x17) << 32) | data)]
        assert m.command == 0x17  # Polling completion never re-queries 16.
    for symbol, index in itertools.product((SET, UNLIMIT), (-1, 3, 2147483647, -2147483648)):
        m, _ = case(symbol, dict(index=index), outcome='range-error-boundary')
        assert sum(v['write'] for v in m.trace) == 1 and m.events[-1]['size'] == 3

    for raw, flags in itertools.product(patterns, itertools.product((0, 1), repeat=3)):
        m, _ = case(MAXOUT, dict(raw=raw, topology=topology_word(flags=flags), vector_spare=4))
        writes = [v for v in m.trace if v['write']]
        expected = [dict(write=True, msr=0x150, value=0x8000000400000000)]
        for address in (1, 2, 3):
            expected.extend([dict(write=True, msr=0x150, value=((address << 8) | 0x16) << 32),
                             dict(write=True, msr=0x150, value=(((address << 8) | 0x17) << 32) | (raw | 0x800007ff))])
        assert writes == expected  # No domain is skipped based on topology flags.

    for raw, prefix, spare in itertools.product(patterns, ([], [True, False], [True] * 63), (0, 4)):
        values = {1: raw, 2: raw ^ 0x800007ff, 3: (~raw) & 0xffffffff}
        m, row = case(READ, dict(raw_by_address=values, prefix_flags=prefix, vector_spare=spare,
                                read_result=0, write_result=0, status=0xfe, busy_reads=2))
        assert m.values(m.output, 4) == [values[address] & 0x7ff for address in (1, 2, 3)]
        assert m.bits(m.flags) == prefix + [bool(values[address] >> 31) for address in (1, 2, 3)]
        assert m.reg('rax') == m.output
        row.update(values=m.values(m.output, 4), flags=m.bits(m.flags))
        assert [v['value'] for v in m.trace if v['write']] == [0x8000000400000000] + [
            ((address << 8) | 0x16) << 32 for address in (1, 2, 3)]

    for symbol in (TOPOLOGY,) + INDEXED:
        m, _ = case(symbol, dict(busy=True, busy_command=4), True, 'instruction-limit')
        assert m.reads > 80 and sum(v['write'] for v in m.trace) == 1
    for symbol, command in itertools.product((SET, UNLIMIT, MAXOUT), (0x16, 0x17)):
        m, _ = case(symbol, dict(busy=True, busy_command=command), True, 'instruction-limit')
        assert m.command == command and m.reads > 80
    for status in (0, 1, 0xfe):
        m, row = case(READ, dict(raw=0x800003ff, busy=True, status=status))
        assert m.values(m.output, 4) == [1023] * 3 and m.bits(m.flags) == [True] * 3
        starts = [i for i, v in enumerate(m.trace) if v['write']]
        counts = [sum(1 for v in m.trace[start + 1:end] if not v['write'])
                  for start, end in zip(starts, starts[1:] + [len(m.trace)])]
        assert counts == [2, 203, 202, 202]
        row['read_counts_per_query'] = counts

    return dict(schema=2, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=PINS,
                scope='Eight complete original ICCmax/topology functions; synthetic MSR/container boundaries only. '
                      'The ninth pinned function, MAX_NON_TURBO_LIM_RATIO, is replayed separately by legacy-intel-platform.py. '
                      'No original process, driver or hardware is executed.',
                emulated_symbols=list(SCALARS) + [TOPOLOGY] + list(INDEXED), scenarios=rows,
                findings=[
                    'Scalar read masks 10 bits; indexed read and setter use 11. CPU/firmware binding and physical units remain unresolved.',
                    'Scalar setter ORs data with 800003ff, then overwrites MSR 601 with 1fff without verification.',
                    'Topology command 80000004 returns three 4-bit addresses and flags. Indexed operations ignore these flags.',
                    'Indexed query/write words contain address<<8 and 16/17, but omit bit 31. Input indexes are range checked.',
                    'Indexed setters preserve unrelated data bits but ignore errors and only poll completion, with no readback comparison.',
                    'Topology and setters can poll indefinitely. Indexed reader has a bounded poll count but returns payload even when still busy or failed.',
                    'The reader returns three raw 11-bit values and appends three bit-31 flags to the supplied bool vector.',
                    'These behaviors characterize legacy instructions, not a validated platform protocol or enabled GUI/CLI controls.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = investigate(load_fixture())
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(scenarios=len(report['scenarios']), emulated_functions=len(report['emulated_symbols']),
                          fixture_sha256=PINS, source_elf_sha256=base.SOURCE_SHA)))


if __name__ == '__main__':
    main()
