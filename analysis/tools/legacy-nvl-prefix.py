#!/usr/bin/env python3
"""Continue the original NVL import through its fixed MSR prelude, without hardware."""
import argparse
import copy
import hashlib
import io
import itertools
import json
from pathlib import Path
import platform
import re
import importlib.util

from elftools.elf.elffile import ELFFile
import unicorn

spec = importlib.util.spec_from_file_location('nvl_profile', Path(__file__).with_name('legacy-nvl-profile.py'))
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)
msr, base = profile.msr, profile.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-nvl-prefix.json'
FIXTURE_SHA = '4f3583a200896ad98c7d19d683ee5626d9c1195bfe2a974f5b0f4f3a654fd76c'
HELPERS = ('_Z5Wr1ADiiiiiiii', '_Z5Wr650iiiiiiii', '_Z9get_32bithhhh')
END = 0x6187a4  # first object/core-count load, after both fixed preset helpers


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    audit = profile.module('prefix_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(' + '|'.join(map(re.escape, HELPERS)) + ')$'), True)
    elf = ELFFile(io.BytesIO(data))
    functions = []
    for name in HELPERS:
        function = next(f for f in report['selected_functions'] if name in f['symbols'])
        address, size = function['address'], function['size']
        segment = next(s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and
                       s['p_vaddr'] <= address and address + size <= s['p_vaddr'] + s['p_filesz'])
        offset = segment['p_offset'] + address - segment['p_vaddr']
        code = data[offset:offset + size]
        functions.append(dict(symbol=name, address=address, size=size, code_hex=code.hex(),
                              code_sha256=hashlib.sha256(code).hexdigest(), calls=function['calls']))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA,
                profile_fixture_sha256=profile.FIXTURE_SHA, msr_fixture_sha256=msr.FIXTURE_SHA,
                scope='Original Load Profile through complete percoreoverride_en, Wr1AD, Wr650, '
                      'get_32bit, Wrmsr and Rdmsr bodies. Stop before per-core payload application. '
                      'Qt, streams and libc are synthetic; no host device files or hardware.',
                functions=functions)


def load_fixture(path=FIXTURE):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('NVL prefix fixture hash mismatch')
    fixture = json.loads(raw)
    if (fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA or
            fixture['profile_fixture_sha256'] != profile.FIXTURE_SHA or
            fixture['msr_fixture_sha256'] != msr.FIXTURE_SHA or
            tuple(f['symbol'] for f in fixture['functions']) != HELPERS):
        raise ValueError('NVL prefix fixture dependencies differ')
    return fixture


class PrefixMachine(profile.ProfileMachine):
    def __init__(self, fixture, inputs):
        combined = copy.deepcopy(profile.load_fixture())
        combined['functions'].extend(fixture['functions'])
        combined['functions'].extend(f for f in msr.load_fixture()['functions'] if f['symbol'] == '_Z5RdmsrjPjS_')
        super().__init__(combined, inputs)
        self.requested_msr = None
        self.profile_reads = []
        self.pre_read_buffer_accesses = []
        self.uc.hook_add(unicorn.UC_HOOK_MEM_READ, self.on_profile_read)

    def on_profile_read(self, uc, access, address, size, value, unused):
        if address < self.profile_buffer + profile.PROFILE_SIZE and address + size > self.profile_buffer:
            # The entry prologue probes a stack page with `or [rsp], 0`.
            # That precedes istream.read and is not consumption of file data.
            rows = self.profile_reads if self.read_request is not None else self.pre_read_buffer_accesses
            rows.append(dict(instruction=self.reg('rip'), address=address, size=size))

    def hook(self, uc, pc, size, unused):
        if pc == END:
            self.stop('fixed-prefix-complete')
            return
        super().hook(uc, pc, size, unused)

    def external(self, name):
        if name == 'lseek@plt':
            self.requested_msr = self.reg('rsi')
            if self.requested_msr not in (0x150, 0x1ad, 0x650) or self.reg('rdx') != 0:
                raise ValueError('unexpected prefix MSR seek')
            result = -1 if self.mode in ('open-error', 'seek-error') else self.requested_msr
            self.calls.append(name)
            self.io.append(dict(op='lseek', fd=self.reg('rdi') & 0xffffffff,
                                offset=self.requested_msr, result=result))
            self.ret(result)
        elif name in ('read@plt', 'write@plt'):
            mode = self.mode
            writes = sum(row['op'] == 'write' for row in self.io)
            if name == 'read@plt':
                if mode == 'commit-busy':
                    self.mode = 'busy-forever' if writes >= 2 else 'success'
                elif mode == 'commit-read-error':
                    self.mode = 'read-error' if writes >= 2 else 'success'
                elif mode == 'second-read-error':
                    self.mode = 'read-error' if self.reads == 1 else 'success'
            # Execute the unchanged MSR leaves; only their libc boundary is
            # synthetic. requested_msr records the seek argument, even when
            # the injected seek fails; it does not claim a real device offset.
            try:
                msr.MsrMachine.external(self, name)
            finally:
                self.mode = mode
            self.io[-1]['requested_msr'] = self.requested_msr
        else:
            super().external(name)


