import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('pm_package',
    Path(__file__).resolve().parents[1] / 'tools/legacy-amd-pm-package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PmPackage(unittest.TestCase):
    def test_original_vectors_all_versions_counts_and_float_patterns(self):
        report = package.research()
        self.assertEqual(report['case_count'], 128)
        self.assertFalse(report['hardware_execution'])
        self.assertFalse(report['units_verified'])
        self.assertFalse(report['targets_verified'])
        for case in report['cases']:
            self.assertEqual(len(case['package_fields']), 53)
            self.assertEqual(len({f['offset'] for f in case['package_fields']}), 53)

    def test_original_groups_are_disjoint_from_existing_public_and_limit_fields(self):
        offsets = {o + 4*i for _, _, o, labels in package.GROUPS for i in range(len(labels))}
        self.assertEqual(len(offsets), 53)
        self.assertTrue(offsets.isdisjoint(package.limits.OFFSETS))
        self.assertLess(max(offsets) + 4, 0x178)
        self.assertEqual([len(n) for _, _, _, n in package.GROUPS], [7, 8, 4, 10, 24])
