"""Legacy PL4 width and failure behavior from original machine instructions."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('pl4_test',Path(__file__).resolve().parents[1]/'tools/legacy-intel-pl4.py')
pl4=importlib.util.module_from_spec(spec); spec.loader.exec_module(pl4)


class IntelPl4(unittest.TestCase):
    def test_original_entries(self):
        self.assertEqual(len(pl4.investigate(pl4.load_fixture())['scenarios']),612)

    def test_original_negative_input_saturates_and_preserves_lock(self):
        m=pl4.Pl4Machine(pl4.load_fixture(),dict(raw=1<<31,input=-1))
        self.assertEqual(m.run(pl4.SET)['outcome'],'returned')
        self.assertEqual(m.writes,[(1<<31)|0x3fff])
