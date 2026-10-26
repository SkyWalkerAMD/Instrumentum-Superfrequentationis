#!/usr/bin/env python3
"""Characterize original VF consumers and SoC helpers using synthetic I/O only."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
from unicorn import UcError, UC_ERR_EXCEPTION
import importlib.util

spec = importlib.util.spec_from_file_location('vf_pbo', Path(__file__).with_name('legacy-amd-pbo-transactions.py'))
pbo = importlib.util.module_from_spec(spec); spec.loader.exec_module(pbo)
VF = '_ZN5amdvf16get_amd_freq_vidEPmm'
LN2 = '_Z6ln2_env'
FORCE = '_Z24TEST_SMC_MSG_ForceSocVidjRj'
UNFORCE = '_Z26TEST_SMC_MSG_UnForceSocVidRj'
STREAM = '_ZN10AMD_PM_LOG11stream_vftfERSt6vectorIiSaIiEES3_S3_S3_'
FUNCTIONS = (VF, LN2, FORCE, UNFORCE, STREAM)
FIXTURE = pbo.ROOT / 'analysis/fixtures/legacy-amd-vf-sources.json'
FIXTURE_SHA = 'f00265cf5a2d2bcd78fa33dd77f0421b95db818c56702145e3fd664eba8778fe'


def extract(path):
    pbo.FUNCTIONS = FUNCTIONS
    fixture = pbo.extract(path)
    raw = path.read_bytes(); elf = ELFFile(io.BytesIO(raw))
    def read(address, size):
        for seg in elf.iter_segments():
            offset = address - seg['p_vaddr']
            if seg['p_type'] == 'PT_LOAD' and 0 <= offset and offset+size <= seg['p_memsz']:
                available = min(size, max(0, seg['p_filesz']-offset))
                start = seg['p_offset']+offset
                return raw[start:start+available]+bytes(size-available)
        raise ValueError('Unmapped original constant')
    dec = Cs(CS_ARCH_X86, CS_MODE_64); dec.detail = True
    constants = {}
    for fn in fixture['functions']:
        for ins in dec.disasm(bytes.fromhex(fn['code_hex']), fn['address']):
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    address = ins.address+ins.size+op.mem.disp
                    constants[address] = read(address, 128).hex()
    fixture['memory_data'] = [dict(address=a, data_hex=v) for a,v in sorted(constants.items())]
    return fixture


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('VF source fixture identity mismatch')
    return json.loads(raw)


class Machine(pbo.Machine):
    def __init__(self, fixture, inputs):
        inputs = dict(inputs)
        inputs.setdefault('argument_address',0x3b10a88 if inputs.get('device')==0x1630 else 0x3b10a40)
        super().__init__(fixture, inputs)
        for item in fixture['memory_data']:
            raw = bytes.fromhex(item['data_hex']); self.map(item['address'],len(raw)); self.uc.mem_write(item['address'],raw)
        for name in self.objects:
            self.uc.mem_write(self.objects[name],bytes([bool(inputs.get(name,name=='GLOBAL_IS_SHIMADA'))]))
        self.this = self.alloc(0xd00); self.vf_output = self.alloc(16); self.stream_outputs = []

    def prepare(self, symbol):
        self.active_symbol = symbol
        if symbol == VF:
            self.reg('rdi',self.this); self.reg('rsi',self.vf_output); self.reg('rdx',self.inputs.get('affinity',1))
            self.uc.mem_write(self.vf_output,bytes.fromhex('aabbccdd')*4)
            # Explicit synthetic pre-existing local stack word for failed
            # RdmsrTx calls that do not initialize their output pointers.
            self.put32(pbo.base.STACK+0xffe0,self.inputs.get('stack_low',0))
        elif symbol == FORCE:
            self.reg('rdi',self.inputs.get('vid',0x12345678)); self.reg('rsi',self.output_word)
        elif symbol == UNFORCE: self.reg('rdi',self.output_word)
        elif symbol == STREAM:
            self.reg('rdi',self.this)
            labels = self.inputs.get('labels',['Voltage','Temp','Freq Ghz','Eff Freq'])
            label_data = self.alloc(32*len(labels))
            for n,label in enumerate(labels): self.cpp_string(label_data+32*n,label)
            self.uc.mem_write(self.this+0x910,struct.pack('<3Q',label_data,label_data+32*len(labels),label_data+32*len(labels)))
            rows = self.inputs.get('rows',[[1.0,1.125,1.25],[50.5,51.5,52.5],[4.0,0.0,4.5],[3.5,0.0,4.0]])
            words = [[struct.unpack('<I',struct.pack('<f',v))[0] for v in row] for row in rows]
            self.vector(self.this+0x928,words); self.put32(self.this+0x6c,self.inputs.get('cores',3))
            for n,reg in enumerate(('rsi','rdx','rcx','r8')):
                vector = self.alloc(24); data = self.alloc(512)
                self.uc.mem_write(vector,struct.pack('<3Q',data,data,data+512)); self.reg(reg,vector); self.stream_outputs.append(vector)

    def external(self, name):
        if name == '_Z7RdmsrTxjPjS_m':
            self.calls.append(name); assert self.reg('rdi') == 0xc0010293
            self.events.append(dict(msr=self.reg('rdi'),affinity=self.reg('rcx'),read_result=self.inputs.get('read_result',1)))
            if self.inputs.get('read_result',1):
                self.put32(self.reg('rsi'),self.inputs.get('low',0)); self.put32(self.reg('rdx'),self.inputs.get('high',0))
            self.ret(self.inputs.get('read_result',1))
        elif name == '_ZN2EC8Rd_ECRAMEhh':
            self.calls.append(name); self.events.append(dict(ec_offset=self.reg('rsi'),bank=self.reg('rdx')))
            assert (self.reg('rsi'),self.reg('rdx')) == (0x4c,2)
            self.ret(self.inputs.get('ec_byte',0))
        elif name == '_Z13find_pci_dev2jjj':
            self.calls.append(name); self.searches.append([self.reg('rdi'),self.reg('rsi'),self.reg('rdx')]); self.ret(0)
        elif name == '_ZN10AMD_PM_LOG15refresh_monitorEj' or name == '_ZN10AMD_PM_LOG6get_pmEi':
            self.calls.append(name); self.events.append(dict(synthetic_pm_boundary=name,mode=self.reg('rsi')))
            assert self.reg('rdi') == self.this
            self.ret(self.inputs.get('pm_return',0))
        elif name == '_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7compareEPKc@plt':
            self.calls.append(name)
            text = bytes(self.uc.mem_read(self.u64(self.reg('rdi')),self.u64(self.reg('rdi')+8)))
            needle = bytes(self.uc.mem_read(self.reg('rsi'),64)).split(b'\0',1)[0]
            self.searches.append(needle.decode()); self.ret(0 if text == needle else 1)
        elif name == '_ZSt24__throw_out_of_range_fmtPKcz@plt':
            self.calls.append(name); self.stop('range-error-boundary')
        else: super().external(name)

    def observe(self, symbol):
        self.put32(self.output_word,0xaabbccdd)
        try: out = self.run(symbol)
        except UcError as error:
            if error.errno != UC_ERR_EXCEPTION or self.reg('rip') != 0x6ece96: raise
            out = dict(outcome='integer-divide-zero',steps=self.steps)
        result = dict(symbol=symbol,inputs=self.inputs,outcome=out['outcome'],steps=out['steps'],calls=self.calls,
            searches=self.searches,events=self.events,pci=self.pci,delays=self.delays,returned=self.reg('rax')&pbo.U32,
            vf_words=list(struct.unpack('<2Q',self.uc.mem_read(self.vf_output,16))),
            output_word=struct.unpack('<I',self.uc.mem_read(self.output_word,4))[0])
        if symbol == STREAM:
            result['label_indices'] = list(struct.unpack('<4I',self.uc.mem_read(self.this+0xca0,16)))
            result['vectors'] = []
            for vector in self.stream_outputs:
                start,end = self.u64(vector),self.u64(vector+8)
                result['vectors'].append(list(struct.unpack('<%di'%((end-start)//4),self.uc.mem_read(start,end-start))))
        return result


def investigate(fixture):
    vf = []
    for fid in (0,1,127,255):
        for did in range(64): vf.append(Machine(fixture,dict(low=fid|(did<<8)|(100<<14))).observe(VF))
    for vid in range(256): vf.append(Machine(fixture,dict(low=100|(4<<8)|(vid<<14),high=0xffffffff)).observe(VF))
    for affinity in (0,1,1<<63,(1<<64)-1):
        for low in (0,100|(4<<8)|(17<<14)):
            vf.append(Machine(fixture,dict(affinity=affinity,read_result=0,stack_low=low)).observe(VF))
    ec = [Machine(fixture,dict(ec_byte=n)).observe(LN2) for n in range(256)]
    soc = []; faults = []
    for symbol in (FORCE,UNFORCE):
        for device,shimada in ((0x153a,1),(0x1480,0),(0x14d8,0),(0x1630,0)):
            for response in (0,1,0xfd,0xffffffff):
                soc.append(Machine(fixture,dict(device=device,GLOBAL_IS_SHIMADA=shimada,response=response,returned=0x89abcdef)).observe(symbol))
        baseline = Machine(fixture,{}).observe(symbol)
        for at in range(len(baseline['pci'])):
            faults.append(Machine(fixture,dict(fail_at=at)).observe(symbol))
    stream = [Machine(fixture,inputs).observe(STREAM) for inputs in (
        {},dict(pm_return=0xffffffff),dict(cores=0),dict(cores=4),
        dict(rows=[[1.0,1.125,1.25],[50.5,51.5,52.5],[4.0,4.25,4.5],[3.5,3.75,4.0]]),
        dict(labels=['Voltage','Temp','MissingFreq','Eff Freq']))]
    return dict(schema=1,source_elf_sha256=pbo.SOURCE_SHA,fixture_sha256=FIXTURE_SHA,hardware_execution=False,
        scope='Complete original instructions. Synthetic PCI/MSR/EC, PM refresh/get_pm boundaries, labels and float vectors. No firmware execution, actual PM layout, voltage or physical mapping is established.',
        vf=vf,ec=ec,soc=soc,faults=faults,stream=stream)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--extract',type=Path); parser.add_argument('--output',type=Path,required=True); args=parser.parse_args()
    if args.extract:
        raw=(json.dumps(extract(args.extract),indent=2)+'\n').encode(); FIXTURE.write_bytes(raw)
        print('fixture_sha256',hashlib.sha256(raw).hexdigest())
    else:
        report=investigate(load_fixture()); args.output.write_bytes((json.dumps(report,indent=2)+'\n').encode())
        print(json.dumps({k:len(report[k]) for k in ('vf','ec','soc','faults','stream')}))
