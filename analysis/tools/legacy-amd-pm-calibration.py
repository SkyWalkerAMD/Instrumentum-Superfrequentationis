#!/usr/bin/env python3
"""Bounded original calibration experiments; never scan physical memory."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
from unicorn import UC_HOOK_MEM_WRITE

spec=importlib.util.spec_from_file_location('calibration_percore',Path(__file__).with_name('legacy-amd-pm-percore.py'))
percore=importlib.util.module_from_spec(spec);spec.loader.exec_module(percore)
base=percore.limits
CASES=('valid','forward_one','invalid_first','zero_second','high_second','nan_first')


class Machine(percore.Machine):
    def __init__(self,version,ccds,case):
        super().__init__(base.load_fixture(),version,ccds,0)
        self.case=case;self.calibration_reads=[];self.calibration_writes=[];self.reached_dispatch=False;self.bound=False
        self.uc.hook_add(UC_HOOK_MEM_WRITE,self.write_hook)

    def write_hook(self,uc,access,address,size,value,context):
        if self.phase=='calibration' and base.OBJ<=address<base.OBJ+0x1000:
            self.calibration_writes.append(dict(member=hex(address-base.OBJ),size=size,value=value))

    def hook(self,uc,address,size,context):
        if self.phase=='calibration':
            if address==0x2e73a2:
                self.reached_dispatch=True;uc.emu_stop();return
            if address==0x230ea0:
                # Stop before the 129th synthetic read: enough to demonstrate
                # that the 30-iteration counter is not a global scan bound.
                if len(self.calibration_reads)==128:
                    self.bound=True;uc.emu_stop();return
                n=len(self.calibration_reads);first=n%2==0
                a,b=0x40a00000,0x40900000 # 5.0 and 4.5, independent binary32 words
                if self.case=='forward_one' and n<2:a=0x3f800000
                if self.case=='invalid_first':a=0x3f800000
                if self.case=='zero_second':b=0
                if self.case=='high_second':b=0x41000000
                if self.case=='nan_first':a=0x7fc12345
                value=a if first else b
                self.calibration_reads.append(dict(offset=self.reg('rsi'),raw=value))
                self.ret(value);return
        super().hook(uc,address,size,context)

    def run(self):
        if self.phase=='calibrated':return
        super().run()

    def experiment(self):
        self.run() # Original constructor/common monitor, with the old synthetic calibration-complete precondition.
        self.phase='calibration';self.steps=0
        self.uc.mem_write(base.OBJ+0x7a,b'\1');self.uc.mem_write(base.OBJ+0xcdc,b'\0')
        initial=bytes(self.uc.mem_read(base.OBJ,0x1000))
        self.reg('rsp',base.STACK+0xfff8);self.p64(base.STACK+0xfff8,base.STOP)
        self.reg('rdi',base.OBJ);self.reg('rsi',4);self.uc.emu_start(0x2e7360,base.STOP+1)
        after=bytes(self.uc.mem_read(base.OBJ,0x1000))
        row=dict(version=hex(self.version),ccds=self.ccds,case=self.case,reached_dispatch=self.reached_dispatch,
                 observation_bound_reached=self.bound,reads=self.calibration_reads,writes=self.calibration_writes,
                 initial_offset=struct.unpack_from('<I',initial,0xcd8)[0],final_offset=struct.unpack_from('<I',after,0xcd8)[0],
                 done=after[0xcdc],adjustment=struct.unpack_from('<I',after,0xce0)[0])
        assert all(w['member'] in ('0xcd8','0xcdc','0xce0') for w in self.calibration_writes)
        if self.reached_dispatch:
            self.phase='calibrated';actual=self.trace();expected=percore.Machine(base.load_fixture(),self.version,self.ccds,0).trace()
            # Both reads use the same offset-related synthetic data. This
            # compares layouts, not valid hardware measurements or units.
            assert actual['groups']==expected['groups']
            row.update(core_ccd_layout_unchanged=True,overlapping_words=len(actual['core_ccd_overlap']),
                outside_words=sum(len(g['outside_public_table']) for g in actual['groups']))
        return row


def investigate():
    rows=[Machine(v,c,case).experiment() for v,c in ((0x730204,2),(0x731004,16)) for case in CASES]
    return dict(source_elf_sha256=base.SOURCE_SHA,fixture_sha256=base.FIXTURE_SHA,hardware_execution=False,
        scope='Original constructor then a second call with explicit calibration flag reset and synthetic alternating MMIO values. Observe at most 128 calibration reads; completed cases compare original mode-4 layout with the earlier calibrated-state fixture. Not hardware topology or a valid firmware layout.',cases=rows)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    report=investigate();a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps([dict(version=r['version'],case=r['case'],reads=len(r['reads']),done=r['done'],bound=r['observation_bound_reached']) for r in report['cases']]))
