"""Pin original frequency arithmetic without running the original process."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('pstate_frequency', Path(__file__).resolve().parents[1] / 'tools/legacy-amd-pstate-frequency.py')
frequency = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frequency)


class PstateFrequency(unittest.TestCase):
    def test_original_branch_encoding_sweep(self):
        report = frequency.investigate(frequency.load_fixture())
        self.assertEqual(report['cases'], 16384)
        self.assertEqual(report['divider9_example']['original_mhz'], 3300)
        self.assertAlmostEqual(report['divider9_example']['documented_mhz'], 3666.6666666666665)
        self.assertFalse(report['hardware_execution'])
