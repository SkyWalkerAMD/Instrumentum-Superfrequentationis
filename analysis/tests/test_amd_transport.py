"""Characterize legacy error propagation, not successful hardware commands."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec=importlib.util.spec_from_file_location('amd_transport',Path(__file__).resolve().parents[1]/'tools/legacy-amd-transport.py')
amd=importlib.util.module_from_spec(spec);spec.loader.exec_module(amd)


class AmdTransportTest(unittest.TestCase):
    def test_permanent_busy_still_enters_command_and_releases(self):
        m=amd.TransportMachine(amd.load_fixture(),{'busy_reads':100,'busy_value':255})
        self.assertEqual(m.run(amd.CMD)['outcome'],'returned')
        self.assertEqual(m.port_reads,11)
        self.assertEqual(sum(x.get('sleep_us')==2000 for x in m.trace),10)
        self.assertEqual([x['value'] for x in m.trace if 'port_write' in x],[1,0])
        self.assertEqual(m.pci_reads,2)
        self.assertEqual(len(m.pci),20)

    def test_backend_read_failure_flows_through_original_pci_sentinel(self):
        m=amd.TransportMachine(amd.load_fixture(),{'mode':'read-error'})
        self.assertEqual(m.run(amd.CMD)['outcome'],'returned')
        self.assertEqual(m.reg('rax') & amd.U32,amd.U32)
        self.assertEqual(struct.unpack('<I',m.uc.mem_read(m.output,4))[0],amd.U32)
        reads=[x for x in m.pci if x['op']=='read']
        self.assertEqual([x['backend_result'] for x in reads],[0,0])
        self.assertTrue(all(x['value'] is None for x in reads))

    def test_failed_writes_and_zero_status_do_not_stop_or_retry(self):
        m=amd.TransportMachine(amd.load_fixture(),{'mode':'write-error','status':0,'reply':0x12345678})
        self.assertEqual(m.run(amd.CMD)['outcome'],'returned')
        self.assertEqual(m.reg('rax'),0)
        self.assertEqual(struct.unpack('<I',m.uc.mem_read(m.output,4))[0],0x12345678)
        self.assertEqual(m.pci_reads,2)
        self.assertEqual([x['backend_result'] for x in m.pci if x['op']=='write'],[0]*18)


if __name__=='__main__':unittest.main()
