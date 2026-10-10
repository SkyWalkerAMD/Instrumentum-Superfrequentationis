#!/usr/bin/env python3
"""Regenerate (or verify) the portable UMC descriptors and original-code vectors."""
import argparse
from collections import defaultdict, deque
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]


def generated():
    contract=json.loads((ROOT/'analysis/contracts/amd-umc-fields.json').read_bytes())
    proof=json.loads((ROOT/'docs/validation/legacy-amd-umc-analysis.json').read_bytes())
    assert contract['fixture_sha256']==proof['fixture_sha256']=='0a4cfbda11940d92d8cc24bdde0bf48350631881359f4ae2572cb131de834be9'
    assert contract['source_elf_sha256']==proof['source_elf_sha256']=='44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
    assert contract['descriptors']==proof['descriptors']
    fields=contract['descriptors']; by_name=defaultdict(deque)
    for f in fields: by_name[f['name']].append(f['index'])
    bindings={}
    for group,labels in enumerate(contract['label_groups']):
        for name in labels: bindings[by_name[name].popleft()]=group
    assert len(bindings)==212 and all(not v for v in by_name.values())
    descriptors='// Generated from original UMC field descriptors. Unique IDs preserve duplicate names.\n'
    descriptors+='\n'.join(f'        {{{f["index"]}, {bindings[f["index"]]}, "{f["name"]}", 0x{f["offset"]:x}, {f["lsb"]}, {f["msb"]}}},' for f in fields)+'\n'
    vectors='// Outputs from original Populate_Values with five synthetic PCI scenarios.\n'
    vectors+='static const std::uint32_t originalValues[5][212] = {\n'
    for case in proof['scenarios']: vectors+='    {'+','.join(str(v)+'u' for v in case['fields'])+'},\n'
    vectors+='};\n'
    return {'gui/core/amd_umc_fields.inc':descriptors,'gui/tests/amd_umc_vectors.inc':vectors}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    for path,text in generated().items():
        if args.check:
            if (ROOT/path).read_bytes().replace(b'\r\n',b'\n')!=text.encode('utf-8'):
                raise ValueError('Generated UMC evidence differs: '+path)
        else: (ROOT/path).write_bytes(text.encode('utf-8'))
    print('UMC descriptors and original-code vectors verified' if args.check else 'UMC tables generated')


if __name__=='__main__': main()
