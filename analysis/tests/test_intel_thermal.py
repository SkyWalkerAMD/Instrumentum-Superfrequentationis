import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('thermal_test',Path(__file__).resolve().parents[1]/'tools/legacy-intel-thermal.py')
thermal=importlib.util.module_from_spec(spec); spec.loader.exec_module(thermal)


class IntelThermal(unittest.TestCase):
    def test_original_complete_default_temperature_getter(self):
        self.assertEqual(len(thermal.investigate(thermal.load_fixture())['scenarios']),644)

    def test_configured_offset_does_not_change_default_temperature(self):
        for offset in range(64):
            machine=thermal.ThermalMachine(thermal.load_fixture(),dict(raw=(offset<<24)|(100<<16)))
            self.assertEqual(machine.run(thermal.SYMBOL)['outcome'],'returned')
            self.assertEqual(machine.reg('rax'),100)
