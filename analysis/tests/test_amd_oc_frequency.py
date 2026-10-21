"""Pinned original OC instructions characterize routing, masks and side effects."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('oc_frequency',Path(__file__).resolve().parents[1]/'tools/legacy-amd-oc-frequency.py')
oc=importlib.util.module_from_spec(spec);spec.loader.exec_module(oc)


class OriginalOc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=oc.load_fixture()

    def trace(self,symbol,**inputs):
        return oc.Machine(self.fixture,inputs).result(symbol)['operations']

    def test_mode_and_all_frequency_route_without_acceptance_check(self):
        for symbol,bios,mp1,value in ((oc.ENABLE,0x24,0x17,1),(oc.DISABLE,0x25,0x18,1),(oc.ALL,0x26,0x19,5000)):
            for phoenix,gpt in ((0,0),(1,0),(0,1),(1,1)):
                previous=None
                for returned in (0,1,0xfe):
                    rows=self.trace(symbol,phoenix=phoenix,GLOBAL_IS_GPT=gpt,boundary_return=returned)
                    if previous is not None:self.assertEqual(rows,previous)
                    previous=rows
                    if phoenix or gpt:
                        self.assertEqual(rows,[dict(kind='mp1',message=mp1,argument=value)])
                    else:
                        self.assertEqual([r['value'] for r in rows if r['kind'].startswith('arg')],[value,0,0,0,0,0])
                        self.assertEqual([r['value'] for r in rows if r['kind']=='command'],[bios])

    def test_all_128_flat_selectors_expose_original_seven_vs_eight_mismatch(self):
        for symbol in (oc.CORE,oc.CORE_FAST):
            mismatches=[]
            for target in range(128):
                rows=self.trace(symbol,target=target)
                # The exact instructions divide by 8 but adjust using remainder
                # modulo 7. Fast also omits slow's lower bound at CCD zero.
                ccd=target//8-int(target%7==0)
                if symbol==oc.CORE:ccd=max(0,ccd)
                encoded=((ccd&15)<<28)|(target%8<<20)|5000
                self.assertEqual([r['value'] for r in rows if r['kind']=='arg0'],[encoded])
                if encoded!=((target//8<<28)|(target%8<<20)|5000):mismatches.append(target)
                self.assertEqual([r['value'] for r in rows if r['kind']=='command'],[0x27])
                writes=[r for r in rows if r['kind'].startswith('arg') or r['kind']=='command']
                self.assertEqual(len(writes),2 if symbol==oc.CORE_FAST else 7)
                self.assertTrue(all(r['bdf']==(0x1234 if symbol==oc.CORE_FAST else None) for r in writes))
            self.assertEqual(mismatches,list(range(14,128,7)) if symbol==oc.CORE else list(range(0,128,7)))

    def test_ccd_loops_over_eight_firmware_indices_even_after_failure(self):
        for symbol in (oc.CCD,oc.CCD_FAST):
            for target in range(16):
                rows=self.trace(symbol,target=target,boundary_return=0xfe)
                self.assertEqual([r['value'] for r in rows if r['kind']=='arg0'],[(target<<28)|(core<<20)|5000 for core in range(8)])
                self.assertEqual([r['value'] for r in rows if r['kind']=='command'],[0x27]*8)
                self.assertFalse(any('retrieve_message' in r['kind'] for r in rows))

    def test_truncation_aliases_and_voltage_clamping_are_not_validation(self):
        for symbol in (oc.CORE,oc.CORE_FAST):
            for value in (0,0x100000,-1):
                rows=self.trace(symbol,target=128,value=value)
                self.assertEqual([r['value'] for r in rows if r['kind']=='arg0'],[value&0xfffff])
        for symbol in (oc.CCD,oc.CCD_FAST):
            rows=self.trace(symbol,target=16,value=0)
            self.assertEqual([r['value'] for r in rows if r['kind']=='arg0'],[core<<20 for core in range(8)])
        rows=self.trace(oc.ALL,value=-1)
        self.assertEqual([r['value'] for r in rows if r['kind']=='arg0'],[0xffffffff])
        for value in (-1,0,699,700,701,1250,2400,2401,0x7fffffff):
            rows=self.trace(oc.VID,value=value,boundary_return=0xfe)
            self.assertEqual([r['value'] for r in rows if r['kind']=='arg0'],[(min(2400,max(700,value))-245)//5])
            self.assertEqual([r['value'] for r in rows if r['kind']=='command'],[0x28])
