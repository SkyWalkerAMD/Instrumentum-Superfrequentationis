"""Original AVX behavior is characterized independently of the new core."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('avx_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-avx.py')
avx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(avx)


class IntelAvx(unittest.TestCase):
    def test_original_complete_functions(self):
        report = avx.investigate(avx.load_fixture())
        self.assertEqual(len(report['scenarios']), 720)

    def test_fields_do_not_alias(self):
        fixture = avx.load_fixture()
        for bit in range(32):
            machine = avx.AvxMachine(fixture, dict(raw=1 << bit))
            self.assertEqual(machine.run(avx.READER)['outcome'], 'returned')
            self.assertEqual(machine.result(), [((1 << bit) >> 5) & 31, ((1 << bit) >> 10) & 31])
