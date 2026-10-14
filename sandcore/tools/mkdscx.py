#!/usr/bin/env python3
"""mio：自写HOST_BOOTSTRAP REL链接器，生成自动依赖的旧SCX1容器。"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import tempfile
from pathlib import Path

import mkscl as elf32
import scd_format as scd
import scl_format as scl

PackError = elf32.PackError
require = elf32.require


def normalize_manifest(source: object) -> dict:
    item = elf32.exact_fields(source, {"kind", "name", "dependencies", "imports"}, "application manifest")
    require(item["kind"] == "application", "application manifest kind must be application")
    require(isinstance(item["name"], str) and scd.APP_NAME.fullmatch(item["name"]),
            "application name must be an uppercase SCX basename")
    require(isinstance(item["dependencies"], list) and isinstance(item["imports"], list),
            "dependencies/imports must be arrays")
    deps, names = [], set()
    for i, source_dep in enumerate(item["dependencies"]):
        dep = dict(elf32.exact_fields(source_dep, {"name", "abi_major", "min_minor", "min_version"}, f"dependency {i}"))
        elf32.checked_name(dep["name"], True)
        require(dep["name"] not in names, f"duplicate dependency: {dep['name']}")
        names.add(dep["name"])
        for field in ("abi_major", "min_minor", "min_version"):
            elf32.unsigned(dep[field], f"dependency {i} {field}", field == "abi_major")
        deps.append(dep)
    imports, symbol_names = [], set()
    for i, source_import in enumerate(item["imports"]):
        entry = dict(elf32.exact_fields(source_import, {"name", "kind", "dependency", "size"}, f"import {i}"))
        elf32.checked_name(entry["name"])
        elf32.checked_name(entry["dependency"], True)
        require(entry["name"] not in symbol_names, f"duplicate import: {entry['name']}")
        symbol_names.add(entry["name"])
        require(entry["kind"] in ("function", "data"), f"import {i}: kind must be function or data")
        require(entry["dependency"] in names, f"import {entry['name']}: undeclared dependency")
        elf32.unsigned(entry["size"], f"import {entry['name']} size", True)
        imports.append(entry)
    return dict(kind="application", name=item["name"], dependencies=deps, imports=imports)


def load_manifest(path: Path) -> dict:
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result

    require(path.stat().st_size <= 1024 * 1024, "manifest exceeds 1 MiB")
    return normalize_manifest(json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique))


def application_layout(elf: elf32.ElfObject) -> tuple[bytearray, int, int, list[elf32.Section]]:
    """本程序正文只排一次；BSS暂存相对偏移，待完整SCD尺寸确定再加load。"""
    groups = {name: [] for name in ("text", "rodata", "data", "bss")}
    for s in elf.sections:
        if not s.flags & elf32.SHF_ALLOC or not s.size:
            continue
        group = ("bss" if s.kind == elf32.SHT_NOBITS else "text" if s.flags & elf32.SHF_EXEC
                 else "data" if s.flags & elf32.SHF_WRITE else "rodata")
        groups[group].append(s)
    require(bool(groups["text"]), "application has no machine code")
    payload = bytearray(scd.CRT_SIZE)
    for group in ("text", "rodata", "data"):
        for s in groups[group]:
            offset = scl.align(len(payload), max(1, s.alignment))
            require(s.size <= scd.MAX_SCX_LOAD - offset, "initialized application exceeds SCX1 capacity")
            s.rva = offset
            payload.extend(bytes(offset - len(payload)))
            payload.extend(elf.blob[s.offset:s.offset + s.size])
    bss_size, alignment = 0, 1
    for s in groups["bss"]:
        boundary = max(1, s.alignment)
        alignment = max(alignment, boundary)
        s.rva = scl.align(bss_size, boundary)
        require(s.size <= scd.MAX_PROGRAM_IMAGE - s.rva, "application BSS exceeds private address region")
        bss_size = s.rva + s.size
    return payload, bss_size, alignment, groups["bss"]


def pack_elf(blob: bytes, manifest: dict) -> bytes:
    """真实解析i386 REL并链接本程序内部地址，不读取或复制依赖SCL正文。"""
    m = normalize_manifest(manifest)
    elf = elf32.ElfObject(blob)
    payload, bss_size, alignment, bss_sections = application_layout(elf)
    main = elf.function("main")
    require(main.size > 0 and main.binding == elf32.STB_GLOBAL, "application needs a defined visible main function")
    main_rva = elf.symbol_rva(main, main.size)
    undefined = {s.name for s in elf.symbols[1:] if s.section == elf32.SHN_UNDEF}
    declared = {i["name"] for i in m["imports"]}
    require(undefined == declared,
            f"undefined/import mismatch: undeclared={sorted(undefined-declared)}, unused={sorted(declared-undefined)}")
    indices = {i["name"]: n for n, i in enumerate(m["imports"])}
    external, internal, touched, used = [], [], set(), set()
    for table in elf.sections:
        if table.kind != elf32.SHT_REL:
            continue
        require(table.entry_size == 8 and table.size % 8 == 0, f"{table.name}: invalid REL table size")
        require(table.link == elf.symtab_index and 0 < table.info < len(elf.sections),
                f"{table.name}: invalid REL link/target")
        section = elf.sections[table.info]
        for pos in range(table.offset, table.offset + table.size, 8):
            offset, info = struct.unpack_from("<2I", blob, pos)
            symbol_index, kind = info >> 8, info & 255
            require(0 < symbol_index < len(elf.symbols), f"{table.name}: invalid relocation symbol index")
            require(offset <= section.size and 4 <= section.size - offset,
                    f"{table.name}: relocation patch outside target section")
            if not section.flags & elf32.SHF_ALLOC:
                continue
            require(section.rva is not None and section.kind == elf32.SHT_PROGBITS,
                    f"{table.name}: relocation requires a retained initialized patch")
            require(kind in (scd.ABS32, scd.PC32), f"{table.name}: unsupported i386 relocation {kind}")
            patch = section.rva + offset
            require(not any(p in touched for p in range(patch, patch + 4)), "overlapping application relocations")
            touched.update(range(patch, patch + 4))
            addend = struct.unpack_from("<i", blob, section.offset + offset)[0]
            symbol = elf.symbols[symbol_index]
            if symbol.section == elf32.SHN_UNDEF:
                entry = m["imports"][indices[symbol.name]]
                if symbol.kind in (elf32.STT_FUNC, elf32.STT_OBJECT):
                    require(entry["kind"] == ("function" if symbol.kind == elf32.STT_FUNC else "data"),
                            f"{symbol.name}: import kind conflicts with ELF")
                used.add(symbol.name)
                external.append(dict(patch_rva=patch, type=kind, import_index=indices[symbol.name], addend=addend))
                struct.pack_into("<I", payload, patch, 0)
            else:
                internal.append((patch, kind, symbol, addend))
    require(used == declared, f"manifest imports are not referenced by retained code/data: {sorted(declared-used)}")
    metadata = scd.build_metadata(metadata_rva=len(payload), program_bss_bytes=bss_size,
                                  main_rva=main_rva, dependencies=m["dependencies"], imports=m["imports"],
                                  relocations=external, bss_alignment=alignment)
    load = len(payload) + len(metadata)
    for s in bss_sections:
        s.rva += load
    for patch, kind, symbol, addend in internal:
        value = scd.PROGRAM_BASE + elf.symbol_rva(symbol) + addend
        if kind == scd.PC32:
            value -= scd.PROGRAM_BASE + patch
        struct.pack_into("<I", payload, patch, value & 0xFFFFFFFF)
    return scd.pack_scx(bytes(payload), metadata)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="HOST_BOOTSTRAP: ELF32 i386 ET_REL -> automatically linked dynamic SCX1")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--json", action="store_true", help="print validated dynamic tables")
    args = parser.parse_args(argv)
    temporary = None
    try:
        manifest = load_manifest(args.manifest)
        require(args.output.name == manifest["name"], "output filename differs from application manifest")
        require(args.output.resolve() not in (args.input.resolve(), args.manifest.resolve()), "output overlaps input")
        require(args.input.stat().st_size <= elf32.MAX_ELF_SIZE, "ELF input exceeds 64 MiB")
        result = pack_elf(args.input.read_bytes(), manifest)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix=".mkdscx-", delete=False) as stream:
            temporary = stream.name
            stream.write(result)
        os.replace(temporary, args.output)
        temporary = None
        parsed = scd.validate_scx(result)
        if args.json:
            print(json.dumps(dict(source="HOST_BOOTSTRAP", name=manifest["name"], **parsed), indent=2))
        else:
            print(f"HOST_BOOTSTRAP {manifest['name']}: {len(result)} bytes, "
                  f"{len(parsed['dependencies'])} dependencies, {len(parsed['imports'])} imports, "
                  f"{len(parsed['relocations'])} dynamic relocations")
        return 0
    except (OSError, ValueError) as exc:
        print(f"mkdscx: {exc}", file=sys.stderr)
        return 1
    finally:
        if temporary is not None:
            os.unlink(temporary)


if __name__ == "__main__":
    raise SystemExit(main())
