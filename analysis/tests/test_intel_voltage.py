"""Pinned original target and mode instructions with synthetic MSR boundaries."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('voltage_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-voltage.py')
voltage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(voltage)


class IntelVoltage(unittest.TestCase):
    def test_original_characterization(self):
        self.assertEqual(len(voltage.investigate(voltage.load_fixture())['scenarios']), 82)

    def test_adaptive_writes_both_domains_and_uses_original_inexact_step(self):
        m = voltage.VoltageMachine(voltage.load_fixture(), dict(millivolts=1000, data=0xf3512345))
        self.assertEqual(m.run(voltage.ADAPTIVE)['outcome'], 'returned')
        self.assertEqual([w['high'] for w in m.writes], [0x80000010, 0x80000011, 0x80000210, 0x80000211])
        self.assertEqual(m.writes[1]['low'], 0xf343ff45)
        self.assertEqual(m.writes[1]['low'], m.writes[3]['low'])

    def test_cache_override_clears_offset_even_after_rejection(self):
        m = voltage.VoltageMachine(voltage.load_fixture(), dict(millivolts=1000, data=0xf3512345, status=3, read_result=0, write_result=0))
        self.assertEqual(m.run(voltage.CACHE_OVERRIDE)['outcome'], 'returned')
        self.assertEqual(m.writes, [dict(low=0, high=0x80000210), dict(low=0x13ff45, high=0x80000211)])
        self.assertEqual(len(m.reads), 1)
