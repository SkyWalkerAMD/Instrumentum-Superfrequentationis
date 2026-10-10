"""Original client ratio behavior at the synthetic MSR boundary."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('ratio_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-ratio.py')
ratio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ratio)


class IntelRatio(unittest.TestCase):
    def test_original_preserves_voltage_but_truncates_and_ignores_failure(self):
        fixture = ratio.load_fixture()
        for symbol, (writes, domain) in ratio.FUNCTIONS.items():
            if not writes:
                continue
            m = ratio.RatioMachine(fixture, dict(ratio=257, data=0xf3512345, status=3, read_result=0, write_result=0))
            self.assertEqual(m.run(symbol)['outcome'], 'returned')
            self.assertEqual(m.writes, [dict(low=0, high=0x80000010 | domain << 8),
                                       dict(low=0xf3512301, high=0x80000011 | domain << 8)])
            self.assertEqual(len(m.reads), 1)

    def test_original_busy_loop_has_no_software_timeout(self):
        fixture = ratio.load_fixture()
        m = ratio.RatioMachine(fixture, dict(ratio=59, data=0, busy=True))
        self.assertEqual(m.run('_Z14Wr_150maxratioi', instruction_limit=3000, allow_instruction_bound=True)['outcome'], 'instruction-limit')
        self.assertEqual(len(m.writes), 1)
