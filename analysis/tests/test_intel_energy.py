"""Original MSR 1FC behavior, independently of the production implementation."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('energy_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-energy.py')
energy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(energy)


class IntelEnergy(unittest.TestCase):
    def test_original_functions_and_errors(self):
        report = energy.investigate(energy.load_fixture())
        self.assertEqual(len(report['scenarios']), 434)

    def test_neighboring_controls_and_high_word(self):
        fixture = energy.load_fixture()
        for symbol, bit in energy.WRITERS.items():
            for controls in range(32):
                for disabled in (0, 1):
                    raw = 0xfedcba9800000001 | controls << 17
                    machine = energy.EnergyMachine(fixture, dict(raw=raw, disabled=disabled))
                    self.assertEqual(machine.run(symbol)['outcome'], 'returned')
                    self.assertEqual(machine.trace[-1]['value'], (raw & ~(1 << bit)) | disabled << bit)
