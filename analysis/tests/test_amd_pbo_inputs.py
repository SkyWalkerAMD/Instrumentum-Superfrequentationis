"""Original fuse getter is neither a current-limit query nor fault-aware."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('pbo_inputs',Path(__file__).resolve().parents[1]/'tools/legacy-amd-pbo-inputs.py')
pbo=importlib.util.module_from_spec(spec);spec.loader.exec_module(pbo)

class FmaxInputEvidence(unittest.TestCase):
    def test_complete_fuse_field_and_unrelated_bits(self):
        fixture=pbo.load_fixture()
        for value in range(256):
            for unrelated in (0,0xf807ffff):
                row=pbo.Machine(fixture,(value<<19)|unrelated).result()
                self.assertEqual(row['returned_mhz'],value*25)
                self.assertEqual(row['delays'],[100000])
                self.assertEqual([r['offset'] for r in row['pci']],[0xf8,0xfc])
                self.assertTrue(all(r['index']==0x5d584 for r in row['pci']))

    def test_failed_access_is_not_current_fmax_confirmation(self):
        fixture=pbo.load_fixture()
        write=pbo.Machine(fixture,200<<19,0).result()
        read=pbo.Machine(fixture,200<<19,1).result()
        self.assertEqual(write['returned_mhz'],5000)
        self.assertEqual(read['returned_mhz'],6375)
        self.assertEqual(len(write['pci']),2)
        self.assertTrue(read['pci'][1]['injected_failure'])