def investigate(fixture):
    rows = []

    def case(inputs, expected):
        machine = PrefixMachine(fixture, inputs)
        result = machine.run(profile.LOAD, instruction_limit=3000, allow_instruction_bound=True)
        if result['outcome'] != expected:
            raise AssertionError((inputs, expected, result['outcome']))
        writes = [r for r in machine.io if r['op'] == 'write']
        if expected == 'fixed-prefix-complete':
            if ([r['requested_msr'] for r in writes] != [0x150, 0x150, 0x1ad, 0x650] or
                    [(r['low'], r['high']) for r in writes[:2]] !=
                    [(0, 0x80000014), (inputs.get('reply_low', 0x1234) | 8, 0x80000015)] or
                    [(r['low'], r['high']) for r in writes[-2:]] != [(0x23232323, 0x23232323)] * 2 or
                    machine.profile_reads):
                raise AssertionError('fixed import prefix differs')
        elif expected in ('returned', 'oversize-read-boundary') and machine.io:
            raise AssertionError('rejected profile reached MSR operations')
        elif expected == 'instruction-limit' and any(r['requested_msr'] != 0x150 for r in writes):
            raise AssertionError('busy loop unexpectedly reached preset writes')
        rows.append(dict(inputs=inputs, read_request=machine.read_request, file_events=machine.file_events,
                         pre_read_buffer_accesses=machine.pre_read_buffer_accesses,
                         profile_reads=machine.profile_reads, libc_io=machine.io, **result))

    for delivered, fill, reply in itertools.product((0, 1, profile.PROFILE_SIZE-1, profile.PROFILE_SIZE),
                                                    (0, 255), (0, 0x1234, 0xffffffff)):
        case(dict(actual_read=delivered, payload_byte=fill, reply_low=reply), 'fixed-prefix-complete')
    stuck = {'busy-forever', 'commit-busy', 'commit-read-error', 'read-error',
             'read-eof', 'short-read-4', 'short-read-7', 'open-error'}
    modes = sorted(stuck | {'delayed', 'second-read-error', 'write-error', 'short-write', 'close-error', 'seek-error'})
    for mode, delivered in itertools.product(modes, (0, profile.PROFILE_SIZE)):
        case(dict(io_mode=mode, actual_read=delivered),
             'instruction-limit' if mode in stuck else 'fixed-prefix-complete')
    for inputs in (dict(cancel=True), dict(qt_open=False), dict(first_open=False),
                   dict(second_open=False), dict(tell_last=profile.PROFILE_SIZE-1)):
        case(inputs, 'returned')
    case(dict(tell_last=profile.PROFILE_SIZE+1), 'oversize-read-boundary')
    case(dict(close_error=True), 'fixed-prefix-complete')
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA,
                profile_fixture_sha256=profile.FIXTURE_SHA, scope=fixture['scope'],
                environment=dict(system=platform.system(), python=platform.python_version(),
                                 unicorn=unicorn.__version__),
                cases_characterized=len(rows), observations=rows)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.binary:
        if not args.export_fixture:
            parser.error('--binary requires --export-fixture')
        fixture = extract(args.binary)
        raw = profile.encoded(fixture)
        args.export_fixture.write_bytes(raw)
        print('fixture SHA ' + hashlib.sha256(raw).hexdigest())
    else:
        fixture = load_fixture(args.fixture)
    if args.output:
        result = investigate(fixture)
        args.output.write_bytes(profile.encoded(result))
        print(str(result['cases_characterized']) + ' original NVL prefix cases; synthetic I/O only')
    elif not args.binary:
        parser.error('provide --output')


if __name__ == '__main__':
    main()
