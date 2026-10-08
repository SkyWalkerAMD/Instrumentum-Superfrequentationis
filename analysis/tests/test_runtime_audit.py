"""Real ELF fixtures: symbol versions, PLT/CET and -fno-plt GOT references."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'tools/elf-runtime-audit.py'
spec = importlib.util.spec_from_file_location('runtime_audit', TOOL)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


@unittest.skipUnless(shutil.which('gcc'), 'requires Linux x86-64 gcc/binutils')
class AuditTest(unittest.TestCase):
    def test_zero_size_assembly_and_object_evidence(self):
        # Link but never execute these I/O instructions. The local boundary
        # label must end a span even though it is not a function symbol.
        with tempfile.TemporaryDirectory(prefix='octool-asm-audit-') as directory:
            root = Path(directory)
            (root/'main.c').write_text('volatile unsigned char fixture_flag;\n'
                                       'int main(void){return fixture_flag;}\n')
            (root/'probe.S').write_text('''
.text
.globl raw_probe, raw_alias
raw_probe:
raw_alias:
  mov $172, %eax
  syscall
  in %dx, %al
  out %al, %dx
  ret
raw_boundary:
  ud2
.section .fixture_tail,"ax",@progbits
.globl raw_tail
raw_tail:
  ret
.section .fixture_bad,"ax",@progbits
.globl raw_bad
raw_bad:
  .byte 0x0f
.data
.globl raw_data
raw_data:
  .byte 0xc3
.section .note.GNU-stack,"",@progbits
''')
            subprocess.check_call(['gcc', '-fPIC', '-pie', 'main.c', 'probe.S', '-o', 'fixture'], cwd=root)
            result = audit.inspect(root/'fixture', re.compile('^main$'),
                                   assembly_pattern=re.compile('^raw_(probe|alias|tail|bad)$'),
                                   object_pattern=re.compile('^fixture_flag$'))
            spans = result['assembly_spans']
            probe = next(s for s in spans if 'raw_probe' in s['symbols'])
            self.assertEqual(probe['symbols'], ['raw_alias', 'raw_probe'])
            self.assertEqual(probe['boundary_symbols'], ['raw_boundary'])
            self.assertEqual([i['mnemonic'] for i in probe['instructions']],
                             ['mov', 'syscall', 'in', 'out', 'ret'])
            self.assertTrue(probe['complete_decode'])
            tail = next(s for s in spans if 'raw_tail' in s['symbols'])
            self.assertEqual(tail['boundary_symbols'], [])
            self.assertEqual(tail['decoded_bytes'], 1)
            bad = next(s for s in spans if 'raw_bad' in s['symbols'])
            self.assertFalse(bad['complete_decode'])
            self.assertEqual(bad['decoded_bytes'], 0)
            flag, = result['selected_objects']
            self.assertTrue(flag['zero_initialized'])
            self.assertEqual(flag['initial_bytes_hex'], '00')
            self.assertTrue(any(r['function'] == 'main' for r in flag['references']))
            self.assertFalse(result['executed'])
            with self.assertRaisesRegex(ValueError, 'executable section'):
                audit.inspect(root/'fixture', re.compile('^main$'),
                              assembly_pattern=re.compile('^raw_data$'))

    def test_plt_cet_and_got_versions(self):
        with tempfile.TemporaryDirectory(prefix='octool-elf-audit-') as directory:
            root = Path(directory)
            (root/'provider.c').write_text('int fixture_data=7; int fixture_func(int x){return x+1;}\n')
            (root/'versions.map').write_text('AUDIT_1 { global: fixture_func; fixture_data; local: *; };\n')
            (root/'main.c').write_text('extern int fixture_data; extern int fixture_func(int);\n'
                                       'int main(void){return fixture_func(fixture_data);}\n')
            subprocess.check_call(['gcc', '-shared', '-fPIC', 'provider.c', '-o', 'libfixture.so',
                                   '-Wl,--version-script=versions.map,-soname,libfixture.so'], cwd=root)
            for options in ([], ['-fcf-protection=full', '-Wl,-z,ibtplt'], ['-fno-plt']):
                subprocess.check_call(['gcc', '-fPIC', '-pie', 'main.c', '-L.', '-lfixture',
                                       '-o', 'fixture'] + options, cwd=root)
                result = audit.inspect(root/'fixture', re.compile('^main$'))
                for symbol in ('fixture_func', 'fixture_data'):
                    imported = next(s for s in result['imports'] if s['symbol'] == symbol)
                    self.assertEqual(imported['provider'], 'libfixture.so')
                    self.assertEqual(imported['version'], 'AUDIT_1')
                    self.assertTrue(any(s['function'] == 'main' for s in imported['sites']), options)
                self.assertFalse(result['executed'])
                self.assertFalse(result['incomplete_decodes'])


if __name__ == '__main__':
    unittest.main()
