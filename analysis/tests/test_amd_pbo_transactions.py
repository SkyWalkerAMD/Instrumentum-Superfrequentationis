"""Guard the recovered original traces; they are not production protocols."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('pbo_transactions',Path(__file__).resolve().parents[1]/'tools/legacy-amd-pbo-transactions.py')
pbo=importlib.util.module_from_spec(spec);spec.loader.exec_module(pbo)


def writes(row):
    return [(x['index'],x['value']) for x in row['pci'] if x['write'] and x['offset'] in (0xbc,0xfc)]


class PboTransactionEvidence(unittest.TestCase):
    def test_shimada_constructor_and_fmax(self):
        m=pbo.Machine(pbo.load_fixture(),dict(device=0x153a,flag11=0))
        row=m.result(pbo.CTOR);obj=bytes.fromhex(row['object_hex'])
        self.assertEqual(obj[16:18],b'\0\1')
        row=m.result(pbo.FMAX)
        self.assertEqual(writes(row),[(0x50200,1),(0x3b10970,0),(0x3b10a40,5000),(0x3b10970,0),(0x3b10a40,5000),(0x3b10924,0x70)])

    def test_fmax_flag_command_and_no_completion_check(self):
        for flag,command in ((0,0x6d),(1,0x70)):
            for frequency in (0,5000,pbo.U32):
                row=pbo.Machine(pbo.load_fixture(),dict(flag11=flag,frequency=frequency)).result(pbo.FMAX)
                self.assertEqual(writes(row)[-1],(0x3b10924,command))
                self.assertTrue(all(x['write'] for x in row['pci']))
                self.assertEqual([v for a,v in writes(row) if a==0x3b10a40],[frequency,frequency])

    def test_rails_copy_zero_pack_and_implicit_enable(self):
        for a,b in ((0,0),(0,1000),(1000,0),(700,1250),(244,245),(-1,3000),(2147483647,-2147483648)):
            row=pbo.Machine(pbo.load_fixture(),dict(rail0=a,rail1=b)).result(pbo.RAILS)
            if a==b==0:self.assertEqual(row['pci'],[]);continue
            def encoded(v):
                n=(v-245)&pbo.U32;n=n if n<0x80000000 else n-(1<<32)
                return ((abs(n)//5)*(1 if n>=0 else -1))&0x1ff
            expected=encoded(a or b)|(encoded(b or a)<<17)
            self.assertEqual(writes(row),[(0x3b10970,0),(0x3b10970,1),(0x3b10a40,expected),(0x3b10924,0x61),(0x3b10924,0x5d),(0x3b10970,0),(0x3b10970,1),(0x3b10a40,0)])
            self.assertFalse(any(x['locked'] or not x['write'] for x in row['pci']))

    def test_mp1_name_uses_shimada_rsmu_and_reads_output_on_failure(self):
        for response in (0,1,0xfe,pbo.U32):
            row=pbo.Machine(pbo.load_fixture(),dict(response=response,returned=0x634)).result(pbo.MP1)
            self.assertEqual(writes(row),[(0x50200,1),(0x3b10970,0),(0x3b10a40,0),(0x3b10970,0),(0x3b10a40,0),(0x3b10924,0xa8)])
            self.assertEqual(row['returned'],response);self.assertEqual(row['output'],0x634)
            self.assertEqual(sum(not x['write'] and x['index']==0x3b10970 for x in row['pci']),1 if response==1 else 21)
            self.assertEqual(row['pci'][-1]['index'],0x3b10a40)

    def test_original_does_not_stop_on_failed_pci_write(self):
        for symbol in (pbo.FMAX,pbo.RAILS,pbo.MP1):
            baseline=pbo.Machine(pbo.load_fixture(),{}).result(symbol)
            for at,request in enumerate(baseline['pci']):
                if not request['write']:continue
                row=pbo.Machine(pbo.load_fixture(),dict(fail_at=at)).result(symbol)
                self.assertEqual(len(row['pci']),len(baseline['pci']))
                self.assertEqual(writes(row),writes(baseline))
