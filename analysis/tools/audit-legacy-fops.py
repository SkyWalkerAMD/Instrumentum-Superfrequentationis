#!/usr/bin/env python3
"""Read pinned old KO objects and DWARF; never load a module or call ioctl."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import zipfile

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def inspect(data, digest, audit):
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError("module hash differs from pinned original")
    members = {"read", "write", "unlocked_ioctl", "compat_ioctl", "mmap", "open", "release"}
    evidence = audit.module_evidence(data, re.compile(r"(?!)"), {"file_operations": members})
    elf = ELFFile(io.BytesIO(data))
    objects = [s for s in elf.get_section_by_name(".symtab").iter_symbols() if s.name == "fops"]
    if len(objects) != 1 or objects[0]["st_info"]["type"] != "STT_OBJECT":
        raise ValueError("expected one fops object")
    obj = objects[0]
    section = elf.get_section(obj["st_shndx"])
    start, size = obj["st_value"], obj["st_size"]
    raw = section.data()[start:start + size]
    if section["sh_type"] != "SHT_PROGBITS" or len(raw) != size:
        raise ValueError("fops is not a complete file-backed object")
    offsets = {}
    for field in evidence["layout_fields"]:
        key, off = field["member"], field["offset"]
        if key in offsets and offsets[key] != off:
            raise ValueError("conflicting DWARF layouts")
        offsets[key] = off
    if set(offsets) != members:
        raise ValueError("missing selected file_operations DWARF members")
    relocations = []
    for relsec in elf.iter_sections():
        if not isinstance(relsec, RelocationSection) or relsec["sh_info"] != obj["st_shndx"]:
            continue
        symbols = elf.get_section(relsec["sh_link"])
        for rel in relsec.iter_relocations():
            if start <= rel["r_offset"] < start + size:
                symbol = symbols.get_symbol(rel["r_info_sym"])
                name = symbol.name
                if not name and isinstance(symbol["st_shndx"], int):
                    name = elf.get_section(symbol["st_shndx"]).name
                relocations.append({"object_offset": rel["r_offset"] - start,
                                    "type": rel["r_info_type"], "symbol": name,
                                    "addend": rel["r_addend"] if rel.is_RELA() else None})
    fields = []
    for name, off in sorted(offsets.items(), key=lambda x: x[1]):
        if off < 0 or off + 8 > size:
            raise ValueError("member outside object")
        rels = [r for r in relocations if off - 7 <= r["object_offset"] < off + 8]
        null = raw[off:off + 8] == bytes(8) and not rels
        if name in ("unlocked_ioctl", "compat_ioctl"):
            if not null:
                raise ValueError("ioctl slot is not null: " + name)
        elif len(rels) != 1 or rels[0]["object_offset"] != off or rels[0]["type"] != 1:
            raise ValueError("expected one R_X86_64_64 callback relocation: " + name)
        fields.append({"member": name, "offset": off, "stored_bytes": raw[off:off + 8].hex(),
                       "relocations": rels, "null_without_relocation": null})
    return {"sha256": digest, "vermagic": evidence["vermagic"],
            "fops_section": section.name, "fops_offset": start, "fops_size": size,
            "fops_bytes_sha256": hashlib.sha256(raw).hexdigest(), "fields": fields}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("module_zip", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("mailbox_audit", Path(__file__).with_name("audit-legacy-mailbox.py"))
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    result = {"schema": 1, "scope": "static fops object fields by DWARF and relocations; no kernel/module execution",
              "interpretation": "Both ioctl slots are zero with no relocation in each pinned fops object; this is not a runtime ENOTTY observation.",
              "modules": {}}
    with zipfile.ZipFile(args.module_zip) as archive:
        for name, digest in audit.MODULES.items():
            matches = [item for item in archive.namelist() if Path(item).name == name]
            if len(matches) != 1:
                raise ValueError("expected exactly one " + name)
            result["modules"][name] = inspect(archive.read(matches[0]), digest, audit)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(result, indent=2) + "\n").encode("utf-8"))
    print("3 pinned old fops objects: ioctl slots null by DWARF and relocation checks; no execution")


if __name__ == "__main__":
    main()
