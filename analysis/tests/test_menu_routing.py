"""Old menu labels do not imply hardware checks or one window per action."""
import importlib.util
from pathlib import Path
import unittest

TOOL=Path(__file__).resolve().parents[1]/'tools/emulate-legacy-menu-routing.py'
spec=importlib.util.spec_from_file_location('menu_routing',TOOL)
menu=importlib.util.module_from_spec(spec);spec.loader.exec_module(menu)


class MenuRoutingTest(unittest.TestCase):
    def run_menu(self,symbol,inputs):
        m=menu.MenuMachine(menu.load_fixture(),inputs)
        self.assertEqual(m.run(symbol)['outcome'],'returned')
        return m

    def test_trx50_name_has_priority_over_tr5_pci_and_shimada(self):
        m=self.run_menu(menu.AMD,{'vendor':0x1022,'board':'Pro WS TRX50-SAGE WIFI','tr5_bdf':0,'shimada':True})
        self.assertEqual([p['panel'] for p in m.panels],['tr5_mb2'])
        self.assertNotIn('_Z9is_tr5_esv',m.calls)
        m=self.run_menu(menu.AMD,{'vendor':0x1022,'board':'trx50','tr5_bdf':-1})
        self.assertEqual([p['panel'] for p in m.panels],['am5_mb3'])

    def test_w790_action_checks_amd_then_cached_flag_not_board_name(self):
        m=self.run_menu(menu.W790,{'vendor':0xffff,'board':'W790 ACE','gnr':True})
        self.assertEqual([p['panel'] for p in m.panels],['w890_mb','w890_vrm_module'])
        self.assertFalse(m.searches)
        m=self.run_menu(menu.W790,{'vendor':0x1022,'gnr':True})
        self.assertFalse(m.panels);self.assertIn('Not Supported!',m.qt_texts)

    def test_adl_timings_action_constructs_two_panels(self):
        m=self.run_menu(menu.TIMINGS,{'vendor':0x8086,'device':0x4648})
        self.assertEqual([p['panel'] for p in m.panels],['intel_memtime','adl_timings'])
        self.assertTrue(all(not p['constructor_body_executed'] for p in m.panels))


if __name__=='__main__':unittest.main()
