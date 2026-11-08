#!/usr/bin/env python3
"""Validate analysis wrappers independently with pefile; never execute firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
W = runpy.run_path(str(ROOT / 'analysis/tools/firmware-te-pe.py'))
INPUTS = {}
ACTUAL_REPORTS = []


def fixture():
    # Literal TE with a nonzero, non-page-aligned ImageBase and raw != TE offset.
    body = bytearray(0xb0)
    struct.pack_into('<HHBBHIIQ', body, 0, 0x5a56, 0x14c, 1, 11, 0x1b8, 0x240, 0x240, 0x12345678)
    struct.pack_into('<8s6I2HI', body, 40, b'.text\0\0\0', 0x1b, 0x220, 0x20, 0x220,
                     0, 0, 0, 0, 0x60000020)
    # Correct entry is within this fixture's one 32-byte section.
    struct.pack_into('<I', body, 8, 0x224)
    struct.pack_into('<I', body, 12, 0x220)
    body[0x90:0xb0] = bytes(range(32))
    return bytes(body)


class WrapperTests(unittest.TestCase):
    def test_literal_offsets_and_nonzero_base(self):
        te = fixture()
        pe, report = W['wrap_te'](te)
        self.assertEqual(pe[0x220:0x240], bytes(range(32)))
        self.assertEqual(struct.unpack_from('<I', pe, 0xb4)[0], 0x12345678)
        self.assertEqual(report['entry_virtual_address'], '0x1234589c')
        self.assertEqual(W['rva_to_offset'](W['parse_te'](te), 0x224, 'te'), 0x94)
        self.assertEqual(W['rva_to_offset'](W['parse_pe'](pe), 0x224, 'pe'), 0x224)
        self.assertFalse(report['executable_image_characteristic'])

    def test_reject_header_variants(self):
        for offset, fmt, value in ((0, '<H', 0), (2, '<H', 0x8664), (4, '<B', 0),
                (5, '<B', 10), (6, '<H', 39), (16, '<Q', 0x100000000), (8, '<I', 0x5000)):
            body = bytearray(fixture())
            struct.pack_into(fmt, body, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                W['wrap_te'](body)

    def test_reject_truncation_and_overlay(self):
        for body in (fixture()[:20], fixture()[:60], fixture()[:-1], fixture()+b'\0'):
            with self.subTest(length=len(body)), self.assertRaises(ValueError):
                W['wrap_te'](body)

    def test_reject_auxiliary_and_unknown_raw_layout(self):
        for offset, value in ((40+16, 0x21), (40+20, 0x200), (40+24, 1), (24, 0x9000)):
            body = bytearray(fixture())
            struct.pack_into('<I', body, offset, value)
            if offset == 24:
                struct.pack_into('<I', body, 28, 8)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                W['wrap_te'](body)

    def test_reject_overlapping_sections(self):
        body = bytearray(fixture())
        body[4] = 2
        body[80:120] = body[40:80]
        with self.assertRaisesRegex(ValueError, 'Overlapping'):
            W['wrap_te'](body)

    def test_reject_synthetic_header_overlap(self):
        body = bytearray(fixture())
        struct.pack_into('<H', body, 6, 40)
        struct.pack_into('<I', body, 8, 0x94)
        struct.pack_into('<I', body, 40+12, 0x80)
        struct.pack_into('<I', body, 40+20, 0x80)
        body = body[:0xa0]
        with self.assertRaisesRegex(ValueError, 'headers overlap'):
            W['wrap_te'](body)

    def test_detect_byte_address_and_metadata_tampering(self):
        original = fixture()
        wrapped, _ = W['wrap_te'](original)
        for offset in (0x220, 0xb4, 0xa8, 0xac, 0xdc, 0x120, 0x178+8, 0x178+12, 0x178+36):
            altered = bytearray(wrapped)
            altered[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                W['verify_equivalent'](original, altered)

    def test_reject_executable_characteristic(self):
        wrapped, _ = W['wrap_te'](fixture())
        altered = bytearray(wrapped)
        altered[0x96] |= 2
        with self.assertRaisesRegex(ValueError, 'executable-image'):
            W['verify_equivalent'](fixture(), altered)

    def test_reject_rva_in_headers_or_virtual_tail(self):
        te = W['parse_te'](fixture())
        for rva in (0, 0x21f, 0x240, 0x1234589c):
            with self.subTest(rva=rva), self.assertRaises(ValueError):
                W['rva_to_offset'](te, rva, 'te')

    def test_pinned_identity_is_required(self):
        with self.assertRaisesRegex(ValueError, 'original TE identity'):
            W['pinned_wrapper']('core-status-producer', fixture())

    def test_original_producer_with_independent_pefile(self):
        self.verify_original('core-status-producer', 0x09ac6280, 0x190, 33984,
            'ecf94dafb435b1058bf5351960b18910674170bf066e148a9bc73616232c0992')

    def test_original_consumer_with_independent_pefile(self):
        self.verify_original('core-status-consumer', 0x09b07988, 0x180, 21120,
            'c821f23f2ac30abfb9a4b6a93ab2920b9b15a19cab8be836e1409c77b1c635a9')

    def verify_original(self, name, expected_base, expected_delta, expected_size, expected_sha):
        if name not in INPUTS:
            self.skipTest('Pinned original input supplied only by explicit integration arguments')
        import pefile
        body = INPUTS[name].read_bytes()
        wrapped, report = W['pinned_wrapper'](name, body)
        independent = pefile.PE(data=wrapped)
        self.assertEqual(independent.FILE_HEADER.Machine, 0x14c)
        self.assertEqual(independent.OPTIONAL_HEADER.ImageBase, expected_base)
        self.assertEqual(independent.OPTIONAL_HEADER.AddressOfEntryPoint, struct.unpack_from('<I', body, 8)[0])
        self.assertEqual(len(wrapped), expected_size)
        self.assertEqual(hashlib.sha256(wrapped).hexdigest(), expected_sha)
        self.assertEqual(len(independent.sections), body[4])
        self.assertFalse(independent.FILE_HEADER.Characteristics & 2)
        mapped = independent.get_memory_mapped_image()
        compared = 0
        # Parse original TE section records independently of the wrapper parser.
        for index, section in enumerate(independent.sections):
            fields = struct.unpack_from('<8s6I2HI', body, 40+40*index)
            name_bytes, virtual_size, rva, size, raw, _, _, _, _, flags = fields
            original_bytes = body[raw-expected_delta:raw-expected_delta+size]
            self.assertEqual((section.Name, section.Misc_VirtualSize, section.VirtualAddress,
                section.SizeOfRawData, section.Characteristics), (name_bytes, virtual_size, rva, size, flags))
            self.assertEqual(section.get_data(), original_bytes)
            self.assertEqual(mapped[rva:rva+size], original_bytes)
            # Every file-backed byte retains the same RVA and virtual address.
            for index_in_section in range(size):
                new_offset = independent.get_offset_from_rva(rva + index_in_section)
                self.assertEqual(wrapped[new_offset], body[raw-expected_delta+index_in_section])
                self.assertEqual(independent.get_rva_from_offset(new_offset), rva + index_in_section)
            compared += size
        for procedure in report['procedures']:
            rva = int(procedure['rva'], 16)
            new_offset = independent.get_offset_from_rva(rva)
            old_offset = rva-expected_delta
            self.assertEqual(int(procedure['analysis_pe_file_offset'], 16), new_offset)
            self.assertEqual(int(procedure['original_te_file_offset'], 16), old_offset)
            self.assertEqual(int(procedure['virtual_address'], 16), expected_base+rva)
            self.assertEqual(wrapped[new_offset:new_offset+16], body[old_offset:old_offset+16])
        report['independent_parser'] = 'pefile ' + pefile.__version__
        report['independently_compared_file_backed_bytes'] = compared
        report['all_file_backed_rva_mappings_equal'] = True
        ACTUAL_REPORTS.append(report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--producer', type=Path)
    parser.add_argument('--consumer', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if bool(args.producer) != bool(args.consumer):
        parser.error('Supply both pinned original TE images')
    if args.producer:
        INPUTS.update({'core-status-producer':args.producer, 'core-status-consumer':args.consumer})
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(WrapperTests))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(dict(schema=1, hardware_execution=False,
            tests_run=result.testsRun, failures=len(result.failures), errors=len(result.errors),
            skipped=len(result.skipped), passed=result.wasSuccessful(), original_images=ACTUAL_REPORTS),
            indent=2)+'\n', encoding='utf-8')
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
