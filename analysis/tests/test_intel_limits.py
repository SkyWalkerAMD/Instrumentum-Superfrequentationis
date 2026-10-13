import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('limits_test',Path(__file__).resolve().parents[1]/'tools/legacy-intel-limits.py')
limits=importlib.util.module_from_spec(spec); spec.loader.exec_module(limits)


class IntelLimits(unittest.TestCase):
    def test_complete_original_helpers(self):
        self.assertEqual(len(limits.investigate(limits.load_fixture())['scenarios']),274)

    def test_upper_half_and_reserved_bit_exposure(self):
        fixture=limits.load_fixture()
        for symbol,(_,count) in limits.READERS.items():
            m=limits.LimitsMachine(fixture,dict(raw=0xffffffff00000000))
            self.assertEqual(m.run(symbol)['outcome'],'returned')
            self.assertFalse(any(m.result(count)))
            m=limits.LimitsMachine(fixture,dict(raw=1<<2))
            self.assertEqual(m.run(symbol)['outcome'],'returned')
            self.assertTrue(m.result(count)[2])  # Raw client reserved bit, not a decoded reason.
