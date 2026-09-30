#!/usr/bin/env python3
"""Read actual ELF64 DT_NEEDED and version requirements; never execute input.

The release policy is the EL8 system ABI. Test native target builds separately.
This parser also runs on Windows, so an Ubuntu binary cannot be mistaken for
an EL8 release merely because the cross-distro build environment is absent.
"""
import argparse
import json
import re
import struct
from pathlib import Path

LIMITS = {"GLIBC": (2, 28), "GLIBCXX": (3, 4, 25), "CXXABI": (1, 3, 11)}
FORBIDDEN = re.compile(r"^(libQt5|libicu|libjpeg\.so|libtiff(?:xx)?\.so|libwebp(?:demux|mux)?\.so|libopencv)")


def elf_info(path):
    data = Path(path).read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("expected little-endian ELF64")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    if header[2] != 62:
        raise ValueError("expected x86-64 ELF")
    program_offset, program_size, program_count = header[5], header[9], header[10]
    if program_count and (program_size != 56 or program_count == 0xffff):
        raise ValueError("unsupported ELF program table")
    if program_offset + program_size * program_count > len(data):
        raise ValueError("truncated ELF program table")
    stack_flags = []
    for i in range(program_count):
        program = struct.unpack_from("<IIQQQQQQ", data, program_offset + i * program_size)
        if program[0] == 0x6474e551:  # PT_GNU_STACK; PF_X = 1
            stack_flags.append(program[1])
    offset, size, count = header[6], header[11], header[12]
    if size != 64 or count == 0:
        raise ValueError("missing or unsupported ELF section table")
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, offset + i * size)
                for i in range(count)]

    def section(s):
        start, length = s[4:6]
        if start + length > len(data):
            raise ValueError("truncated ELF section")
        return data[start:start + length]

    def string(table, pos):
        if pos >= len(table):
            raise ValueError("invalid ELF string offset")
        end = table.find(b"\0", pos)
        if end < 0:
            raise ValueError("unterminated ELF string")
        return table[pos:end].decode("ascii")

    needed, versions, paths = [], set(), []
    for s in sections:
        if s[1] not in (6, 0x6ffffffe):  # SHT_DYNAMIC / SHT_GNU_verneed
            continue
        table, block = section(sections[s[6]]), section(s)
        if s[1] == 6:
            for pos in range(0, len(block), 16):
                tag, value = struct.unpack_from("<qQ", block, pos)
                if tag == 0:
                    break
                if tag == 1:
                    needed.append(string(table, value))
                if tag in (15, 29):
                    paths.extend(string(table, value).split(":"))
        else:
            pos, visited = 0, set()
            while pos < len(block):
                if pos in visited:
                    raise ValueError("cyclic version requirements")
                visited.add(pos)
                ver, num, _, aux, nxt = struct.unpack_from("<HHIII", block, pos)
                if ver != 1:
                    raise ValueError("unsupported version requirements")
                cursor = pos + aux
                for _ in range(num):
                    _, _, _, name, step = struct.unpack_from("<IHHII", block, cursor)
                    versions.add(string(table, name))
                    cursor += step
                if nxt == 0:
                    break
                pos += nxt
    if not needed:
        raise ValueError("expected dynamically linked glibc with static Qt")
    return {"needed": sorted(needed), "versions": sorted(versions), "rpaths": paths,
            "gnu_stack_flags": stack_flags}


def violations(info):
    errors = []
    stacks = info.get("gnu_stack_flags", [])
    if len(stacks) != 1:
        errors.append("expected exactly one PT_GNU_STACK declaration")
    elif stacks[0] & 1:
        errors.append("executable GNU_STACK is not allowed in the portable GUI")
    for requirement in info["versions"]:
        for namespace, limit in LIMITS.items():
            if requirement.startswith(namespace + "_"):
                suffix = requirement[len(namespace) + 1:]
                if not re.fullmatch(r"\d+(\.\d+)+", suffix):
                    errors.append("unsupported requirement " + requirement)
                elif tuple(map(int, suffix.split("."))) > limit:
                    errors.append("above EL8 floor: " + requirement)
    for library in info["needed"]:
        if FORBIDDEN.match(library):
            errors.append("unbundled or nonstatic dependency: " + library)
        if "/" in library:
            errors.append("absolute/path dependency: " + library)
    for path in info["rpaths"]:
        if path and path not in ("$ORIGIN", "$ORIGIN/../lib"):
            errors.append("nonportable RPATH/RUNPATH: " + path)
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--inspect", action="store_true", help="report without release policy")
    args = parser.parse_args()
    try:
        info = elf_info(args.binary)
        errors = [] if args.inspect else violations(info)
    except (ValueError, IndexError, struct.error, OSError) as exc:
        parser.exit(2, str(exc) + "\n")
    print(json.dumps({"file": str(args.binary), **info, "errors": errors}, indent=2))
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
