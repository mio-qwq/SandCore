#!/usr/bin/env python3
"""mio：将明确声明的 ELF32 ET_REL 转为 SCL；这是 HOST_BOOTSTRAP 工具。"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import scl_format as scl

SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB = 0, 1, 2, 3
SHT_NOTE, SHT_NOBITS, SHT_REL = 7, 8, 9
SHF_WRITE, SHF_ALLOC, SHF_EXEC = 1, 2, 4
SHF_MERGE, SHF_STRINGS = 0x10, 0x20
SHN_UNDEF, SHN_ABS, SHN_COMMON = 0, 0xFFF1, 0xFFF2
STB_LOCAL, STB_GLOBAL = 0, 1
STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION, STT_FILE = 0, 1, 2, 3, 4
MAX_ELF_SIZE = 64 * 1024 * 1024


class PackError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise PackError(message)


def unsigned(value: object, label: str, nonzero: bool = False) -> int:
    require(type(value) is int and (1 if nonzero else 0) <= value <= 0xFFFFFFFF,
            f"{label}: expected {'nonzero ' if nonzero else ''}u32")
    return value


def exact_fields(item: object, fields: set[str], label: str) -> dict:
    require(isinstance(item, dict), f"{label}: expected an object")
    require(set(item) == fields, f"{label}: required fields are {', '.join(sorted(fields))}")
    return item


def checked_name(value: object, library: bool = False) -> str:
    pattern = scl.LIBRARY_NAME if library else scl.SYMBOL_NAME
    require(isinstance(value, str) and pattern.fullmatch(value) is not None,
            f"invalid {'library' if library else 'symbol'} name: {value!r}")
    return value


def normalize_manifest(source: object) -> dict:
    """清单不猜测符号归属，所有 ABI 与回调字段都必须由作者给出。"""
    item = exact_fields(source, {"name", "abi_major", "abi_minor", "library_version",
                                "exports", "imports", "dependencies", "init", "fini"}, "manifest")
    result = dict(item)
    result["name"] = checked_name(item["name"], True)
    for field in ("abi_major", "abi_minor", "library_version"):
        result[field] = unsigned(item[field], field, field == "abi_major")
    for field in ("init", "fini"):
        require(item[field] is None or isinstance(item[field], str), f"{field}: expected symbol or null")
        if item[field] is not None:
            checked_name(item[field])
    for field in ("exports", "imports", "dependencies"):
        require(isinstance(item[field], list), f"{field}: expected array")
    dependencies = []
    dependency_names = set()
    for index, dep in enumerate(item["dependencies"]):
        d = exact_fields(dep, {"name", "abi_major", "min_minor", "min_version"}, f"dependency {index}")
        checked_name(d["name"], True)
        require(d["name"] not in dependency_names, f"duplicate dependency: {d['name']}")
        require(d["name"] != result["name"], "manifest declares its own library as dependency")
        dependency_names.add(d["name"])
        for field in ("abi_major", "min_minor", "min_version"):
            unsigned(d[field], f"dependency {index} {field}", field == "abi_major")
        dependencies.append(dict(d))
    result["dependencies"] = dependencies
    for field, keys in (("exports", {"name", "kind"}),
                        ("imports", {"name", "kind", "dependency", "size"})):
        entries, names = [], set()
        for index, entry in enumerate(item[field]):
            e = dict(exact_fields(entry, keys, f"{field} {index}"))
            checked_name(e["name"])
            require(e["name"] not in names, f"duplicate {field} symbol: {e['name']}")
            names.add(e["name"])
            require(e["kind"] in ("function", "data"), f"{field} {index}: invalid kind")
            if field == "imports":
                checked_name(e["dependency"], True)
                require(e["dependency"] in dependency_names, f"import {e['name']}: undeclared dependency")
                unsigned(e["size"], f"import {e['name']} size", True)
            entries.append(e)
        result[field] = entries
    require(not ({e["name"] for e in result["exports"]} & {e["name"] for e in result["imports"]}),
            "a symbol cannot be both an export and an import")
    return result


def load_manifest(path: Path) -> dict:
    def unique(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in pairs:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result

    require(path.stat().st_size <= 1024 * 1024, "manifest exceeds 1 MiB")
    return normalize_manifest(json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique))


@dataclass
class Section:
    index: int
    name_offset: int
    kind: int
    flags: int
    address: int
    offset: int
    size: int
    link: int
    info: int
    alignment: int
    entry_size: int
    name: str = ""
    rva: int | None = None


@dataclass
class Symbol:
    index: int
    name: str
    value: int
    size: int
    binding: int
    kind: int
    visibility: int
    section: int


class ElfObject:
    """只读取本工具支持的语义，拒绝需要动态 ELF 链接器的其他类型。"""

    def __init__(self, blob: bytes):
        self.blob = blob
        require(52 <= len(blob) <= MAX_ELF_SIZE, "ELF size outside supported range")
        ident = blob[:16]
        require(ident[:7] == b"\x7fELF\x01\x01\x01", "expected little-endian ELF32 version 1")
        require(ident[7] in (0, 3) and ident[8] == 0 and not any(ident[9:]), "unsupported ELF identification")
        (kind, machine, version, entry, phoff, shoff, flags, hsize, phsize,
         phcount, shsize, shcount, shnames) = struct.unpack_from("<HHIIIIIHHHHHH", blob, 16)
        require((kind, machine, version) == (1, 3, 1), "expected i386 ET_REL; ET_EXEC/ET_DYN are not SCL inputs")
        require(entry == phoff == phcount == phsize == flags == 0,
                "ET_REL must not contain entry, program headers or machine flags")
        require(hsize == 52 and shsize == 40 and 1 < shcount < 0xFF00 and 0 < shnames < shcount,
                "invalid or unsupported extended ELF section table")
        self.range(shoff, shcount * 40, "section table")
        require(shoff >= 52, "section table overlaps ELF header")
        self.sections = [Section(i, *struct.unpack_from("<10I", blob, shoff + i * 40))
                         for i in range(shcount)]
        require(not any(struct.unpack_from("<10I", blob, shoff)), "nonzero ELF null section")
        intervals = [(0, 52, "ELF header"), (shoff, shoff + shcount * 40, "section table")]
        for s in self.sections[1:]:
            require(s.kind in (SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_NOTE, SHT_NOBITS, SHT_REL),
                    f"section {s.index}: unsupported type {s.kind} (RELA/TLS/GROUP/dynamic arrays are unsupported)")
            require(not s.address, f"section {s.index}: ET_REL address must be zero")
            require(s.alignment == 0 or s.alignment & (s.alignment - 1) == 0,
                    f"section {s.index}: alignment is not a power of two")
            require(not (s.flags & (0x200 | 0x400 | 0x800)), "COMDAT, TLS and compressed sections are unsupported")
            if s.kind != SHT_NOBITS:
                self.range(s.offset, s.size, f"section {s.index}")
                if s.size:
                    intervals.append((s.offset, s.offset + s.size, f"section {s.index}"))
        intervals.sort()
        for old, new in zip(intervals, intervals[1:]):
            require(new[0] >= old[1], f"ELF file ranges overlap: {old[2]}, {new[2]}")
        self.section_strings = self.string_table(self.sections[shnames])
        for s in self.sections:
            s.name = self.string(self.section_strings, s.name_offset, "section name")
            if s.flags & SHF_ALLOC:
                require(s.kind in (SHT_PROGBITS, SHT_NOBITS), f"{s.name}: unsupported allocated section type")
                require(not s.flags & ~(SHF_ALLOC | SHF_WRITE | SHF_EXEC | SHF_MERGE | SHF_STRINGS),
                        f"{s.name}: unsupported allocated flags")
                require(s.alignment <= scl.PAGE_SIZE, f"{s.name}: alignment exceeds SCL page size")
                require(not (s.flags & SHF_WRITE and s.flags & SHF_EXEC), f"{s.name}: writable executable section")
                require(s.name not in (".eh_frame", ".gcc_except_table", ".ctors", ".dtors"),
                        f"{s.name}: implicit constructor or exception runtime is unsupported")
                if s.kind == SHT_NOBITS:
                    require(s.flags & SHF_WRITE and not s.flags & SHF_EXEC,
                            f"{s.name}: only writable non-executable BSS is supported")
        symtabs = [s for s in self.sections if s.kind == SHT_SYMTAB]
        require(len(symtabs) == 1, "exactly one ELF symbol table is required (merge objects with ld -r)")
        table = symtabs[0]
        self.symtab_index = table.index
        require(table.entry_size == 16 and table.size >= 16 and table.size % 16 == 0,
                "invalid ELF symbol table entry size")
        require(0 < table.link < shcount, "symbol string table index outside ELF")
        strings = self.string_table(self.sections[table.link])
        count = table.size // 16
        require(0 < table.info <= count, "invalid first global symbol index")
        self.symbols = []
        self.globals = {}
        for index in range(count):
            off, value, size, info, other, section = struct.unpack_from("<IIIBBH", blob, table.offset + index * 16)
            name = self.string(strings, off, f"symbol {index}")
            symbol = Symbol(index, name, value, size, info >> 4, info & 15, other, section)
            if index == 0:
                require(off == value == size == info == other == section == 0, "nonzero ELF null symbol")
            else:
                require(symbol.binding in (STB_LOCAL, STB_GLOBAL), f"{name}: weak/unique symbol binding is unsupported")
                require(symbol.kind in (STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION, STT_FILE),
                        f"{name}: TLS/IFUNC/common symbol type is unsupported")
                require(other <= 3, f"{name}: invalid symbol visibility")
                require((index < table.info) == (symbol.binding == STB_LOCAL), "ELF local/global partition differs")
                require(section != SHN_COMMON and (section < shcount or section == SHN_ABS),
                        f"{name}: COMMON/extended or invalid symbol section (use -fno-common)")
                if 0 < section < shcount:
                    require(value <= self.sections[section].size and size <= self.sections[section].size - value,
                            f"{name}: symbol range outside ELF section")
                if section == SHN_UNDEF:
                    require(symbol.binding == STB_GLOBAL and other == 0 and value == size == 0,
                            f"{name}: unsupported undefined symbol attributes")
                if symbol.binding == STB_GLOBAL:
                    checked_name(name)
                    require(name not in self.globals, f"duplicate ELF global symbol: {name}")
                    require(name not in ("_start", "sc_call"), "SCX start/EXIT bridge must not be linked into a SCL")
                    self.globals[name] = symbol
            self.symbols.append(symbol)

    def range(self, offset: int, size: int, label: str) -> None:
        require(offset <= len(self.blob) and size <= len(self.blob) - offset, f"{label}: file range outside ELF")

    def string_table(self, section: Section) -> bytes:
        require(section.kind == SHT_STRTAB and section.size > 0, "invalid ELF string table")
        data = self.blob[section.offset:section.offset + section.size]
        require(data[0] == data[-1] == 0, "ELF string table must begin and end with NUL")
        return data

    @staticmethod
    def string(table: bytes, offset: int, label: str) -> str:
        # ELF 允许字符串后缀复用，不能把 SCL 的起始边界约束误套到 ELF。
        require(offset < len(table), f"{label}: string offset outside table")
        end = table.find(b"\0", offset)
        require(end >= 0, f"{label}: unterminated string")
        try:
            return table[offset:end].decode("ascii")
        except UnicodeDecodeError as exc:
            raise PackError(f"{label}: non-ASCII name is unsupported") from exc

    def layout(self) -> list[dict]:
        groups: dict[str, list[Section]] = {k: [] for k in ("text", "rodata", "data", "bss")}
        for s in self.sections:
            if s.flags & SHF_ALLOC and s.size:
                group = "bss" if s.kind == SHT_NOBITS else "text" if s.flags & SHF_EXEC else "data" if s.flags & SHF_WRITE else "rodata"
                groups[group].append(s)
        segments, rva = [], 0
        for kind, sections in groups.items():
            if not sections:
                continue
            cursor, payload = 0, bytearray()
            for s in sections:
                offset = scl.align(cursor, max(1, s.alignment))
                require(offset + s.size <= scl.MAX_IMAGE_SIZE - rva, "ELF image exceeds SCL size limit")
                s.rva = rva + offset
                if kind != "bss":
                    payload.extend(bytes(offset - cursor))
                    payload.extend(self.blob[s.offset:s.offset + s.size])
                cursor = offset + s.size
            memory = scl.align(cursor)
            require(memory <= scl.MAX_IMAGE_SIZE - rva, "page-aligned ELF image exceeds SCL size limit")
            flags = scl.SEG_R | (scl.SEG_X if kind == "text" else scl.SEG_W if kind in ("data", "bss") else 0)
            segments.append(dict(rva=rva, mem_size=memory, flags=flags, data=bytes(payload)))
            rva += memory
        require(bool(segments), "ELF has no nonempty allocated sections")
        return segments

    def symbol_rva(self, symbol: Symbol, width: int = 1) -> int:
        require(0 < symbol.section < len(self.sections), f"{symbol.name}: absolute/undefined symbols cannot become an internal target")
        section = self.sections[symbol.section]
        require(section.rva is not None and symbol.value < section.size and width <= section.size - symbol.value,
                f"{symbol.name}: symbol not inside a retained allocated section")
        return section.rva + symbol.value

    def function(self, name: str) -> Symbol:
        symbol = self.globals.get(name)
        require(symbol is not None and symbol.kind == STT_FUNC and symbol.visibility == 0,
                f"{name}: callback/export needs a defined visible function")
        self.symbol_rva(symbol, max(1, symbol.size))
        require(self.sections[symbol.section].flags & SHF_EXEC, f"{name}: function is not executable")
        return symbol


def pack_elf(blob: bytes, manifest: dict) -> bytes:
    """将 ELF REL 的原始四字节加数抽出；正文置零，不进行固定基址预链接。"""
    m = normalize_manifest(manifest)
    elf = ElfObject(blob)
    segments = elf.layout()
    undefined = {s.name for s in elf.symbols[1:] if s.section == SHN_UNDEF}
    imports = m["imports"]
    declared = {i["name"] for i in imports}
    require(undefined == declared,
            f"undefined/import mismatch: undeclared={sorted(undefined-declared)}, unused={sorted(declared-undefined)}")
    import_index = {i["name"]: index for index, i in enumerate(imports)}
    exports = []
    for item in m["exports"]:
        symbol = elf.globals.get(item["name"])
        require(symbol is not None and symbol.visibility == 0 and symbol.size > 0,
                f"{item['name']}: export needs a defined visible symbol with nonzero ELF size")
        expected = STT_FUNC if item["kind"] == "function" else STT_OBJECT
        require(symbol.kind == expected, f"{symbol.name}: export kind conflicts with ELF symbol")
        rva = elf.symbol_rva(symbol, symbol.size)
        require(bool(elf.sections[symbol.section].flags & SHF_EXEC) == (expected == STT_FUNC),
                f"{symbol.name}: export kind conflicts with section access")
        exports.append(dict(name=symbol.name, rva=rva, size=symbol.size, kind=item["kind"]))
    callbacks = {}
    for field in ("init", "fini"):
        callbacks[field + "_rva"] = scl.NO_ENTRY if m[field] is None else elf.symbol_rva(elf.function(m[field]))
    relocations = []
    touched = set()
    for table in elf.sections:
        if table.kind != SHT_REL:
            continue
        require(table.entry_size == 8 and table.size % 8 == 0, f"{table.name}: invalid REL table size")
        require(table.link == elf.symtab_index and 0 < table.info < len(elf.sections), f"{table.name}: invalid relocation link/target")
        target_section = elf.sections[table.info]
        # 非装载调试节的重定位仍检查索引/范围，但不会进入客体映像。
        for pos in range(table.offset, table.offset + table.size, 8):
            offset, info = struct.unpack_from("<2I", blob, pos)
            index, kind = info >> 8, info & 255
            require(0 < index < len(elf.symbols), f"{table.name}: invalid relocation symbol index")
            require(offset <= target_section.size and 4 <= target_section.size - offset,
                    f"{table.name}: relocation patch outside target section")
            if not target_section.flags & SHF_ALLOC:
                continue
            require(target_section.rva is not None and target_section.kind == SHT_PROGBITS,
                    f"{table.name}: relocation needs a retained file-backed patch")
            require(kind in (1, 2), f"{table.name}: unsupported i386 relocation {kind}; only R_386_32/PC32 are supported")
            patch = target_section.rva + offset
            require(not any(p in touched for p in range(patch, patch + 4)), "overlapping ELF relocations")
            touched.update(range(patch, patch + 4))
            addend = struct.unpack_from("<i", blob, target_section.offset + offset)[0]
            symbol = elf.symbols[index]
            if symbol.section == SHN_UNDEF:
                import_item = imports[import_index[symbol.name]]
                if symbol.kind in (STT_FUNC, STT_OBJECT):
                    require(import_item["kind"] == ("function" if symbol.kind == STT_FUNC else "data"),
                            f"{symbol.name}: import kind conflicts with ELF symbol")
                target_kind, target = scl.IMPORT, import_index[symbol.name]
            else:
                target_kind, target = scl.INTERNAL, elf.symbol_rva(symbol)
            relocations.append(dict(patch_rva=patch, type=kind, target_kind=target_kind, target=target, addend=addend))
    # 每个目标段只复制一次，再将 REL 位置清零；避免逐重定位复制整段。
    buffers = [bytearray(s["data"]) for s in segments]
    for r in relocations:
        for index, segment in enumerate(segments):
            relative = r["patch_rva"] - segment["rva"]
            if 0 <= relative <= len(buffers[index]) - 4:
                buffers[index][relative:relative + 4] = b"\0\0\0\0"
                break
    for s, payload in zip(segments, buffers):
        s["data"] = bytes(payload)
    return scl.build_scl(name=m["name"], segments=segments, exports=exports, imports=imports,
                         relocations=relocations, dependencies=m["dependencies"],
                         abi_major=m["abi_major"], abi_minor=m["abi_minor"],
                         library_version=m["library_version"], **callbacks)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="HOST_BOOTSTRAP: ELF32 i386 ET_REL -> SandCore SCL v1")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args(argv)
    temporary = None
    try:
        manifest = load_manifest(args.manifest)
        require(args.output.name == manifest["name"], "output filename must equal manifest library name")
        require(args.output.resolve() not in (args.input.resolve(), args.manifest.resolve()), "output overlaps source input")
        require(args.input.stat().st_size <= MAX_ELF_SIZE, "ELF input exceeds 64 MiB")
        blob = pack_elf(args.input.read_bytes(), manifest)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix=".mkscl-", delete=False) as stream:
            temporary = stream.name
            stream.write(blob)
        os.replace(temporary, args.output)
        temporary = None
        parsed = scl.validate(blob)
        print(f"HOST_BOOTSTRAP {parsed['name']}: {len(blob)} bytes, {len(parsed['exports'])} exports, "
              f"{len(parsed['imports'])} imports, {len(parsed['relocations'])} relocations")
        return 0
    except (OSError, ValueError) as exc:
        print(f"mkscl: {exc}", file=sys.stderr)
        return 1
    finally:
        if temporary is not None:
            os.unlink(temporary)


if __name__ == "__main__":
    raise SystemExit(main())
