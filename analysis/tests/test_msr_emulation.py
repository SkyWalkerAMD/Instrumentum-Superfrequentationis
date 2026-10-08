"""MSR error propagation through unchanged UI and original syscall wrappers."""
import importlib.util
from pathlib import Path
import unittest

TOOL=Path(__file__).resolve().parents[1]/'tools/emulate-legacy-msr.py'
spec=importlib.util.spec_from_file_location('msr_emulation',TOOL)
msr=importlib.util.module_from_spec(spec)
spec.loader.exec_module(msr)


class MsrTest(unittest.TestCase):
    def test_failed_read_reuses_previous_write_command(self):
        for symbol,command in msr.SLOTS.items():
            machine=msr.MsrMachine(msr.load_fixture(),{'io_mode':'open-error'})
            result=machine.run(symbol,instruction_limit=msr.LIMIT,allow_instruction_bound=True)
            self.assertEqual(result['outcome'],'instruction-limit')
            reads=[r for r in machine.io if r['op']=='read']
            writes=[r for r in machine.io if r['op']=='write']
            self.assertEqual(len(writes),1)
            self.assertEqual(reads[0]['buffer'],writes[0]['buffer'])
            self.assertEqual(reads[0]['before_hex'],msr.struct.pack('<II',0,command).hex())
            self.assertTrue(all(r['before_hex']==r['after_hex'] for r in reads))

    def test_write_failure_still_reaches_applied_label(self):
        machine=msr.MsrMachine(msr.load_fixture(),{'io_mode':'write-error'})
        result=machine.run(next(iter(msr.SLOTS)))
        self.assertEqual(result['outcome'],'returned')
        self.assertIn('Applied!',result['qt_texts'])
        self.assertTrue(all(r['result']==-1 for r in machine.io if r['op']=='write'))

    def test_npu_and_gt_query_differ_but_commit_command_matches(self):
        queries=[];commits=[]
        for symbol in msr.SLOTS:
            machine=msr.MsrMachine(msr.load_fixture(),{'constructor_flag':1,'parsed_uint':257})
            machine.run(symbol)
            writes=[r for r in machine.io if r['op']=='write']
            queries.append(writes[0]['high']);commits.append(writes[1]['high'])
            self.assertEqual(writes[1]['low'] & 255,1)
            self.assertEqual(machine.flag_reads,0)
        self.assertNotEqual(*queries)
        self.assertEqual(commits,[0x80000111]*2)


if __name__=='__main__':unittest.main()
