#!/usr/bin/env python3
"""Extract pinned ASUS ProArt X870E-CREATOR WIFI 2503 research inputs only."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile
import zlib

ARCHIVE_SHA = 'fc7211637882a09ccf01059d4ee1681c0a9876e2aff58d57949d903664012d9d'
CAPSULE_SHA = 'f34be92fa4577c976b299495d29f66e437839165cfecf2856d030487880b20a9'
CAPSULE_NAME = 'ProArt-X870E-CREATOR-WIFI-ASUS-2503.CAP'
INPUTS = {
    'smu': ((0x100200, 0x5d0200), 137472, 136953,
            '5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596'),
    'mp5': ((0x184700, 0x654700), 64144, 63621,
            'fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556'),
}
MODULES = {
    '308612BE-4783-4B9B-9E7A-A39244D34929':
        ('transport', '563fbf5d9350c95a9f8a063e4f98a6fbe4d22d3e2bc11d2a3346a4e3320578c9'),
    '69233953-AFD7-40D7-BBEB-CFBCDFC8A646':
        ('transport2', '9ca8b4e78e40f98853163c8414cd61745005ed8d4cd8f7a8463c76e6b16a280e'),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def extract(capsule, name):
    if digest(capsule) != CAPSULE_SHA:
        raise ValueError('Unexpected capsule')
    offsets, packed_size, compressed_size, expected = INPUTS[name]
    packed = capsule[offsets[0]:offsets[0] + packed_size]
    if packed != capsule[offsets[1]:offsets[1] + packed_size]:
        raise ValueError('Firmware duplicates differ')
    u32 = lambda off: struct.unpack_from('<I', packed, off)[0]
    if (packed[0x10:0x14] != b'$PS1' or u32(0x48) != 1 or
            u32(0x50) != 262144 or u32(0x54) != compressed_size or u32(0x60) != 0x625400 or
            compressed_size + 0x100 > len(packed)):
        raise ValueError('Unexpected PSP header')
    decoder = zlib.decompressobj()
    body = decoder.decompress(packed[0x100:0x100 + compressed_size], 262145)
    if (len(body) != 262144 or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail or
            digest(body) != expected):
        raise ValueError('Unexpected decompressed firmware')
    return body, dict(component=name, body_sha256=expected, body_bytes=len(body),
                      capsule_offsets=list(offsets), packed_sha256=digest(packed))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--uefi-extract', type=Path)
    args = parser.parse_args()
    if digest(args.archive.read_bytes()) != ARCHIVE_SHA:
        raise ValueError('Unexpected official BIOS archive')
    with zipfile.ZipFile(args.archive) as archive:
        entries = [info for info in archive.infolist() if info.filename == CAPSULE_NAME]
        if len(entries) != 1 or entries[0].file_size != 33558528:
            raise ValueError('Expected one pinned capsule')
        capsule = archive.read(entries[0])
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = dict(schema=1, archive_sha256=ARCHIVE_SHA, capsule_sha256=CAPSULE_SHA,
        source_url='https://dlcdnets.asus.com/pub/ASUS/mb/BIOS/ProArt-X870E-CREATOR-WIFI-ASUS-2503.ZIP',
        board='ASUS ProArt X870E-CREATOR WIFI', bios='2503', header_version='0x00625400',
        installed_bios_known=False, hardware_execution=False, components=[], modules=[])
    for name in INPUTS:
        body, entry = extract(capsule, name)
        (output / (name + '-62-54-body.bin')).write_bytes(body)
        manifest['components'].append(entry)
    capsule_path = output / CAPSULE_NAME
    capsule_path.write_bytes(capsule)
    if args.uefi_extract:
        # Caller verifies the analysis tool; never execute vendor firmware.
        subprocess.run([str(args.uefi_extract.resolve()), str(capsule_path), 'unpack'], check=True)
        dump = Path(str(capsule_path) + '.dump')
        for guid, (name, expected) in MODULES.items():
            body = (dump / ('Section_PE32_image_' + guid + '_body.bin')).read_bytes()
            if digest(body) != expected:
                raise ValueError('Unexpected UEFI module: ' + guid)
            (output / ('uefi-granite-' + name + '.bin')).write_bytes(body)
            manifest['modules'].append(dict(guid=guid, name=name, bytes=len(body), sha256=expected))
    (output / 'granite-input-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('Verified Granite Ridge reference inputs; no hardware execution')


if __name__ == '__main__':
    main()
