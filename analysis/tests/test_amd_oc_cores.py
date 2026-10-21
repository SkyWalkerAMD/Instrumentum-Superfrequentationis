"""Original CCD loops do not branch on command-write boundary return values."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('oc_cores',Path(__file__).resolve().parents[1]/'tools/legacy-amd-oc-cores.py')
cores=importlib.util.module_from_spec(spec);spec.loader.exec_module(cores)


class OriginalCcdLoops(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=cores.oc.load_fixture()

    def test_each_command_failure_position_still_reaches_all_eight_cores(self):
        for symbol in (cores.oc.CCD,cores.oc.CCD_FAST):
            for ccd in (0,1,7,15):
                for failure in range(8):
                    for returned in (0,0xfe,0xffffffff):
                        row=cores.Machine(self.fixture,dict(target=ccd,fail_at=failure,failure_return=returned)).result(symbol)
                        self.assertEqual([r['value'] for r in row['operations'] if r['kind']=='arg0'],[(ccd<<28)|(core<<20)|5000 for core in range(8)])
                        self.assertEqual([r['value'] for r in row['operations'] if r['kind']=='command'],[0x27]*8)
                        self.assertEqual([r['synthetic_return'] for r in row['command_boundary_returns']],[returned if i==failure else 1 for i in range(8)])
                        self.assertFalse(any('retrieve_message' in r['kind'] for r in row['operations']))

    def test_fast_argument_retention_and_explicit_failure_are_distinct(self):
        for symbol in (cores.oc.CCD,cores.oc.CCD_FAST):
            row=cores.Machine(self.fixture,dict(target=15,fail_at=2,failure_return=0xfe)).result(symbol)
            arguments=[r for r in row['operations'] if r['kind'].startswith('arg')]
            self.assertEqual(len(arguments),8 if symbol==cores.oc.CCD_FAST else 48)
            for r in arguments:
                if r['kind']!='arg0':self.assertEqual(r['value'],0)
            self.assertEqual(len(row['command_boundary_returns']),8)
            self.assertEqual(row['command_boundary_returns'][2]['synthetic_return'],0xfe)
