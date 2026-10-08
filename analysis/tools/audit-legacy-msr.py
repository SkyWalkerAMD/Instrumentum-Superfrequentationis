#!/usr/bin/env python3
"""Pin original MSR leaves and two complete UI slots; inventory simple busy loops."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from elftools.elf.elffile import ELFFile

SOURCE_SHA='44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
FUNCTIONS=('_Z5Wrmsrjjj','_Z5RdmsrjPjS_',
           '_ZN10intel_ctl610gt_clickedEv','_ZN10intel_ctl611npu_clickedEv')
CONSTRUCTOR='_ZN10intel_ctl6C2EP7QWidgetb'


def encoded(value):
    return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8')


def investigate(path):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    spec=importlib.util.spec_from_file_location('runtime_audit',Path(__file__).with_name('elf-runtime-audit.py'))
    audit=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    result=audit.inspect(path,re.compile('^('+'|'.join(map(re.escape,FUNCTIONS+(CONSTRUCTOR,)))+')$'),True)
    result.pop('imports')
    constructor=next(f for f in result['selected_functions'] if CONSTRUCTOR in f['symbols'])
    constructor['instruction_scope']='Selected parameter-store/branch spans only; whole declared function hash retained'
    constructor['instructions']=[i for i in constructor['instructions'] if any(lo<=i['address']<hi for lo,hi in (
        (0x600300,0x6003a5),(0x601970,0x6019e3),(0x601b21,0x601be8)))]
    elf=ELFFile(io.BytesIO(data))
    symbols=list(elf.get_section_by_name('.symtab').iter_symbols())
    byname={s.name:s for s in symbols}

    def read_symbol(s):
        section=elf.get_section(s['st_shndx'])
        off=section['sh_offset']+s['st_value']-section['sh_addr']
        return data[off:off+s['st_size']]

    fixture={'schema':1,'source_elf_sha256':SOURCE_SHA,
             'scope':'Two original MSR leaves and two complete UI slots; all libc/Qt I/O boundaries synthetic',
             'functions':[],'objects':{},'literals':[]}
    literals={}
    for name in FUNCTIONS:
        f=next(row for row in result['selected_functions'] if name in row['symbols'])
        code=read_symbol(byname[name])
        fixture['functions'].append({'symbol':name,'address':f['address'],'size':len(code),
                                    'code_hex':code.hex(),'code_sha256':hashlib.sha256(code).hexdigest(),'calls':f['calls']})
        for literal in f['literals']:
            literals[literal['address']]=literal['text']
    fixture['literals']=[{'address':va,'text':text} for va,text in sorted(literals.items())]
    result['fixture_sha256']=hashlib.sha256(encoded(fixture)).hexdigest()
    # Narrow recognizer: call Rdmsr; mov REG,[...]; test REG,REG; js BACK.
    # Require a straight-line back edge with no hidden call/branch/timeout.
    # Other loop shapes are explicitly outside this count.
    target=byname['_Z5RdmsrjPjS_']['st_value']
    decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    seen=set(); loops=[]
    for symbol in symbols:
        if symbol['st_info']['type']!='STT_FUNC' or not symbol['st_size'] or not isinstance(symbol['st_shndx'],int):
            continue
        section=elf.get_section(symbol['st_shndx'])
        if not section['sh_flags'] & 4:
            continue
        key=(symbol['st_value'],symbol['st_size'])
        if key in seen:continue
        seen.add(key)
        code=read_symbol(symbol)
        if b'\xe8' not in code:continue
        ins=list(decoder.disasm_lite(code,symbol['st_value']))
        bypc={x[0]:n for n,x in enumerate(ins)}
        for n,(pc,size,mnemonic,ops) in enumerate(ins[:-3]):
            if mnemonic!='call' or ops!=hex(target):continue
            mov,test,branch=ins[n+1:n+4]
            if mov[2]!='mov' or test[2]!='test' or branch[2]!='js':continue
            reg=mov[3].split(',')[0]
            if reg not in ('eax','ebx','ecx','edx','esi','edi','r8d','r9d','r10d','r11d') or test[3]!=reg+', '+reg or 'ptr [' not in mov[3]:continue
            back=int(branch[3],16)
            if back not in bypc or not 0<=n-bypc[back]<=8:continue
            body=ins[bypc[back]:n]
            if any(x[2] not in ('mov','lea','xor','nop') for x in body):continue
            end=branch[0]+branch[1]
            loops.append({'function':symbol.name,'function_address':symbol['st_value'],
                          'function_sha256':hashlib.sha256(code).hexdigest(),'rdmsr_call':pc,
                          'loop_start':back,'loop_end':end,
                          'instructions':[{'address':x[0],'size':x[1],'mnemonic':x[2],'operands':x[3]}
                                          for x in ins[bypc[back]:n+4]]})
    result['simple_busy_loops']=sorted(loops,key=lambda x:x['rdmsr_call'])
    result['loop_scope']='Only the documented straight-line signed-bit polling shape; no complete loop or runtime reachability claim'
    return result,fixture


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--export-fixture',type=Path,required=True)
    args=parser.parse_args()
    result,fixture=investigate(args.binary)
    for path,value in [(args.output,result),(args.export_fixture,fixture)]:
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(encoded(value))
    print('fixture SHA {}; {} simple MSR busy loops'.format(result['fixture_sha256'],len(result['simple_busy_loops'])))


if __name__=='__main__':main()
