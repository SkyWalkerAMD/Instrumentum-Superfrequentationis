"""Original storage collisions must stay visible, rather than being clipped."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('pm_percore',Path(__file__).resolve().parents[1]/'tools/legacy-amd-pm-percore.py')
pm=importlib.util.module_from_spec(spec);spec.loader.exec_module(pm)


class PerCoreLayoutEvidence(unittest.TestCase):
    def trace(self,version,ccds,pattern=0):
        return pm.Machine(pm.limits.load_fixture(),version,ccds,pattern).trace()

    def test_original_labels_and_slot_storage(self):
        for pattern in (0,1):
            r=self.trace(0x730c04,12,pattern);core,ccd=r['groups']
            self.assertEqual([x['label'] for x in core['rows']],list(pm.CORE_LABELS))
            self.assertEqual([x['label'] for x in ccd['rows']],list(pm.CCD_LABELS))
            self.assertEqual((core['slots'],ccd['slots']),(96,12))
            self.assertEqual((core['rows'][0]['offsets'][0],ccd['rows'][0]['offsets'][0]),(0x634,0x2134))
            self.assertEqual(r['core_ccd_overlap'],[])

    def test_small_table_ccd_range_conflict(self):
        r=self.trace(0x730204,2)
        self.assertEqual(r['groups'][0]['outside_public_table'],[])
        self.assertEqual(len(r['groups'][1]['outside_public_table']),22)
        self.assertTrue(all(p>=r['public_table_bytes'] for p in r['groups'][1]['outside_public_table']))

    def test_constructor_six_ccd_input_falls_back_to_two(self):
        r=self.trace(0x730604,6)
        self.assertEqual([g['slots'] for g in r['groups']],[16,2])

    def test_in_bounds_does_not_establish_correct_layout(self):
        r=self.trace(0x731004,16)
        self.assertTrue(all(not g['outside_public_table'] for g in r['groups']))
        self.assertEqual(len(r['core_ccd_overlap']),176)
        self.assertEqual(r['common_field_conflicts'],[0x714,0x71c])
