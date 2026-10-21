#!/usr/bin/env python3
"""Original CCD loops with a failure at one selected synthetic write boundary.

No actual SMU response, device access or physical-core mapping is emulated.
The bounded, hash-verified function instructions come from the existing fixture.
"""
import argparse
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('oc_base',Path(__file__).with_name('legacy-amd-oc-frequency.py'))
oc=importlib.util.module_from_spec(spec);spec.loader.exec_module(oc)


class Machine(oc.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs)
        self.command_returns=[]

    def external(self,name):
        write=name in ('_Z10Wr_SMU_argjj','_Z10Wr_SMU_argjjj')
        if write and self.reg('rdi') & oc.pbo.U32 == 0x1020:
            position=len(self.command_returns)
            returned=self.inputs.get('failure_return',0xfe) if position==self.inputs.get('fail_at',-1) else 1
            self.calls.append(name)
            self.operations.append(dict(kind='command',value=self.reg('rsi') & oc.pbo.U32,
                bdf=self.reg('rdx') & oc.pbo.U32 if name.endswith('jjj') else None))
            self.command_returns.append(dict(index=position,synthetic_return=returned))
            self.ret(returned)
        else:
            super().external(name)

    def result(self,symbol):
        row=super().result(symbol)
        row['command_boundary_returns']=self.command_returns
        return row


def investigate():
    fixture=oc.load_fixture();rows=[]
    for symbol in (oc.CCD,oc.CCD_FAST):
        for ccd in range(16):
            for failure in range(8):
                for returned in (0,0xfe,0xffffffff):
                    rows.append(Machine(fixture,dict(target=ccd,fail_at=failure,failure_return=returned)).result(symbol))
    return dict(schema=1,source_elf_sha256=oc.pbo.SOURCE_SHA,fixture_sha256=oc.FIXTURE_SHA,hardware_execution=False,
        scope='Original per-CCD wrapper and helper instructions. One Wr_SMU_arg command-write boundary per run returns a selected synthetic value; all other command writes return one. This is not an emulated firmware response or proof of a real I/O failure.',
        traces=rows,total=len(rows),
        observations=['Both normal and fast original loops submit eight core arguments regardless of these synthetic return values.',
                      'No core-existence check or acknowledgement is established by the loop. Fast writes only argument zero; normal initializes six arguments.',
                      'New GUI/CLI uses an explicit selection, fresh capability/identity checks before each core and stops on the first error. Physical mapping and measured frequency remain unverified.'])


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    report=investigate();args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Recorded',report['total'],'bounded original CCD traces')
