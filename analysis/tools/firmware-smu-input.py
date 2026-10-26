#!/usr/bin/env python3
"""Extract pinned research inputs; never launch or modify vendor firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile
import zlib

ARCHIVE_SHA = '5a06390b1f9e47baa0868e2949855516cddbba5ebe6dd32fa7141c5e4b404b70'
CAPSULE_SHA = 'ec6e197963c11b44f36b11592c8d097e09aed508980d2d0be2276e244ed2dbe4'
BODY_SHA = 'a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7'
CAPSULE_NAME = 'Pro-WS-TRX50-SAGE-WIFI-ASUS-1317.CAP'
MODULES = {
    '062913EF-0152-4B5A-B9BB-0BF21A09D1D3': ('transport', '16dc54fefb3ac983d3752adbe32db3f38259e239fc89ecec78b18a0912796e60'),
    '31B03837-0853-4B17-A6BB-E0BC2405F901': ('limits', '04f224cf28739fad16b68bb82d0761d2f6ac5e247ad7377ad6e0051cf9405b43'),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def extract_smu(capsule):
    if digest(capsule) != CAPSULE_SHA:
        raise ValueError('Unexpected vendor capsule')
    # PSPTool 3.7 identifies these as duplicate SMU_OFFCHIP_FW directory entries
    # (6,2) and (8,2). Independently check both exact ranges and decompress the
    # body; PSPTool CLI -u truncates this image at the compressed signed length.
    packed = capsule[0x9cc000:0x9cc000 + 123200]
    if packed != capsule[0xc6c000:0xc6c000 + 123200]:
        raise ValueError('SMU duplicate differs')
    u32 = lambda offset: struct.unpack_from('<I', packed, offset)[0]
    if (packed[0x10:0x14] != b'$PS1' or u32(0x18) != 0 or
            u32(0x48) != 1 or u32(0x50) != 262144 or u32(0x60) != 0x732e00):
        raise ValueError('Unexpected SMU header')
    compressed_size = u32(0x54)
    if not 0 < compressed_size <= len(packed) - 0x100 - 0x200:
        raise ValueError('Compressed body exceeds signed input')
    decoder = zlib.decompressobj()
    body = decoder.decompress(packed[0x100:0x100 + compressed_size], 262145)
    if (len(body) != 262144 or not decoder.eof or decoder.unconsumed_tail or
            decoder.unused_data or digest(body) != BODY_SHA):
        raise ValueError('Unexpected decompressed SMU body')
    return body, digest(packed)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--uefi-extract', type=Path)
    args = parser.parse_args()
    if digest(args.archive.read_bytes()) != ARCHIVE_SHA:
        raise ValueError('Unexpected BIOS archive')
    with zipfile.ZipFile(args.archive) as archive:
        entries = [info for info in archive.infolist() if info.filename == CAPSULE_NAME]
        if len(entries) != 1 or entries[0].file_size != 33558528:
            raise ValueError('Expected one pinned capsule')
        capsule = archive.read(entries[0])
    body, packed_sha = extract_smu(capsule)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    capsule_path = output / CAPSULE_NAME
    capsule_path.write_bytes(capsule)
    (output / 'smu-73-2e-body.bin').write_bytes(body)
    manifest = dict(schema=1, archive_sha256=ARCHIVE_SHA, capsule_sha256=CAPSULE_SHA,
        smu_packed_sha256=packed_sha, smu_body_sha256=BODY_SHA, smu_body_bytes=len(body),
        source_url='https://dlcdnets.asus.com/pub/ASUS/mb/BIOS/Pro-WS-TRX50-SAGE-WIFI-ASUS-1317.zip',
        support_url='https://www.asus.com/motherboards-components/motherboards/workstation/pro-ws-trx50-sage-wifi/helpdesk_bios/',
        hardware_execution=False, modules=[])
    if args.uefi_extract:
        # The caller verifies the extractor release hash. Only the extractor is
        # executed, never the capsule, BIOSRenamer, or extracted PE modules.
        subprocess.run([str(args.uefi_extract.resolve()), str(capsule_path), 'unpack'], check=True)
        dump = Path(str(capsule_path) + '.dump')
        for guid, (name, expected) in MODULES.items():
            module = (dump / ('Section_PE32_image_' + guid + '_body.bin')).read_bytes()
            if digest(module) != expected:
                raise ValueError('Unexpected UEFI module: ' + guid)
            (output / ('uefi-' + name + '.bin')).write_bytes(module)
            manifest['modules'].append(dict(guid=guid, name=name, sha256=expected, bytes=len(module)))
    (output / 'input-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('Verified static inputs:', BODY_SHA, len(body))


if __name__ == '__main__':
    main()
