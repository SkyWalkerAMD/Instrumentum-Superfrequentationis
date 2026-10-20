import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'pm_arrays', Path(__file__).resolve().parents[1] / 'tools/legacy-amd-pm-arrays.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class OriginalArrayMonitor(unittest.TestCase):
    def test_original_constructor_and_storage(self):
        report = module.research()
        self.assertEqual(report['case_count'], 128)
        self.assertEqual(report['field_count'], 92)
        self.assertFalse(report['hardware_execution'])
        self.assertFalse(report['units_verified'])
        self.assertFalse(report['targets_verified'])
        for case in report['cases']:
            fields = case['array_fields']
            self.assertEqual([int(f['offset'], 16) for f in fields], list(range(0x294, 0x404, 4)))
            self.assertEqual([f['slot'] for f in fields[:36]], list(range(12))*3)
            self.assertEqual([f['slot'] for f in fields[36:84]], list(range(24))*2)
            self.assertEqual([f['slot'] for f in fields[84:]], [0]*4+list(range(4)))
            self.assertEqual(fields[0]['name'], 'GMI_FULL_WIDTH')
            self.assertEqual(fields[36]['name'], 'DIMM_POWER')
            self.assertEqual(fields[60]['name'], 'DIMM_TEMP')
            self.assertEqual(fields[-1]['name'], 'PCIE_IOHC_BANDWIDTH[4]')


if __name__ == '__main__':
    unittest.main()
