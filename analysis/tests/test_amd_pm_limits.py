"""Original monitor evidence is independent of the replacement decoder."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('pm_limits', Path(__file__).resolve().parents[1] / 'tools/legacy-amd-pm-limits.py')
limits = importlib.util.module_from_spec(spec)
spec.loader.exec_module(limits)


class PmLimits(unittest.TestCase):
    def test_original_monitor_versions_and_topology_inputs(self):
        report = limits.research(limits.load_fixture())
        self.assertEqual(report['case_count'],128)
        self.assertEqual(len(report['fields']),24)
        self.assertFalse(report['hardware_execution'])
        self.assertTrue(all(not f['unit_verified'] for f in report['fields']))
        self.assertEqual(report['fields'][0]['offset'],'0x0')
        self.assertEqual(report['fields'][16]['offset'],'0x1c8')
        self.assertEqual(report['fields'][-1]['offset'],'0x1e4')

    def test_fixture_function_identity_and_boundaries(self):
        fixture=limits.load_fixture()
        self.assertEqual([f['size'] for f in fixture['functions']],[36961,22281,9])
        self.assertIn(0x230ea0,fixture['boundaries'])
        self.assertEqual(set(fixture['globals']),{'GLOBAL_IS_SHIMADA','GLOBAL_IS_GRANITE','GLOBAL_IS_GPT'})
