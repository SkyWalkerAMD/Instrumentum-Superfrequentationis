"""Original BCLK voltage evidence is independent of the replacement core."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('bclk_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-bclk-voltage.py')
bclk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bclk)


class IntelBclkVoltage(unittest.TestCase):
    def test_complete_original_functions(self):
        report = bclk.investigate(bclk.load_fixture())
        self.assertEqual(len(report['scenarios']), 926)

    def test_invalid_bool_changes_neighboring_bits_in_original(self):
        machine = bclk.BclkMachine(bclk.load_fixture(), dict(raw=0, enabled=2))
        self.assertEqual(machine.run(bclk.WRITER)['outcome'], 'returned')
        writes = [row for row in machine.trace if row['write']]
        self.assertEqual(writes[-1]['low'], 8)
        self.assertEqual(machine.reads[0x14], 2)  # No independent verification after set.
