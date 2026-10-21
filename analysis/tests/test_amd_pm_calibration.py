"""Calibrating the original PM reader does not resolve its layout conflicts."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'pm_calibration', Path(__file__).resolve().parents[1] / 'tools/legacy-amd-pm-calibration.py')
calibration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(calibration)


class OriginalPmCalibration(unittest.TestCase):
    def test_calibration_bound_and_unchanged_core_ccd_ranges(self):
        report = calibration.investigate()
        self.assertEqual(len(report['cases']), 12)
        for row in report['cases']:
            with self.subTest(version=row['version'], case=row['case']):
                case = row['case']
                if case == 'invalid_first':
                    self.assertTrue(row['observation_bound_reached'])
                    self.assertFalse(row['reached_dispatch'])
                    self.assertEqual(row['done'], 0)
                    self.assertEqual(len(row['reads']), 128)
                    continue
                self.assertFalse(row['observation_bound_reached'])
                self.assertTrue(row['reached_dispatch'])
                self.assertEqual(row['done'], 1)
                self.assertEqual(len(row['reads']), {
                    'valid': 2, 'forward_one': 4, 'zero_second': 122,
                    'high_second': 122, 'nan_first': 2,
                }[case])
                if case == 'forward_one':
                    self.assertEqual(row['final_offset'] - row['initial_offset'], 4)
                    self.assertEqual(row['adjustment'], 8)
                self.assertTrue(row['core_ccd_layout_unchanged'])
                if row['version'] == '0x730204':
                    self.assertEqual(row['outside_words'], 22)
                else:
                    self.assertEqual(row['overlapping_words'], 176)
