"""Original predicate return domain and software profile update interactions."""
import importlib.util
from pathlib import Path
import unittest

TOOL=Path(__file__).resolve().parents[1]/'tools/legacy-amd-initialization.py'
spec=importlib.util.spec_from_file_location('amd_initialization',TOOL)
amd=importlib.util.module_from_spec(spec);spec.loader.exec_module(amd)


class AmdInitializationTest(unittest.TestCase):
    def test_family_cannot_return_later_compared_values(self):
        fixture=amd.load_fixture();values=set()
        for extended in range(256):
            m=amd.AmdMachine(fixture,{'eax':(extended<<20)|0xfffff})
            result=m.run(amd.FAMILY)
            self.assertEqual(result['outcome'],'returned')
            values.add(result['return_al'])
        self.assertEqual(values,{0,0xf,0x11,0x13})
        self.assertFalse(values & {0xb,0x1a})

    def test_shimada_then_gpt_preserves_omitted_assignments(self):
        m=amd.AmdMachine(amd.load_fixture(),{'eax':11<<20,'pci_devices':[0x153a,0x1122]})
        self.assertEqual(m.run(amd.STARTUP)['outcome'],'constructor-join')
        state=m.state()
        self.assertEqual(state['GLOBAL_IS_SHIMADA'],1);self.assertEqual(state['GLOBAL_IS_GPT'],1)
        self.assertEqual(state['SMU_IOPORT'],0x3b10978)
        self.assertEqual(state['BIOSSMC_MSG_SetOverclockFreqAllCores'],0x26)
        self.assertEqual([w['value'] for w in m.writes if w['object']=='SMU_IOPORT'],[0x3b1097c,0x3b10978])
        self.assertEqual([w['value'] for w in m.writes if w['object']=='BIOSSMC_MSG_SetOverclockFreqAllCores'],[0x26])

    def test_negative_pheonix_probe_does_not_clear_cached_flag(self):
        m=amd.AmdMachine(amd.load_fixture(),{'pci_devices':[],'initial_phx':1})
        self.assertEqual(m.run(amd.PHX)['return_al'],0)
        self.assertEqual(m.state()['GLOBAL_IS_PHX'],1)
        self.assertFalse(m.writes)


if __name__=='__main__':unittest.main()
