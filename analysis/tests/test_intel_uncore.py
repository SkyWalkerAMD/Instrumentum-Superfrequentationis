"""Original cache-ratio setter, with synthetic hardware boundaries only."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('uncore_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-uncore.py')
uncore = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uncore)


class IntelUncore(unittest.TestCase):
    def test_original_side_effects_and_busy_loop(self):
        result = uncore.investigate(uncore.load_fixture())
        self.assertEqual(len(result['scenarios']), 48)
        self.assertEqual(result['busy_loop']['outcome'], 'instruction-limit')
