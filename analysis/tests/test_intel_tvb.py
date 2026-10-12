"""Check recovered TVB bits against original executable instructions."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('tvb_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-tvb.py')
tvb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tvb)


class IntelTvb(unittest.TestCase):
    def test_original_bits_and_failure_behavior(self):
        report = tvb.investigate(tvb.load_fixture())
        self.assertEqual(len(report['scenarios']), 47)
        self.assertEqual(sum(row['outcome'] == 'instruction-limit' for row in report['scenarios']), 3)

    def test_every_misc_disable_combination(self):
        fixture = tvb.load_fixture()
        for raw in range(32):
            machine = tvb.TvbMachine(fixture, dict(data=raw | 0xf1234500))
            self.assertEqual(machine.run(tvb.READ)['outcome'], 'returned')
            self.assertEqual(machine.bits, [bool(raw & (1 << bit)) for bit in (1, 2, 3, 4)])
