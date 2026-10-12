"""TVB table recovery against complete original function instructions."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('tvb_table_test', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-tvb-table.py')
table = importlib.util.module_from_spec(spec)
spec.loader.exec_module(table)


class IntelTvbTable(unittest.TestCase):
    def test_original_selectors_bytes_and_failure_behavior(self):
        report = table.investigate(table.load_fixture())
        self.assertEqual(len(report['scenarios']),88)
        self.assertEqual(sum(r['outcome'] == 'instruction-limit' for r in report['scenarios']),3)

    def test_original_reader_count_is_not_a_capability_probe(self):
        fixture = table.load_fixture()
        for count in (0,1,6,8,10):
            m = table.TableMachine(fixture,dict(count=count,status=5))
            self.assertEqual(m.run(table.READ)['outcome'],'returned')
            self.assertEqual(len(m.writes),count)
            self.assertEqual(len(m.rows(m.output)),count)

    def test_original_four_bytes_are_independent(self):
        fixture = table.load_fixture()
        for shift in (0,8,16,24):
            for value in (1,127,128,255):
                raw = value << shift
                m = table.TableMachine(fixture,dict(count=1,data=raw))
                self.assertEqual(m.run(table.READ)['outcome'],'returned')
                self.assertEqual(m.rows(m.output),[[1,raw >> 24,(raw >> 8) & 255,raw & 255,(raw >> 16) & 255]])
