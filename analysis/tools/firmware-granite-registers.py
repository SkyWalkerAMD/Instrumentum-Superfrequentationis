#!/usr/bin/env python3
"""Replay register packet helpers against scripted MMIO, never real hardware."""
import argparse
from collections import deque
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct

Slice=runpy.run_path(str(Path(__file__).with_name('firmware-granite-semantics.py')))['Slice']
SHA='5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596'


class RegisterSlice(Slice):
    def __init__(self,body,code,channel,register,value,delay,write=False,stall=None):
        super().__init__(body,code)
        u=lambda a:struct.unpack_from('<I',body,a)[0]
        # Deliberately include high bits in the synthetic base to exercise the
        # actual address mask. This is not the board's runtime configuration.
        base=0x100000+channel*0x40000; address=channel+1
        self.r.update(a1=0x50000,a2=channel,a3=register,a4=value)
        self.put(0xc598+12*channel+0x14,base)
        self.put(0xc598+12*channel+0x1c,address,1)
        port=lambda literal:((base+u(literal)) & u(0x11430))+u(0x11438)+0x200
        select,ready,packet,response,control=[port(a) for a in (0x126dc,0x126e0,0x126e4,0x126e8,0x126ec)]
        if write:
            gates=[(select,1<<15),(ready,1),(response,1<<24)]
            self.expected_writes=[(select,address),(packet,address | register<<6 | value<<14 | 0x1000000)]
        else:
            gates=[(select,1<<15),(ready,1),(response,1<<25),(select,1<<16),
                   (control,0x600),(ready,1),(control,0x200),(response,0xc00000ff | value<<8)]
            self.expected_writes=[(select,address),(packet,address | register<<6 | 0x2000000),
                                  (control,8),(packet,address)]
        self.ports={select,ready,packet,response,control}; self.writes=[]
        self.responses=deque()
        for index,(port,bits) in enumerate(gates):
            if index==stall:
                self.responses.append((port,None)); break
            self.responses.extend([(port,0)]*delay+[(port,bits)])

    def get(self,address,size=4):
        if address in self.ports:
            assert size==4 and self.responses and self.responses[0][0]==address
            value=self.responses[0][1]
            if value is None: return 0
            self.responses.popleft(); return value
        return super().get(address,size)

    def put(self,address,value,size=4):
        if hasattr(self,'ports') and address in self.ports:
            assert size==4
            self.writes.append((address,value))
        else: super().put(address,value,size)


class VoltageSlice(RegisterSlice):
    """Direct VID packet helper with scripted ready/status bits only."""
    def __init__(self,body,code,channel,vid,delay,alternate=False,stall=None):
        Slice.__init__(self,body,code)
        u=lambda a:struct.unpack_from('<I',body,a)[0]
        base=0x100000+channel*0x40000
        address=63-channel if alternate else channel+1
        self.r.update(a1=0x50000,a2=channel,a3=vid)
        self.put(0xc598+12*channel+0x14,base)
        self.put(0xc598+12*channel+0x1c,address,1)
        port=lambda literal:((base+u(literal))&u(0x11430))+u(0x11438)+0x200
        ready,packet,select=[port(p) for p in (0x126e0,0x126f0,0x126dc)]
        self.ports={ready,packet,select};self.writes=[];self.responses=deque()
        self.expected_writes=[(packet,address | vid<<6 | 0x8000),(select,address)]
        for index,(port,bits) in enumerate(((ready,1),(select,1<<15))):
            if index==stall:
                self.responses.append((port,None));break
            self.responses.extend([(port,0)]*delay+[(port,bits)])


def voltage_packets(body,code):
    assert struct.unpack_from('<I',body,0x1198c)[0]==0x8000
    assert struct.unpack_from('<I',body,0x126f0)[0]==0x73010
    cases=stalls=0;trace=hashlib.sha256()
    for channel,vid,delay,alternate in itertools.product(range(3),range(512),(0,1,3),(False,True)):
        vm=VoltageSlice(body,code,channel,vid,delay,alternate)
        vm.run(0x32d6c,0x32dcb)
        assert not vm.responses and vm.writes==vm.expected_writes
        for address,value in vm.writes:trace.update(struct.pack('<2I',address,value))
        cases+=1
    for gate in range(2):
        vm=VoltageSlice(body,code,1,0x101,0,stall=gate)
        try:vm.run(0x32d6c,0x32dcb,budget=512)
        except ValueError as error:assert str(error)=='Bounded instruction budget exceeded'
        else:raise AssertionError('A stalled voltage helper unexpectedly completed')
        assert vm.responses[0][1] is None
        assert vm.writes==([] if gate==0 else vm.expected_writes)
        stalls+=1
    return dict(helper='0x32d6c',cases=cases,stalled_gates=stalls,vid_codes=512,
        packet='configured address | VID code << 6 | 0x8000',packet_register_offset='0x73010',
        write_order=['packet','configured address to select register'],completion_bit=15,
        readback_payload_returned=False,physical_channel_identity_verified=False,
        independent_second_rail_verified=False,peripheral_effects_verified=False,
        runtime_write_enabled=False,trace_sha256=trace.hexdigest())


def main():
    p=argparse.ArgumentParser()
    for name in ('smu','study','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();body=a.smu.read_bytes();assert hashlib.sha256(body).hexdigest()==SHA
    study=json.loads(a.study.read_text());assert study['source_sha256']==SHA
    code={int(i['address'],16):i for f in study['functions'] for i in f['instructions']}
    counts={False:0,True:0};stalls=0
    for write in (False,True):
        start,stop=(0x32cbc,0x32d34) if write else (0x32bf8,0x32cb9)
        for channel,register,delay,variant in itertools.product(range(3),range(256),(0,1,3),(0,128,255)):
            value=variant if write else (register*73+variant)&255
            vm=RegisterSlice(body,code,channel,register,value,delay,write)
            vm.run(start,stop)
            assert not vm.responses and vm.writes==vm.expected_writes
            if not write:assert vm.r['a2']==value
            counts[write]+=1
        for gate in range(3 if write else 8):
            vm=RegisterSlice(body,code,1,0x8b,0xa5,0,write,gate)
            try: vm.run(start,stop,budget=512)
            except ValueError as error:assert str(error)=='Bounded instruction budget exceeded'
            else:raise AssertionError('A stalled ready bit unexpectedly completed')
            assert list(vm.responses)[0][1] is None
            stalls+=1
    report=dict(schema=2,source_sha256=SHA,hardware_execution=False,
        read_cases=counts[False],write_cases=counts[True],stalled_gates=stalls,
        firmware_channels=[0,1,2],controller_addresses_fixture=[1,2,3],register_bytes=256,
        waits_before_each_ready_reply=[0,1,3],
        read_handler='0x33054 -> 0x32bf8',write_handler='0x33084 -> 0x32cbc',
        read_packet='address | register << 6 | 0x02000000',
        write_packet='address | register << 6 | data << 14 | 0x01000000',
        read_result_bits='15:8',write_completion_bit=24,read_completion_bit=30,
        direct_voltage_packets=voltage_packets(body,code),
        physical_rail_mapping_verified=False,device_register_contract_verified=False,
        peripheral_effects_verified=False,timing_verified=False,runtime_write_enabled=False,
        limits='Scripted ready bits prove marshaling and loops only; a stalled bit exhausts the research budget, not a firmware timeout. Completion is not a verified voltage change or independent setting readback.')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print('Verified',counts,'scripted register helper cases and',stalls,'stalled gates')


if __name__=='__main__':main()
