#!/usr/bin/env python3
"""Static/synthetic Granite 61h handler research; never a hardware writer.

No physical rail identity or setter/readback pair is inferred. The tested
semantics cover integer storage and a bounded shared-floor consumer slice.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

SHA = '5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596'


def consumer(body, instructions):
    """Replay storage -> selection -> float floor on synthetic RAM only.

    Stop before opaque ceil.s. Replay the independent float store explicitly;
    the encoded hardware request, peripheral side effects and readback are
    outside this experiment. Positive finite fixtures avoid FP exceptions.
    """
    helpers = runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))
    Slice, f32 = helpers['Slice'], helpers['f32']
    u = lambda a: struct.unpack_from('<I', body, a)[0]
    assert u(0x11514) == 0xae30 and u(0x116b4) == 0x8c30
    assert u(0x1178c) == 0xb9a0 and u(0x118e0) == 0xa19c
    assert u(0x11484) == 0x3e7ae148 and u(0x11480) == 0x3ba3d70a
    assert instructions[0x21069]['text'] == 'ceil.s a3,f4,0x0'
    assert instructions[0x21072]['text'] == 'ssi f3,a5,0x0'
    assert instructions[0x210c7]['text'] == 'call8 0x0002100c'
    assert instructions[0x210cd]['text'] == 'lsi f0,a8,0x0'
    assert instructions[0x210d0]['text'] == 'ssi f0,a8,0x4'
    offset, scale = (struct.unpack_from('<f', body, p)[0] for p in (0x11484,0x11480))
    cases = accepted = rejected = selected_second = raised_by_second = 0
    trace = hashlib.sha256()
    for code, selector, count, policy, floor in itertools.product(
            range(512), (0,1), (1,2,4), (0,1), (0.0,1.25,3.0)):
        cap = (0,80,255)[code % 3]
        response, writes = replay(body, instructions, selector<<9 | code, 1, count, cap)
        vm = Slice(body, instructions)
        vm.put(0xae68,1,1); vm.put(0xae67,count,1); vm.put(0xa19c,policy,1)
        vm.putf(0xb9a8,floor)
        initial = [17+7*i for i in range(count)]
        for i,value in enumerate(initial): vm.put(0xaf28+2*i,value,2)
        for address,value in writes.items(): vm.put(address,value,2)
        chosen = count-1 if policy else 0
        expected_code = min(code,cap) if cap else code
        expected_code = expected_code if selector<count and selector==chosen else initial[chosen]
        expected = max(floor,f32(offset+expected_code*scale))
        vm.run(0x2100c,0x21060)
        assert vm.r['a11'] == expected_code and vm.f['f3'] == expected
        # The store is after ceil.s, but its operands do not depend on ceil.s.
        # Replaying it in isolation is not execution of the encoded-code path.
        vm.run(0x21072,0x21075)
        assert vm.getf(0x8c30) == expected
        vm.run(0x210ca,0x210d3)  # caller mirrors the shared float into +4
        assert vm.getf(0x8c34) == expected
        ok = selector<count
        assert response == (1 if ok else 0xfd)
        cases += 1; accepted += int(ok); rejected += int(not ok)
        selected_second += int(ok and selector==chosen==1)
        raised_by_second += int(ok and selector==chosen==1 and expected>floor)
        trace.update(struct.pack('<6Iff',code,selector,count,policy,cap,expected_code,floor,expected))
    for floor in (0.0,1.25,3.0):
        vm = Slice(body,instructions); vm.put(0xae68,0,1); vm.putf(0xb9a8,floor)
        vm.run(0x2100c,0x21085)  # no configured-count or array read on this branch
        assert vm.f['f3'] == floor
    return dict(checked=cases,accepted_stores=accepted,rejected_stores=rejected,
        selected_second_slot=selected_second,second_slot_above_shared_input=raised_by_second,
        disabled_branch_cases=3,entry='0x2100c',shared_float='0x8c30',caller_mirror='0x8c34',
        selection='byte[0xa19c] == 0 ? first : last configured entry',
        code_to_float='float32(0.245 + code * 0.005)',
        combination='max(existing shared input, converted selected code)',
        trace_sha256=trace.hexdigest(),ceil_and_encoded_code_path_emulated=False,
        physical_rail_identity_verified=False,hardware_register_write_emulated=False,
        independent_second_rail_getter_verified=False)


def channel_routing(body, instructions):
    """Bounded callers of the application helper; no peripheral is executed.

    Codes here are fixtures at the output of the unmodeled ceil.s path.
    The update of a software mirror after a stubbed helper is not a readback.
    """
    helpers=runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))
    Slice,f32=helpers['Slice'],helpers['f32']
    for pc,text in {0x20be1:'call8 0x00020e4c',0x20c05:'call8 0x00020e4c',
            0x20fc0:'call8 0x00032d6c'}.items(): assert instructions[pc]['text']==text
    cases=calls=direct=blocked=0; trace=hashlib.sha256()
    for requested,current in itertools.product(range(512),(0,1,127,255,511)):
        for start,call,end,needed in ((0x20bd1,0x20be1,0x20be4,requested>current),
                (0x20bf5,0x20c05,0x20c08,requested<current)):
            vm=Slice(body,instructions)
            vm.put(0x8c6c,current,2); vm.put(0x8c6e,requested,2)
            vm.r.update(a10=0xdeadbeef,a12=0xfeedface)
            vm.run(start,call if needed else end)
            if needed:
                assert [vm.r[r] for r in ('a10','a11','a12')]==[0,requested,0]
                calls+=1
            cases+=1
            trace.update(struct.pack('<4I',start,requested,current,int(needed)))
    offset,scale=(struct.unpack_from('<f',body,p)[0] for p in (0x11484,0x11480))
    for requested,locked in itertools.product(range(512),(0,1)):
        vm=Slice(body,instructions); vm.put(0x8c70,locked,2); vm.put(0x8c6e,requested,2)
        vm.put(0x8c6c,0xffff,2); vm.putf(0x8c74,-999)
        vm.run(0x20fb0,0x20fde if locked else 0x20fc0)
        if locked:
            assert vm.get(0x8c6c,2)==0xffff and vm.getf(0x8c74)==-999; blocked+=1
        else:
            assert vm.r['a10']==0 and vm.r['a11']==requested
            # Explicitly skip the hardware call, keeping caller-local registers.
            vm.run(0x20fc3,0x20fde)
            assert vm.get(0x8c6c,2)==requested and vm.getf(0x8c74)==f32(offset+requested*scale)
            direct+=1
    return dict(comparison_cases=cases,application_calls=calls,application_channel=0,
        direct_cases=direct,locked_cases=blocked,software_mirror='ushort[0x8c6c], float[0x8c74]',
        trace_sha256=trace.hexdigest(),hardware_helper_stubbed=True,
        encoded_ceil_path_emulated=False,software_mirror_is_hardware_readback=False,
        second_independent_rail_verified=False)


def register_mailbox(body, instructions):
    """Argument/return marshaling only for a newly located register transport.

    Locks, MMIO helpers and mailbox side effects are not executed. In particular,
    out-of-range channel fixtures stop before dereferencing the config array.
    This is not a validated voltage setter/getter pair or an exposed runtime API.
    """
    Slice=runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))['Slice']
    u=lambda a:struct.unpack_from('<I',body,a)[0]
    assert u(0x6fa4+8*0x90)==0x33054 and u(0x6fa4+8*0x91)==0x33084
    for pc,text in {0x3306a:'call8 0x00032bf8',0x33071:'call8 0x00001580',
            0x3309d:'call8 0x00032cbc',0x32cb6:'l8ui a2,a1,0x1'}.items():
        assert instructions[pc]['text']==text
    reads=writes=0;trace=hashlib.sha256()
    for channel,register,byte,upper in itertools.product((0,1,2,3,255),range(256),(0,128,255),(0,0xff)):
        vm=Slice(body,instructions);vm.r.update(a1=0x50000,a2=7,a10=channel|(register<<8)|(upper<<16)|(upper<<24))
        vm.run(0x3305c,0x33061);vm.run(0x33064,0x3306a)
        assert vm.r['a10']==channel and vm.r['a11']==register
        # Helper's completed reply word: extract bits 15:8, not the sent VID.
        helper=Slice(body,instructions);helper.r['a1']=0x60000
        helper.put(0x60000,0x400000ff|(byte<<8)|(upper<<16))
        helper.run(0x32cb6,0x32cb9);assert helper.r['a2']==byte
        vm.r['a10']=helper.r['a2'];vm.run(0x3306d,0x33071)
        assert vm.r['a10']==7 and vm.r['a11']==byte;reads+=1
        vm=Slice(body,instructions);vm.r.update(a1=0x50000,a2=7,a10=channel|(register<<8)|(byte<<16)|(upper<<24))
        vm.run(0x3308c,0x33091);vm.run(0x33094,0x3309d)
        assert [vm.r[r] for r in ('a10','a11','a12')]==[channel,register,byte]
        vm.run(0x330a6,0x330aa);assert vm.r['a10']==7 and vm.r['a11']==1;writes+=1
        trace.update(struct.pack('<4I',channel,register,byte,upper))
    return dict(read_command='0x90',write_command='0x91',read_handler='0x33054',write_handler='0x33084',
        read_cases=reads,write_cases=writes,argument='channel bits 7:0; register bits 15:8; write byte bits 23:16',
        read_reply='byte from completed hardware reply bits 15:8',
        read_mmio_helper='0x32bf8',write_mmio_helper='0x32cbc',
        handler_channel_bounds_check_present=False,peripheral_helpers_stubbed=True,
        physical_channel_identity_verified=False,voltage_register_contract_verified=False,
        independent_second_rail_verified=False,runtime_enabled=False,trace_sha256=trace.hexdigest())


def replay(body, instructions, argument, enabled, count, cap):
    registers = {'a'+str(i):0 for i in range(16)}
    memory = {0xae68:enabled,0xae67:count,0x13549:cap}
    writes, response, pc = {}, None, 0x2a644
    for _ in range(40):
        instruction = instructions[pc]
        raw = bytes.fromhex(instruction['bytes'])
        assert body[pc:pc+len(raw)] == raw
        op, _, text = instruction['text'].partition(' ')
        a = [s.strip() for s in text.split(',') if s.strip()]
        op = op.removesuffix('.n')
        val = lambda s: registers[s] if s.startswith('a') else int(s,0)
        next_pc = pc+len(raw)
        if op == 'entry': pass
        elif op == 'retw':
            assert response in (1,0xfd)
            return response,writes
        elif op in ('mov','movi'): registers[a[0]] = val(a[1])
        elif op == 'l32r': registers[a[0]] = struct.unpack_from('<I',body,int(a[1],0))[0]
        elif op == 'l8ui': registers[a[0]] = memory[val(a[1])+int(a[2],0)]
        elif op == 'extui': registers[a[0]] = (val(a[1])>>int(a[2],0)) & ((1<<int(a[3],0))-1)
        elif op == 'beqz':
            if not val(a[0]): next_pc = int(a[1],0)
        elif op == 'bgeu':
            if val(a[0]) >= val(a[1]): next_pc = int(a[2],0)
        elif op == 'addx2': registers[a[0]] = 2*val(a[1])+val(a[2])
        elif op == 'minu': registers[a[0]] = min(val(a[1]),val(a[2]))
        elif op == 'moveqz':
            if not val(a[2]): registers[a[0]] = val(a[1])
        elif op == 's16i':
            address = val(a[1])+int(a[2],0)
            assert address not in writes
            writes[address] = val(a[0]) & 65535
        elif op == 'call8':
            target = int(a[0],0)
            if target == 0x15dc: registers['a10'] = argument
            elif target == 0x14ec: response = registers['a11']
            else: raise ValueError('Unmodelled call: '+instruction['text'])
        else: raise ValueError('Unmodelled instruction: '+instruction['text'])
        pc = next_pc
    raise ValueError('Instruction budget exceeded')


def main():
    p = argparse.ArgumentParser()
    for key in ('smu','study','output'): p.add_argument('--'+key,type=Path,required=True)
    args = p.parse_args()
    body = args.smu.read_bytes()
    assert hashlib.sha256(body).hexdigest() == SHA
    study = json.loads(args.study.read_text(encoding='utf-8'))
    assert study['source_sha256'] == SHA
    functions = {int(f['entry'],16):f for f in study['functions']}
    instructions = {int(i['address'],16):i for f in functions.values() for i in f['instructions']}
    for address,i in instructions.items():
        raw=bytes.fromhex(i['bytes']); assert body[address:address+len(raw)]==raw
    u = lambda a:struct.unpack_from('<I',body,a)[0]
    for command, entry in ((0x61,0x2a644),(0x6e,0x2a7c8),(0x70,0x29e54),(0xa5,0x25d8c)):
        assert u(0x6fa4+8*command) == entry
        f = functions[entry]
        assert not f['bad_instruction_truncation'] and not f['instructions_truncated']
        for i in f['instructions']:
            address = int(i['address'],16); raw = bytes.fromhex(i['bytes'])
            assert body[address:address+len(raw)] == raw
    for entry in (0x2100c,0x210c4):
        f = functions[entry]
        assert not f['bad_instruction_truncation'] and not f['instructions_truncated']
        for i in f['instructions']:
            address = int(i['address'],16); raw = bytes.fromhex(i['bytes'])
            assert body[address:address+len(raw)] == raw
    assert u(0x11514) == 0xae30 and u(0x11724) == 0x134c4
    checked = accepted = rejected = second_slot = 0
    cases = itertools.chain(
        itertools.product(range(512),(0,1),(0,1),(1,2,255),(0,80,255),(0,)),
        itertools.product((0,1,79,80,511),(0,1,2,15,255),(0,1,255),(0,1,2,16,255),(0,1,80,255),(0,0xfffe0000)))
    for code, selector, enabled, count, cap, upper in cases:
        argument = upper | selector<<9 | code
        response,writes = replay(body,instructions,argument,enabled,count,cap)
        ok = bool(enabled) and selector < count
        expected = {0xaf28+2*selector:min(code,cap) if cap else code} if ok else {}
        assert response == (1 if ok else 0xfd) and writes == expected
        checked += 1; accepted += int(ok); rejected += int(not ok)
        second_slot += int(ok and selector == 1)
    # A5 reads a mailbox argument into a10, then does not use it before a10 is
    # overwritten with the mailbox index. The output comes from two fixed
    # indexed float loads, not from a target-selected 61h storage element.
    between = [instructions[a]['text'] for a in sorted(instructions) if 0x25d94 <= a < 0x25daf]
    assert all('a10' not in instruction for instruction in between)
    assert instructions[0x25daf]['text'] == 'mov.n a10,a2'
    assert instructions[0x25d97]['text'] == 'l16ui a13,a15,0xb0'
    assert instructions[0x25d9a]['text'] == 'l16ui a14,a15,0x74'
    assert u(0x115c0) == 0x9d20
    # 6Eh's fixed scalar is distinct from 61h's selected halfword array. This
    # does not establish a physical rail getter or prove all possible callers.
    scalar = functions[0x2a7c8]['instructions']
    assert any(i['text'].startswith('lsi ') and i['text'].endswith(',0xb4') for i in scalar)
    assert any(i['text'].endswith(',0x00011514') for i in scalar)
    consumer_report = consumer(body,instructions)
    report = dict(schema=4,source_sha256=SHA,hardware_execution=False,mailbox_helpers_stubbed=True,
        checked=checked,accepted=accepted,rejected=rejected,accepted_second_slot=second_slot,
        command='0x61',handler='0x2a644',selector_bits='16:9',code_bits='8:0',
        guard='byte[0xae68] != 0 and selector < byte[0xae67]',
        clamp='cap = byte[0x13549]; cap == 0 ? code : min(code, cap)',
        halfword_array='0xaf28 + 2 * selector',a5_argument_affects_output=False,
        a5_source='0x9d20 + 4 * ushort[0x9d20 + 0xb0/0x74]',
        command_6e_fixed_float='0xaee4',physical_rail_identity_verified=False,
        physical_units_verified=False,consumer_verified=False,
        shared_floor_consumer_slice_verified=True,consumer=consumer_report,
        channel_routing=channel_routing(body,instructions),
        svi_register_mailbox=register_mailbox(body,instructions),
        rail_readback_protocol_verified=False,
        runtime_write_enabled=False)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Verified',checked,'synthetic 61h storage cases and',consumer_report['checked'],
          'shared-floor cases; no live setter/readback enabled')


if __name__ == '__main__': main()
