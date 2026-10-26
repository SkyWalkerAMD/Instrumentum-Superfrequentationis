"""Keep legacy CO writes distinct from unverified Force/UnForce side effects."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('curve_all',Path(__file__).resolve().parents[1]/'tools/legacy-amd-curve-all.py')
curve=importlib.util.module_from_spec(spec); spec.loader.exec_module(curve)


class OriginalAllCoreCurve(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.fixture=curve.load_fixture()

    def test_original_pbo_setter_uses_full_raw_word_and_f8_port(self):
        for margin in (-2147483648,-65536,-51,-50,-25,-1,0,1,50,51,65536,2147483647):
            r=curve.Machine(self.fixture,dict(margin=margin)).observe(curve.ALL)
            writes=[p for p in r['pci'] if p['write'] and p['offset']==0xfc]
            self.assertEqual([(p['index'],p['value']) for p in writes],
                [(0x3b10a40,margin&0xffffffff),(0x3b10924,0xa3),(0x3b10a40,margin&0xffffffff),
                 (0x3b10924,7),(0x3b10a40,margin&0xffffffff)])
            self.assertFalse(r['pci'][-1]['locked'])
            self.assertFalse(r['pci'][-1]['write'])

    def test_release_helpers_do_not_prove_curve_or_voltage_restoration(self):
        for symbol,message in ((curve.FORCE,0xe),(curve.UNFORCE,0xf),(curve.UNVID,0x13)):
            for response in (0,1,0xfd,0xffffffff):
                r=curve.Machine(self.fixture,dict(response=response,returned=0x12345678)).observe(symbol)
                commands=[p['value'] for p in r['pci'] if p['write'] and p['offset']==0xbc and p['index']==0x3b10924]
                self.assertEqual(commands,[0xfd,message])
                self.assertEqual(r['returned'],response)
                self.assertEqual(r['output_word'],0x12345678)
                # Output is published before the response is read, even when
                # the synthetic response is busy or rejection. No 07/61/5E.
                reads=[p for p in r['pci'] if not p['write']]
                self.assertEqual([p['index'] for p in reads],[0x3b10a40,0x3b10970])
                self.assertTrue(any(p['write'] and p['index']==0x3b10970 and p['value']==0xfd for p in r['pci']))

    def test_each_injected_pci_failure_keeps_executing_original_tail(self):
        count=0
        for symbol in (curve.ALL,curve.FORCE,curve.UNFORCE,curve.UNVID):
            baseline=curve.Machine(self.fixture,{}).observe(symbol)
            for index in range(len(baseline['pci'])):
                r=curve.Machine(self.fixture,dict(fail_at=index,response=0xfd)).observe(symbol)
                self.assertEqual(len(r['pci']),len(baseline['pci']))
                self.assertTrue(r['pci'][index]['injected_failure'])
                count+=1
        self.assertEqual(count,60)
