"""Pin original turbo-table packing and forwarded failures without hardware."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('turbo_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-turbo.py')
turbo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(turbo)


class IntelTurbo(unittest.TestCase):
    def test_complete_original_writers(self):
        self.assertEqual(len(turbo.investigate(turbo.load_fixture())['scenarios']), 520)

    def test_stack_arguments_and_failed_write(self):
        machine = turbo.TurboMachine(turbo.load_fixture(), dict(values=[1, 2, 3, 4, 5, 6, 257, -1], write_result=0))
        result = machine.run('_Z5Wr651iiiiiiii')
        self.assertEqual(result['return_al'], 0)
        self.assertEqual(machine.writes, [dict(msr=0x651, low=0x04030201, high=0xff010605)])
        self.assertEqual(machine.calls, ['_Z5Wrmsrjjj'])
