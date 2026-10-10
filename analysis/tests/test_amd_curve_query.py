"""Characterize original curve-query failures without hardware access."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('curve_test', Path(__file__).resolve().parents[1] / 'tools/legacy-amd-curve-query.py')
curve = importlib.util.module_from_spec(spec); spec.loader.exec_module(curve)


class AmdCurveQuery(unittest.TestCase):
    def test_constructor_shimada_override_preserves_alternate_argument(self):
        fixture = curve.load_fixture()
        for gpt in (0, 1):
            machine = curve.CurveMachine(fixture, dict(shimada=1, gpt=gpt))
            fields = machine.construct()
            self.assertEqual((fields['command'], fields['response'], fields['query']), (0x3b10924, 0x3b10970, 0xa3))
            self.assertEqual(fields['argument'], 0x3b10a88 if gpt else 0x3b10a40)

    def test_original_reads_unconfirmed_argument_after_ten_failed_polls(self):
        for response in (0, 0xfe, 0xffffffff):
            machine = curve.CurveMachine(curve.load_fixture(), dict(shimada=1, response=response))
            machine.construct(); machine.run(curve.QUERY)
            self.assertEqual(machine.polls, 10)
            self.assertEqual(machine.reg('rax') & 0xffffffff, 0x30700000)
            self.assertFalse(machine.pci[-1]['write'])
            self.assertEqual(machine.pci[-1]['index'], 0x3b10a40)

    def test_original_ignores_object_bdf_and_truncates_core(self):
        machine = curve.CurveMachine(curve.load_fixture(), dict(shimada=1, identity=[0x1022, 0x1480], found=0xabcd, ccd=16, core=257))
        fields = machine.construct(); machine.run(curve.QUERY)
        self.assertEqual(fields['device'], 0xabcd)
        self.assertTrue(all(row['bdf'] == 0 for row in machine.pci))
        self.assertEqual(machine.pci[3]['value'], 0x00100000)

    def test_failed_argument_read_is_returned_as_a_valid_looking_integer(self):
        machine = curve.CurveMachine(curve.load_fixture(), dict(shimada=1, fail_at=9, read_failure=0xffffffff))
        machine.construct(); machine.run(curve.QUERY)
        self.assertTrue(machine.pci[-1]['error'])
        self.assertEqual(machine.reg('rax') & 0xffffffff, 0xffffffff)
