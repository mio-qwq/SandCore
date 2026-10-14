"""mio：SCX初始化载荷尾部的SCD1；字节合同与freestanding C验证器相同。"""

from __future__ import annotations

import re
import struct
from typing import Any

import scl_format as scl

MAGIC = b"SCD1MIO\0"
HEADER_SIZE, VERSION, CRT_SIZE = 64, 1, 64
PROGRAM_BASE, MAX_PROGRAM_IMAGE, MAX_SCX_LOAD = 0x400000, 0x3D0000, 262108
SCX_MAGIC, SCX_HEADER_SIZE = b"SCX1MIO\0", 36
APP_NAME = re.compile(r"[A-Z0-9_]{1,31}\.SCX\Z", re.ASCII)
FUNCTION, DATA, ABS32, PC32 = 1, 2, 1, 2
E_FORMAT, E_RANGE, E_SYMBOL, E_VERSION = -1, -2, -3, -4


class ScdError(ValueError):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code


def require(condition: bool, code: int, message: str) -> None:
    if not condition:
        raise ScdError(code, message)


def u32(value: object, label: str = "integer") -> int:
    require(type(value) is int and 0 <= value <= 0xFFFFFFFF, E_RANGE, f"{label}: expected u32")
    return value


def validate(blob: bytes | bytearray | memoryview, metadata_rva: int) -> dict[str, Any]:
    """只验证SCD字节；调用者核对program_load/BSS和实际SCX映像相等。"""
    data = memoryview(blob).cast("B")
    size = len(data)
    require(HEADER_SIZE <= size <= MAX_PROGRAM_IMAGE, E_RANGE, "metadata size outside SCD1 limits")
    require(data[:8] == MAGIC, E_FORMAT, "bad SCD1 magic")
    (version, header_size, metadata_size, load, bss, main, dep_off, dep_count,
     imp_off, imp_count, rel_off, rel_count, str_off, str_size) = struct.unpack_from("<14I", data, 8)
    require(version == VERSION, E_VERSION, "unsupported SCD1 version")
    require(header_size == HEADER_SIZE, E_FORMAT, "noncanonical SCD1 header")
    u32(metadata_rva, "metadata RVA")
    require(metadata_size == size and 0 < load <= MAX_PROGRAM_IMAGE
            and bss <= MAX_PROGRAM_IMAGE - load and metadata_rva <= load
            and size == load - metadata_rva and CRT_SIZE <= main < metadata_rva,
            E_RANGE, "program size, main or metadata tail is invalid")
    cursor = HEADER_SIZE
    for offset, count, width, label in ((dep_off, dep_count, 20, "dependencies"),
                                       (imp_off, imp_count, 16, "imports"),
                                       (rel_off, rel_count, 16, "relocations")):
        require(offset == cursor and count <= (size - cursor) // width,
                E_RANGE, f"{label}: noncanonical table or multiplication overflow")
        cursor += count * width
    require(str_off == cursor and 0 < str_size == size - cursor,
            E_RANGE, "strings must occupy exact metadata tail")
    strings = bytes(data[str_off:])
    require(strings[-1] == 0, E_SYMBOL, "string table must end with NUL")

    def name(offset: int, library: bool = False) -> str:
        require(offset < str_size and (offset == 0 or strings[offset - 1] == 0),
                E_SYMBOL, "name offset is not a NUL string start")
        end = strings.find(b"\0", offset, offset + (36 if library else 128))
        require(end >= 0, E_SYMBOL, "unterminated or overlong name")
        try:
            text = strings[offset:end].decode("ascii")
        except UnicodeDecodeError as exc:
            raise ScdError(E_SYMBOL, "names must be ASCII") from exc
        require(bool((scl.LIBRARY_NAME if library else scl.SYMBOL_NAME).fullmatch(text)),
                E_SYMBOL, "invalid library or symbol name")
        return text

    dependencies, previous = [], ""
    for i in range(dep_count):
        offset, major, minor, release, flags = struct.unpack_from("<5I", data, dep_off + i * 20)
        text = name(offset, True)
        require(major != 0, E_VERSION, "dependency ABI major must be nonzero")
        require(flags == 0, E_FORMAT, "unsupported dependency flags")
        require(text > previous, E_SYMBOL, "dependencies must have unique sorted names")
        previous = text
        dependencies.append(dict(name=text, abi_major=major, min_minor=minor,
                                 min_version=release, flags=flags))
    imports, previous_key = [], (-1, "")
    for i in range(imp_count):
        offset, dep, kind, minimum = struct.unpack_from("<4I", data, imp_off + i * 16)
        text = name(offset)
        require(minimum > 0 and kind in (FUNCTION, DATA), E_SYMBOL, "invalid import kind or minimum size")
        require(dep < dep_count, E_RANGE, "import dependency index outside table")
        key = (dep, text)
        require(key > previous_key, E_SYMBOL, "imports must be uniquely sorted by dependency and name")
        previous_key = key
        imports.append(dict(name=text, dep_index=dep, dependency=dependencies[dep]["name"],
                            kind=kind, size=minimum))
    relocations, previous_end = [], 0
    for i in range(rel_count):
        patch, kind, target, addend = struct.unpack_from("<4I", data, rel_off + i * 16)
        require(metadata_rva >= 4 and max(CRT_SIZE, previous_end) <= patch <= metadata_rva - 4 and target < imp_count,
                E_RANGE, "relocation overlaps, targets metadata or has invalid import index")
        require(kind in (ABS32, PC32), E_FORMAT, "unsupported SCD1 relocation type")
        previous_end = patch + 4
        relocations.append(dict(patch_rva=patch, type=kind, import_index=target,
                                addend=addend if addend < 0x80000000 else addend - 0x100000000))
    return dict(format="SCD1MIO", version=version, metadata_rva=metadata_rva, metadata_size=size,
                program_load_bytes=load, program_bss_bytes=bss, main_rva=main,
                dependencies=dependencies, imports=imports, relocations=relocations,
                strings_offset=str_off, strings_size=str_size)


