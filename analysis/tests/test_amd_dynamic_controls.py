"""Pin recovered original behaviors without enabling memory or EC writes."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('dynamic_controls',ROOT/'analysis/tools/legacy-amd-dynamic-controls.py')
dynamic = importlib.util.module_from_spec(spec); spec.loader.exec_module(dynamic)


class DynamicControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.fixture = dynamic.load_fixture()

    def test_memory_failure_does_not_stop_original_transaction(self):
        m = dynamic.MemoryMachine(self.fixture,dict(enabled=1,device=0x153a,fail_at=0))
        m.result(dynamic.pbo.CTOR); r = m.result(dynamic.MEM)
        self.assertEqual(len(r['pci']),14)
        self.assertTrue(r['pci'][0]['injected_failure'])
        self.assertTrue(all(x['write'] for x in r['pci']))
        self.assertEqual([(x['index'],x['value']) for x in r['pci'] if x['offset']==0xbc][-4:],
                         [(0x3b10970,0),(0x3b10970,1),(0x3b10a40,1),(0x3b10924,0x92)])

    def run_edc(self, **changes):
        inputs=dict(row=2,mode=2,policy=1,enabled=1,initial_control=0xa4,values=[100,200,65536,400,500])
        inputs.update(changes)
        m = dynamic.EdcMachine(self.fixture,inputs)
        self.assertEqual(m.run(dynamic.EDC,instruction_limit=20000)['outcome'],'returned')
        return m

    def test_edc_truncates_and_sets_other_selector_banks_to_65000(self):
        m = self.run_edc()
        writes=[(x['address'],x['value']) for x in m.ec if x['write']]
        self.assertEqual([v for a,v in writes if a in (0xd0,0xd1)],[0,0])
        self.assertEqual([v for a,v in writes if a==0xc2],[2,1,0])
        self.assertEqual(writes[-11:-1],[(0xc2,1),(0xc6,0xfd),(0xc7,0xe8),(0xc8,0xfd),(0xc9,0xe8),
                                        (0xc2,0),(0xc6,0xfd),(0xc7,0xe8),(0xc8,0xfd),(0xc9,0xe8)])
        self.assertEqual(writes[-1],(0xc1,0xa5))

    def test_edc_reports_applied_before_stubbed_refresh_even_with_lost_write(self):
        m = self.run_edc(discard_at=3)
        self.assertTrue(m.ec[3]['discarded'])
        self.assertIn('Applied!',m.qt_texts)
        self.assertTrue(m.info_shown)
        self.assertEqual(m.refreshes,1)
        self.assertEqual(len([x for x in m.ec if not x['write']]),1)


if __name__ == '__main__': unittest.main()
