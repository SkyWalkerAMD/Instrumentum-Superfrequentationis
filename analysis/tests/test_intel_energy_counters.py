"""Original energy counter getters are not reimplementations of the new core."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('counters_test',Path(__file__).resolve().parents[1]/'tools/legacy-intel-energy-counters.py')
counters=importlib.util.module_from_spec(spec); spec.loader.exec_module(counters)


class IntelEnergyCounters(unittest.TestCase):
    def test_original_getters(self):
        self.assertEqual(len(counters.investigate(counters.load_fixture())['scenarios']),400)

    def test_high_bits_are_not_counter_ticks(self):
        m=counters.CounterMachine(counters.load_fixture(),dict(raw=0xffffffff00000000))
        self.assertEqual(m.run(counters.COUNTER)['outcome'],'returned')
        self.assertEqual(m.reg('rax'),0)