def build_metadata(*, metadata_rva: int, program_bss_bytes: int, main_rva: int,
                   dependencies=(), imports=(), relocations=(), bss_alignment: int = 1) -> bytes:
    """重定位import_index是原imports列表下标，排序时显式重映射。"""
    deps = sorted((dict(d) for d in dependencies), key=lambda d: d["name"])
    dep_index = {d["name"]: i for i, d in enumerate(deps)}
    imps = []
    for index, source in enumerate(imports):
        item = dict(source)
        require(item["dependency"] in dep_index, E_SYMBOL, "import has no declared dependency")
        item["dep_index"], item["original_index"] = dep_index[item["dependency"]], index
        imps.append(item)
    imps.sort(key=lambda i: (i["dep_index"], i["name"]))
    remap = {i["original_index"]: n for n, i in enumerate(imps)}
    rels = sorted((dict(r) for r in relocations), key=lambda r: r["patch_rva"])
    for r in rels:
        require(r["import_index"] in remap, E_RANGE, "invalid builder import index")
        r["import_index"] = remap[r["import_index"]]
    # 空清单也保留一个NUL；相同文本可以共用完整起点，不复用后缀。
    strings, offsets = bytearray(b"\0"), {}

    def intern(text: str) -> int:
        require(isinstance(text, str), E_SYMBOL, "name must be text")
        if text not in offsets:
            try:
                raw = text.encode("ascii")
            except UnicodeEncodeError as exc:
                raise ScdError(E_SYMBOL, "name must be ASCII") from exc
            require(b"\0" not in raw, E_SYMBOL, "name contains NUL")
            offsets[text] = len(strings)
            strings.extend(raw + b"\0")
        return offsets[text]

    dep_words = [(intern(d["name"]), u32(d["abi_major"]), u32(d.get("min_minor", 0)),
                  u32(d.get("min_version", 0)), u32(d.get("flags", 0))) for d in deps]
    imp_words = [(intern(i["name"]), i["dep_index"], scl.kind_value(i["kind"]), u32(i["size"])) for i in imps]
    dep_off = HEADER_SIZE
    imp_off = dep_off + len(deps) * 20
    rel_off = imp_off + len(imps) * 16
    str_off = rel_off + len(rels) * 16
    require(type(bss_alignment) is int and 1 <= bss_alignment <= 4096
            and bss_alignment & (bss_alignment - 1) == 0, E_RANGE, "unsupported BSS alignment")
    strings.extend(bytes(-(metadata_rva + str_off + len(strings)) % bss_alignment))
    size = str_off + len(strings)
    require(size <= MAX_PROGRAM_IMAGE, E_RANGE, "metadata exceeds SCD1 limit")
    header = (VERSION, HEADER_SIZE, size, u32(metadata_rva + size), u32(program_bss_bytes),
              u32(main_rva), dep_off, len(deps), imp_off, len(imps), rel_off, len(rels), str_off, len(strings))
    output = bytearray(MAGIC + struct.pack("<14I", *header))
    for row in dep_words:
        output.extend(struct.pack("<5I", *row))
    for row in imp_words:
        output.extend(struct.pack("<4I", *row))
    for r in rels:
        addend = r.get("addend", 0)
        require(type(addend) is int and -0x80000000 <= addend <= 0x7FFFFFFF,
                E_RANGE, "relocation addend must be signed 32-bit")
        output.extend(struct.pack("<4I", u32(r["patch_rva"]), u32(r["type"]),
                                  u32(r["import_index"]), addend & 0xFFFFFFFF))
    output.extend(strings)
    result = bytes(output)
    validate(result, metadata_rva)
    return result


