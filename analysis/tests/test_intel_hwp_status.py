"""Use the original bytes to characterize the read-only HWP status entry."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('hwp_status_test',Path(__file__).resolve().parents[1]/'tools/legacy-intel-hwp-status.py')
status=importlib.util.module_from_spec(spec); spec.loader.exec_module(status)


class IntelHwpStatus(unittest.TestCase):
    def test_original_status(self):
        self.assertEqual(len(status.investigate(status.load_fixture())['scenarios']),138)

    def test_original_does_not_initialize_failed_output(self):
        m=status.StatusMachine(status.load_fixture(),dict(raw=0,read_result=0,supply_data=False,stack_low=0xdeadbeef))
        self.assertEqual(m.run(status.SYMBOL)['outcome'],'returned')
        self.assertEqual(m.reg('rax'),0xdeadbeef)
