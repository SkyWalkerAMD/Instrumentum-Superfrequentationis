#!/usr/bin/env python3
"""Byte-checked Granite mailbox/table selection and command bounds.

This establishes which table the dispatcher selects, not operating-system
permission, electrical identity, command safety or peripheral acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).parent
H = runpy.run_path(str(HERE/'firmware-granite-semantics.py'))
T = runpy.run_path(str(HERE/'firmware-granite-topology.py'))
Slice = H['Slice']
MAILBOXES, TABLE_POINTERS, TABLE_COUNTS = 0x6abc, 0x7eec, 0x7f5c


def initialized(body, code, slot):
    vm = Slice(body, code); vm.r.update(a1=0x50000, a2=slot)
    # Explicit fixture: initial linked mailbox/dispatch descriptors. Runtime
    # modification of these RAM descriptors has not been ruled out.
    vm.ram.update(enumerate(body[0x6abc:0x8000], 0x6abc))
    vm.put(0x82fb, 0, 1); vm.put(0x8197, 0, 1); vm.put(0x7fcc, 0)
    return vm


def table_selection(body, code):
    trace = hashlib.sha256(); valid = null = outside = 0; tables = []
    u = lambda a: struct.unpack_from('<I', body, a)[0]
    for slot in range(28):
        table, count = u(TABLE_POINTERS+4*slot), u(TABLE_COUNTS+4*slot)
        tables.append(dict(mailbox=slot, table=hex(table), command_count=count,
            argument=hex(u(MAILBOXES+12*slot)), response=hex(u(MAILBOXES+12*slot+4)),
            message=hex(u(MAILBOXES+12*slot+8))))
        for command in (*range(count), count, count+1, 0xffffffff):
            vm = initialized(body, code, slot); replies = []
            def input_word(v):
                assert v.r['a10'] == slot; v.r['a10'] = command
            def response(v):
                assert v.r['a10'] == slot; replies.append(v.r['a11'])
            hooks = {0x14b8: input_word, 0x14ec: response}
            if command >= count:
                vm.run(0x136c, None, hooks=hooks)
                assert replies == [0xfe]; outside += 1
            else:
                vm.run(0x136c, 0x13bb, hooks=hooks)
                assert vm.r['a3'] == table and vm.r['a4'] == command
                handler, flags = struct.unpack_from('<II', body, table+8*command)
                if not handler:
                    vm.run(0x13bb, None, hooks=hooks)
                    assert replies == [0xfe]; null += 1
                else:
                    vm.run(0x13bb, 0x13f9, hooks=hooks)
                    assert vm.r['a3'] == table+8*command
                    assert vm.get(vm.r['a3']) == handler
                    assert vm.r['a12'] == (flags >> 8) & 15
                    assert vm.r['a11'] == 2 and vm.get(0x7fcc) == 2
                    assert not replies; valid += 1
                trace.update(struct.pack('<5I', slot, command, table, handler, flags))
    # Explicit route assertions keep unrelated adjacent tables from being
    # interpreted as large RSMU command numbers.
    assert u(TABLE_POINTERS) == 0x6c0c and u(TABLE_COUNTS) == 0x73
    assert struct.unpack_from('<II', body, 0x6c0c+8*0x70) == (0x330b0, 6)
    assert u(TABLE_POINTERS+4) == u(TABLE_POINTERS+8) == 0x6fa4
    assert u(TABLE_COUNTS+4) == u(TABLE_COUNTS+8) == 0xe9
    assert u(0x6fa4+8*0x70) == 0x29e54
    assert u(TABLE_POINTERS+4*5) == 0x7ab4 and u(TABLE_COUNTS+4*5) == 0x1d
    assert u(0x7ab4+8*0x1b) == 0x26cac
    return dict(nonnull_entries=valid, null_entry_rejections=null,
        out_of_range_rejections=outside, tables=tables,
        table_gate_execution=False, queue_or_indirect_call_execution=False,
        descriptors_are_initial_image_fixture=True, trace_sha256=trace.hexdigest())


def mailbox_read(body, code):
    trace = hashlib.sha256(); cases = 0
    for slot in (0, 1, 2, 5):
        for command in (0, 1, 0x70, 0x90, 0xe8, 0xe9, 0x17d, 0xffffffff):
            vm = initialized(body, code, slot)
            message = vm.get(MAILBOXES+12*slot+8)
            vm.put(message, command)
            before = vm.ram.copy()
            vm.run(0x14b8, None, hooks={})
            assert vm.r['a2'] == command and vm.ram == before
            trace.update(struct.pack('<3I', slot, message, command)); cases += 1
    return dict(cases=cases, original_mailbox_reader='0x14b8',
        runtime_mmio_is_fixture=True, physical_mailbox_access=False,
        trace_sha256=trace.hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('smu', 'study', 'output'): p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args(); body = a.smu.read_bytes()
    code = T['instructions'](a.study, body, T['SMU_SHA'])
    assert struct.unpack_from('<I', body, 0xc20)[0] == 0x7d28
    assert 0x7d28+0x1c4 == TABLE_POINTERS and 0x7d28+0x234 == TABLE_COUNTS
    report = dict(schema=1, source_sha256=T['SMU_SHA'], hardware_execution=False,
        dispatcher=table_selection(body, code), mailbox_read=mailbox_read(body, code),
        guarded_svi_writer=dict(mailbox=0, command='0x70', handler='0x330b0',
            rsmu_route=False, physical_rail_identity_verified=False),
        neighboring_temperature_getter=dict(mailbox=5, command='0x1b', handler='0x26cac',
            rsmu_command_0x17d_valid=False, host_product_route_verified=False),
        independent_physical_rail_protocol_verified=False,
        operating_system_permission_verified=False, hardware_effect_measured=False)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
