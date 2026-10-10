"""Pinned original VF instructions, with synthetic MSR boundaries."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('vf_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-vf.py')
vf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vf)


class IntelVf(unittest.TestCase):
    def test_original_characterization(self):
        self.assertEqual(len(vf.investigate(vf.load_fixture())['scenarios']), 135)

    def test_original_core_override_side_effect_precedes_point_query(self):
        machine = vf.VfMachine(vf.load_fixture(), dict(point=8, data=0xf351234d, status=3, read_result=0, write_result=0))
        self.assertEqual(machine.run(vf.CORE)['outcome'], 'returned')
        self.assertEqual([w['high'] for w in machine.writes], [0x80000014, 0x80000015, 0x80080010, 0x80080011])
        self.assertEqual(machine.writes[1]['low'], 0xf3512345)
        self.assertEqual(machine.writes[-1]['low'] & 0x1fffff, 0)

    def test_original_query_truncates_selector_and_ignores_transport_failure(self):
        machine = vf.VfMachine(vf.load_fixture(), dict(point=256, read_result=0, write_result=0))
        self.assertEqual(machine.run(vf.QUERY)['return_al'], 1)
        self.assertEqual(machine.writes, [dict(low=0, high=0x80000010)])
