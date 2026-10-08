#!/usr/bin/env python3
"""Bounded emulation of the original eight MMIO wrappers; no OS/hardware calls.

Only the selected function's unchanged bytes are executable. The write PLT
entry is intercepted, and its request/reply are synthetic. This characterizes
the caller contract, not kernel behaviour, GUI startup or hardware parity.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import platform
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from elftools.elf.elffile import ELFFile
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_RAX, UC_X86_REG_RDI, UC_X86_REG_RSI,
                               UC_X86_REG_RDX, UC_X86_REG_RSP, UC_X86_REG_RIP)

SHA256 = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
FUNCTIONS = {
    '_Z16Read_MMIO_kernelm': (0x0c, 4, False),
    '_Z18Read_MMIO64_kernelm': (0x0a, 8, False),
    '_Z17Read_MMIO8_kernelm': (0x10, 1, False),
    '_Z18Read_MMIO16_kernelm': (0x0e, 2, False),
    '_Z17Write_MMIO_kernelmm': (0x0d, 4, True),
    '_Z19Write_MMIO64_kernelmm': (0x0b, 8, True),
    '_Z18Write_MMIO8_kernelmh': (0x11, 1, True),
    '_Z19Write_MMIO16_kernelmt': (0x0f, 2, True),
}
MASK = (1 << 64) - 1
STACK = 0x60000000
MAILBOX = 0x60010000
STOP = 0x60020000
TOKEN = 71
ADDRESS = 0x12345000  # synthetic number, never mapped as hardware
VALUE = 0x1122334455667788
RESULT = 0xfedcba9876543210
LIMIT = 512
CASES = (
    ('success', 96, 1, 0, True),
    ('delayed_success', 96, 1, 64, True),
    ('no_completion', 96, 0, 0, False),
    ('encoded_enomem', 96, ((-12 & 0xffffffff) << 32) | 1, 0, False),
    ('write_error', -1, 0, 0, False),
    ('short_write', 12, 0, 0, False),
    ('write_error_but_done', -1, 1, 0, True),
)


def pack(value):
    return struct.pack('<Q', value & MASK)


def image(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('input differs from the documented original ELF')
    elf = ELFFile(io.BytesIO(data))
    symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    functions = []
    for name, (opcode, width, write) in FUNCTIONS.items():
        symbol = symbols[name]
        section = elf.get_section(symbol['st_shndx'])
        offset = section['sh_offset'] + symbol['st_value'] - section['sh_addr']
        code = data[offset:offset + symbol['st_size']]
        # Fixed-sample contract: exactly one direct call, to write@plt. Resolve
        # that PLT through its GOT relocation instead of assuming a slot order.
        insns = list(Cs(CS_ARCH_X86, CS_MODE_64).disasm_lite(code, symbol['st_value']))
        calls = [(pc, int(ops, 16)) for pc, _, op, ops in insns if op == 'call']
        if len(calls) != 1:
            raise ValueError('unexpected call shape: ' + name)
        functions.append({'symbol': name, 'address': symbol['st_value'], 'code': code,
                          'opcode': opcode, 'width': width, 'write': write,
                          'write_call': calls[0][0], 'write_plt': calls[0][1]})
    # The pinned image and explicit .rela.plt binding validate the sole stub.
    relocations = elf.get_section_by_name('.rela.plt')
    dynsym = elf.get_section(relocations['sh_link'])
    write_got = [r['r_offset'] for r in relocations.iter_relocations()
                 if dynsym.get_symbol(r['r_info_sym']).name == 'write']
    from capstone.x86 import X86_OP_MEM, X86_REG_RIP
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    for fn in functions:
        va = fn['write_plt']
        sec = next(s for s in elf.iter_sections()
                   if s['sh_addr'] <= va < s['sh_addr'] + s['sh_size'] and s['sh_flags'] & 4)
        off = sec['sh_offset'] + va - sec['sh_addr']
        slots = [ins.address + ins.size + op.mem.disp
                 for ins in decoder.disasm(data[off:off + 16], va) if ins.mnemonic == 'jmp'
                 for op in ins.operands if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP]
        if not any(slot in write_got for slot in slots):
            raise ValueError('call does not resolve to write@plt')
    objects = {name: symbols[name]['st_value']
               for name in ('kernel_address', 'kernel_fd', 'user_request')}
    return functions, objects


def run_case(fn, objects, case):
    name, write_return, done, delay, expected_return = case
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    pages = {fn['address'] & ~4095, (fn['address'] + len(fn['code']) - 1) & ~4095,
             fn['write_plt'] & ~4095, STACK, MAILBOX, STOP}
    for addr in objects.values():
        pages.update((addr & ~4095, (addr + 95) & ~4095))
    for page in sorted(pages):
        uc.mem_map(page, 4096)
    uc.mem_write(fn['address'], fn['code'])
    uc.mem_write(objects['kernel_address'], pack(MAILBOX))
    uc.mem_write(objects['kernel_fd'], struct.pack('<I', 600))
    # Nonzero sentinels reveal which fields are overwritten, including fields
    # retained from an earlier call in the global request object.
    initial = [0xabcdef0000000000 + n for n in range(12)]
    initial[1] = TOKEN
    uc.mem_write(objects['user_request'], struct.pack('<12Q', *initial))
    uc.mem_write(MAILBOX, pack(1) + pack(RESULT))
    uc.reg_write(UC_X86_REG_RSP, STACK + 4088)
    uc.mem_write(STACK + 4088, pack(STOP))
    uc.reg_write(UC_X86_REG_RDI, ADDRESS)
    uc.reg_write(UC_X86_REG_RSI, VALUE)
    state = {'instructions': 0, 'calls': [], 'returned': False, 'post_write_steps': 0}

    def hook(emulator, pc, size, unused):
        state['instructions'] += 1
        if pc == STOP:
            state['returned'] = True
            emulator.emu_stop()
            return
        if pc == fn['write_plt']:
            if state['calls']:
                raise ValueError('unexpected second write')
            request = bytes(emulator.mem_read(emulator.reg_read(UC_X86_REG_RSI), 96))
            state['calls'].append({'fd': emulator.reg_read(UC_X86_REG_RDI),
                                   'count': emulator.reg_read(UC_X86_REG_RDX),
                                   'request_hex': request.hex(),
                                   'mailbox_before_write': struct.unpack('<Q', emulator.mem_read(MAILBOX, 8))[0]})
            emulator.mem_write(MAILBOX, pack(0 if delay else done) + pack(RESULT))
            sp = emulator.reg_read(UC_X86_REG_RSP)
            return_pc = struct.unpack('<Q', emulator.mem_read(sp, 8))[0]
            emulator.reg_write(UC_X86_REG_RSP, sp + 8)
            emulator.reg_write(UC_X86_REG_RAX, write_return & MASK)
            emulator.reg_write(UC_X86_REG_RIP, return_pc)
            return
        if not fn['address'] <= pc < fn['address'] + len(fn['code']):
            raise ValueError('execution escaped selected function')
        if state['calls']:
            state['post_write_steps'] += 1
            if delay and state['post_write_steps'] == delay:
                emulator.mem_write(MAILBOX, pack(done))

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(fn['address'], STOP + 1, timeout=1000000, count=LIMIT)
    assert len(state['calls']) == 1
    call = state['calls'][0]
    expected = initial[:]
    expected[0], expected[2] = fn['opcode'], ADDRESS
    if fn['write']:
        expected[3] = VALUE & ((1 << (8 * fn['width'])) - 1) if fn['width'] < 4 else VALUE
    assert call['fd'] == 600 and call['count'] == 96 and call['mailbox_before_write'] == 0
    assert call['request_hex'] == struct.pack('<12Q', *expected).hex()
    assert state['returned'] == expected_return, (fn['symbol'], name, state)
    if not expected_return:
        assert state['instructions'] == LIMIT, 'stopped for a reason other than the instruction bound'
    if expected_return and not fn['write']:
        assert uc.reg_read(UC_X86_REG_RAX) == RESULT
    return {'case': name, 'write_return': write_return, 'reply_done_hex': hex(done),
            'reply_delay_instructions': delay, 'returned': state['returned'],
            'instructions': state['instructions'], 'stop_pc': hex(uc.reg_read(UC_X86_REG_RIP)),
            'read_result_hex': hex(uc.reg_read(UC_X86_REG_RAX)) if state['returned'] and not fn['write'] else None,
            'write': call}


def investigate(path):
    functions, objects = image(path)
    result = {'schema': 1, 'input_sha256': SHA256,
              'environment': {'python': platform.python_version(), 'system': platform.system(),
                              'unicorn': unicorn.__version__},
              'scope': 'bounded original MMIO caller emulation; write/reply are synthetic; no hardware or kernel execution',
              'instruction_limit_per_case': LIMIT, 'functions': []}
    for fn in functions:
        result['functions'].append({key: value for key, value in fn.items() if key != 'code'})
        result['functions'][-1].update(code_sha256=hashlib.sha256(fn['code']).hexdigest(),
                                       cases=[run_case(fn, objects, case) for case in CASES])
    result['cases_passed'] = len(functions) * len(CASES)
    return result


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = investigate(args.binary)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(result, indent=2) + '\n').encode('utf-8'))
    print('{} caller-contract cases passed; synthetic replies, no hardware'.format(result['cases_passed']))


if __name__ == '__main__':
    main()
