"""Original setter behavior is evidence, not the new runtime protocol."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('curve_set_test',Path(__file__).resolve().parents[1]/'tools/legacy-amd-curve-set.py')
curve=importlib.util.module_from_spec(spec);spec.loader.exec_module(curve)


class CurveSetterEvidence(unittest.TestCase):
    def test_original_negative_argument_preserves_ccx_and_reserved_bits(self):
        m=curve.Machine(curve.load_fixture(),dict(ccd=3,core=7,margin=-25))
        m.run(curve.SINGLE)
        self.assertEqual(m.messages,[0xd5,6])
        self.assertEqual(m.pci[1]['value'],0x3f7fffe7)
        self.assertFalse(m.pci[-1]['locked'])

    def test_original_ignores_failed_write_and_failed_response(self):
        for at in (1,3,7,11):
            m=curve.Machine(curve.load_fixture(),dict(fail_at=at,response=0xfe))
            self.assertEqual(m.run(curve.SINGLE)['outcome'],'returned')
            self.assertEqual(len(m.pci),12)
            self.assertEqual(m.messages,[0xd5,6])
            self.assertEqual(m.reg('rax')&curve.U32,curve.U32 if at==11 else 0xfe)

    def test_offset_wrapper_ignores_mp1_status(self):
        for status in (0,1,curve.U32):
            m=curve.Machine(curve.load_fixture(),dict(status=status,returned=0x634))
            m.run(curve.OFFSET)
            self.assertEqual(m.reg('rax')&curve.U32,0x634)
            self.assertEqual(m.mp1[0]['command'],0xa8)
