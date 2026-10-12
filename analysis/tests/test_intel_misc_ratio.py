"""Pin original miscellaneous ratio bit behavior independently of production."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('misc_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-misc-ratio.py')
misc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(misc)


class IntelMiscRatio(unittest.TestCase):
    def test_original_setters_and_failures(self):
        report = misc.investigate(misc.load_fixture())
        self.assertEqual(len(report['scenarios']), 156)
        self.assertEqual(sum(row['outcome'] == 'instruction-limit' for row in report['scenarios']), 2)

    def test_every_neighboring_control_combination(self):
        fixture = misc.load_fixture()
        for symbol, mask in misc.FUNCTIONS.items():
            for bits in range(32):
                for argument in (0, 1):
                    raw = 0xf1234500 | bits
                    machine = misc.tvb.TvbMachine(fixture, dict(data=raw, argument=argument))
                    self.assertEqual(machine.run(symbol)['outcome'], 'returned')
                    self.assertEqual(machine.writes[-1], dict(low=(raw & ~mask) | (mask if argument else 0), high=0x80000019))
