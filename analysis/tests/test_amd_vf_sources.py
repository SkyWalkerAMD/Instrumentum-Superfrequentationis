"""Prevent original VF/EC/SoC helpers from becoming unsupported hardware claims."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('vf_sources',Path(__file__).resolve().parents[1]/'tools/legacy-amd-vf-sources.py')
vf=importlib.util.module_from_spec(spec); spec.loader.exec_module(vf)


class OriginalVfSources(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.report=vf.investigate(vf.load_fixture())

    def test_msr_formula_and_unchecked_failure(self):
        self.assertEqual(len(self.report['vf']),520)
        for r in self.report['vf']:
            p=r['inputs']; low=p.get('stack_low',0) if p.get('read_result',1)==0 else p['low']
            fid,did,vid=low&255,(low>>8)&63,(low>>14)&255
            self.assertEqual(r['events'][0]['msr'],0xc0010293)
            self.assertEqual(r['events'][0]['affinity'],p.get('affinity',1))
            if not did:
                self.assertEqual(r['outcome'],'integer-divide-zero')
                self.assertEqual(r['vf_words'],[0xddccbbaaddccbbaa]*2)
            else:
                self.assertEqual(r['outcome'],'returned')
                self.assertEqual(r['vf_words'],[((fid*1000000//did)*200)//1000000,245+5*vid])
        self.assertEqual(sum(r['outcome']=='integer-divide-zero' for r in self.report['vf']),8)

    def test_ec_bit_is_a_separate_board_query(self):
        self.assertEqual(len(self.report['ec']),256)
        for r in self.report['ec']:
            self.assertEqual(r['events'],[dict(ec_offset=0x4c,bank=2)])
            self.assertEqual(r['returned'],(r['inputs']['ec_byte']>>3)&1)
            self.assertEqual(r['pci'],[])

    def test_pm_consumer_compacts_vectors_and_reuses_existing_rows(self):
        base,failed,empty,short,full,missing=self.report['stream']
        expected=[[4000,4500],[50,52],[1000,1250],[3500,4000]]
        self.assertEqual(base['label_indices'],[0,1,2,3])
        self.assertEqual(base['vectors'],expected)
        self.assertEqual(failed['vectors'],expected)  # synthetic failed refresh ignored
        self.assertEqual([e['mode'] for e in base['events']],[0,4])
        self.assertEqual(empty['vectors'],[[],[],[],[]])
        self.assertEqual(short['outcome'],'range-error-boundary')
        self.assertEqual(short['vectors'],expected)
        self.assertEqual(full['vectors'],[[4000,4250,4500],[50,51,52],[1000,1125,1250],[3500,3750,4000]])
        self.assertEqual(missing['label_indices'],[0,1,0,3])
        self.assertEqual(missing['vectors'][0],[1000,1125,1250])
        for r in self.report['stream']: self.assertEqual(r['pci'],[])

    def test_soc_input_is_not_a_verified_dual_rail_contract(self):
        self.assertEqual(len(self.report['soc']),32)
        for r in self.report['soc']:
            force=r['symbol']==vf.FORCE; message=0x14 if force else 0x15
            device=r['inputs']['device']
            msg,rsp,arg=(0x3b10a20,0x3b10a80,0x3b10a88) if device==0x1630 else ((0x3b10924,0x3b10970,0x3b10a40) if device==0x153a else (0x3b10524,0x3b10570,0x3b10a40))
            writes=[(p['index'],p['value']) for p in r['pci'] if p['write'] and p['offset']==0xbc]
            self.assertEqual([v for a,v in writes if a==msg],[0xfd,message] if force else [0xfd,0xfd,message])
            self.assertEqual([v for a,v in writes if a==arg],[message])
            self.assertIn((rsp,0x12345678 if force else 0),writes)
            self.assertEqual([p['index'] for p in r['pci'] if not p['write']],[arg,rsp])
            self.assertEqual(r['output_word'],0x89abcdef)
            self.assertEqual(r['returned'],r['inputs']['response'])

    def test_soc_keeps_running_after_each_failed_pci_call(self):
        self.assertEqual(len(self.report['faults']),34)
        for r in self.report['faults']:
            self.assertEqual(len(r['pci']),16 if r['symbol']==vf.FORCE else 18)
            self.assertTrue(r['pci'][r['inputs']['fail_at']]['injected_failure'])
            self.assertEqual(r['outcome'],'returned')