def crt(metadata_va: int) -> bytes:
    """正式工具生成的私有64B启动桥；SYS0x283 EBX=bootinfo, ECX=descriptor。"""
    raw = bytearray.fromhex(
        "83 ec 20 89 e3 b9 00 00 00 00 b8 83 02 00 00 cd 7c 85 c0 78 1c "
        "8b 44 24 0c 89 e2 83 ec 08 52 68 00 00 00 00 ff d0 83 c4 30 "
        "89 c3 31 c0 cd 7c eb fe 83 c4 20 bb 01 00 00 00 31 c0 cd 7c eb fe 90")
    struct.pack_into("<I", raw, 6, u32(metadata_va))
    struct.pack_into("<I", raw, 32, metadata_va)
    return bytes(raw)


def pack_scx(payload: bytes, metadata: bytes, stack_size: int = 131072) -> bytes:
    """只装入本程序正文和SCD；依赖库正文保持独立文件。"""
    parsed = validate(metadata, len(payload))
    require(CRT_SIZE <= len(payload) and parsed["program_load_bytes"] <= MAX_SCX_LOAD,
            E_RANGE, "dynamic SCX load outside legacy loader capacity")
    require(4096 <= u32(stack_size) <= 131072 and stack_size % 4096 == 0,
            E_RANGE, "stack outside SCX1 contract")
    raw = bytearray(payload)
    raw[:CRT_SIZE] = crt(PROGRAM_BASE + len(payload))
    result = (SCX_MAGIC + struct.pack("<7I", 0, len(raw) + len(metadata), parsed["program_bss_bytes"],
                                     stack_size, 0, PROGRAM_BASE, 0x004F494D) + raw + metadata)
    validate_scx(result)
    return result


def validate_scx(blob: bytes) -> dict[str, Any]:
    require(SCX_HEADER_SIZE + CRT_SIZE <= len(blob), E_RANGE, "SCX container is too short")
    require(blob[:8] == SCX_MAGIC, E_FORMAT, "bad SCX1 magic")
    entry, load, bss, stack, flags, base, author = struct.unpack_from("<7I", blob, 8)
    require(entry == flags == 0 and base == PROGRAM_BASE and author == 0x004F494D,
            E_FORMAT, "dynamic output must preserve canonical fixed SCX1 container")
    require(CRT_SIZE <= load <= MAX_SCX_LOAD and len(blob) == SCX_HEADER_SIZE + load
            and 4096 <= stack <= 131072 and stack % 4096 == 0,
            E_RANGE, "SCX size or stack differs from legacy contract")
    metadata_va = struct.unpack_from("<I", blob, SCX_HEADER_SIZE + 6)[0]
    require(metadata_va >= base + CRT_SIZE and metadata_va - base <= load - HEADER_SIZE,
            E_RANGE, "CRT descriptor outside application initialized payload")
    metadata_rva = metadata_va - base
    require(blob[SCX_HEADER_SIZE:SCX_HEADER_SIZE + CRT_SIZE] == crt(metadata_va),
            E_FORMAT, "private CRT is not the canonical generated bridge")
    parsed = validate(blob[SCX_HEADER_SIZE + metadata_rva:], metadata_rva)
    require(parsed["program_load_bytes"] == load and parsed["program_bss_bytes"] == bss,
            E_RANGE, "SCD and SCX load/BSS declarations differ")
    return parsed


def relocated_image(blob: bytes, resolved_imports=()) -> bytes:
    """宿主独立对照模型，仅产生映像字节，不执行应用或库。"""
    parsed = validate_scx(blob)
    require(len(resolved_imports) == len(parsed["imports"]), E_SYMBOL, "resolved import count differs")
    image = bytearray(blob[SCX_HEADER_SIZE:])
    image.extend(bytes(parsed["program_bss_bytes"]))
    for r in parsed["relocations"]:
        value = u32(resolved_imports[r["import_index"]]) + r["addend"]
        if r["type"] == PC32:
            value -= PROGRAM_BASE + r["patch_rva"]
        struct.pack_into("<I", image, r["patch_rva"], value & 0xFFFFFFFF)
    return bytes(image)
