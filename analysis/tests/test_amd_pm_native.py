"""Original PM transfer does not check completion; VID helper routes disagree."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('pm_native',Path(__file__).resolve().parents[1]/'tools/legacy-amd-pm-native.py')
native=importlib.util.module_from_spec(spec); spec.loader.exec_module(native)


class NativePmOriginal(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.fixture=native.load_fixture()

    def test_transport_returns_without_reading_response(self):
        for symbol in (native.START,native.BASE32,native.BASE64):
            baseline=native.Machine(self.fixture,{}).result(symbol)
            self.assertFalse(any(not r['write'] and r['index']==0x3b10970 for r in baseline['pci']))
            self.assertTrue(any(r['write'] and r['index']==0x3b10970 and r['value']==1 for r in baseline['pci']))
            for index in range(len(baseline['pci'])):
                row=native.Machine(self.fixture,dict(fail_at=index)).result(symbol)
                self.assertEqual(len(row['pci']),len(baseline['pci']))
        for symbol in (native.BASE32,native.BASE64):
            row=native.Machine(self.fixture,dict(low=0x23456000,high=1)).result(symbol)
            self.assertEqual(row['returned'],0x23456000 if symbol==native.BASE32 else 0x123456000)
            self.assertEqual([r['locked'] for r in row['pci'] if not r['write']], [False] if symbol==native.BASE32 else [True,True])

    def test_shimada_general_helper_uses_svi2_while_rails_uses_svi3(self):
        for shimada in (0,1):
            for value in range(512):
                row=native.Machine(self.fixture,dict(am5_es=1,GLOBAL_IS_GRANITE=0,
                    GLOBAL_IS_SHIMADA=shimada,GLOBAL_IS_GPT=0,value=value)).result(native.VOLTAGE)
                expected=int(1550-value*6.25) if shimada else value*5+245
                self.assertEqual(row['returned'],expected & 0xffffffff)
        # Independent original rails instruction fixture, not the new preview.
        rails=native.oc.pbo.Machine(native.oc.pbo.load_fixture(),dict(rail0=700,rail1=1250)).result(native.oc.pbo.RAILS)
        packed=91 | (201<<17)
        self.assertTrue(any(r['write'] and r['index']==0x3b10a40 and r['value']==packed for r in rails['pci']))
