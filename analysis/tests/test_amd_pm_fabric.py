import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'pm_fabric', Path(__file__).resolve().parents[1] / 'tools/legacy-amd-pm-fabric.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class OriginalFabricMonitor(unittest.TestCase):
    def test_original_constructor_and_offsets(self):
        report = module.research()
        self.assertEqual(report['case_count'], 128)
        self.assertEqual(report['field_count'], 27)
        self.assertFalse(report['units_verified'])
        self.assertFalse(report['targets_verified'])
        self.assertFalse(report['hardware_execution'])
        for case in report['cases']:
            fields = case['fabric_fields']
            self.assertEqual(fields[0]['name'], 'FCLK_PCIE_UP_THRESHOLD')
            self.assertEqual(fields[8]['offset'], '0x174')
            self.assertEqual(fields[9]['name'], 'SMNCLK')
            self.assertEqual(fields[-1]['offset'], '0x290')
            self.assertEqual(len({f['offset'] for f in fields}), 27)


if __name__ == '__main__':
    unittest.main()
