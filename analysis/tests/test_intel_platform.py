import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('platform_test',Path(__file__).resolve().parents[1]/'tools/legacy-intel-platform.py')
platform=importlib.util.module_from_spec(spec); spec.loader.exec_module(platform)


class IntelPlatform(unittest.TestCase):
    def test_original_complete_ratio_getter(self):
        self.assertEqual(len(platform.investigate(platform.load_fixture())['scenarios']),644)

    def test_unrelated_platform_flags_do_not_change_ratio(self):
        fixture=platform.load_fixture()
        for bit in range(64):
            if 8<=bit<16: continue
            machine=platform.PlatformMachine(fixture,dict(raw=(42<<8)|(1<<bit)))
            self.assertEqual(machine.run(platform.SYMBOL)['outcome'],'returned')
            self.assertEqual(machine.reg('rax'),42)
