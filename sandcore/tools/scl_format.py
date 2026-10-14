"""mio：SCL v1 的标准库依赖 Python 读写实现；与 freestanding C 验证器共用合同。"""

from __future__ import annotations

import re
import struct
from bisect import bisect_right
from typing import Any

MAGIC = b"SCL1MIO\0"
HEADER_SIZE = 128
VERSION = 1
MACHINE_I386 = 3
PAGE_SIZE = 4096
MAX_FILE_SIZE = 16 * 1024 * 1024
MAX_IMAGE_SIZE = 64 * 1024 * 1024
NO_ENTRY = 0xFFFFFFFF
SEG_R, SEG_W, SEG_X = 1, 2, 4
FUNCTION, DATA = 1, 2
ABS32, PC32 = 1, 2
INTERNAL, IMPORT = 0, 1
E_FORMAT, E_RANGE, E_SYMBOL, E_VERSION = -1, -2, -3, -4
LIBRARY_NAME = re.compile(r"[A-Z0-9_]{1,31}\.SCL\Z", re.ASCII)
SYMBOL_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]{0,126}\Z", re.ASCII)


class SclError(ValueError):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code


def _require(condition: bool, code: int, message: str) -> None:
    if not condition:
        raise SclError(code, message)


def u32(value: int, field: str = "integer") -> int:
    _require(isinstance(value, int) and 0 <= value <= 0xFFFFFFFF,
             E_RANGE, f"{field}: expected an unsigned 32-bit integer")
    return value


def align(value: int, boundary: int = PAGE_SIZE) -> int:
    return (value + boundary - 1) & -boundary


def kind_value(value: int | str) -> int:
    if value == "function":
        return FUNCTION
    if value == "data":
        return DATA
    _require(value in (FUNCTION, DATA), E_SYMBOL, "symbol kind must be function or data")
    return int(value)


def validate(blob: bytes | bytearray | memoryview) -> dict[str, Any]:
    """完整验证后返回可 JSON 序列化的真实表；失败抛出携带 C 错误码的 SclError。"""
    data = memoryview(blob).cast("B")
    size = len(data)
    _require(HEADER_SIZE <= size <= MAX_FILE_SIZE, E_RANGE, "file size outside SCL v1 limits")
    _require(data[:8] == MAGIC, E_FORMAT, "bad SCL magic")
    words = struct.unpack_from("<30I", data, 8)
    (header_size, version, machine, flags, file_size, image_size, major, minor,
     library_version, name_off, seg_off, seg_count, exp_off, exp_count,
     imp_off, imp_count, rel_off, rel_count, dep_off, dep_count,
     str_off, str_size, init, fini, *reserved) = words
    _require(header_size == HEADER_SIZE and flags == 0 and not any(reserved),
             E_FORMAT, "noncanonical header or reserved field")
    _require(version == VERSION and machine == MACHINE_I386 and major != 0,
             E_VERSION, "unsupported SCL format, machine or ABI major")
    _require(file_size == size, E_RANGE, "declared and exact file sizes differ")
    _require(0 < image_size <= MAX_IMAGE_SIZE and image_size % PAGE_SIZE == 0,
             E_RANGE, "image size is not a nonzero page-aligned supported size")
    cursor = HEADER_SIZE
    for offset, count, width, label in (
        (seg_off, seg_count, 24, "segments"), (exp_off, exp_count, 20, "exports"),
        (imp_off, imp_count, 16, "imports"), (rel_off, rel_count, 20, "relocations"),
        (dep_off, dep_count, 20, "dependencies"),
    ):
        _require(offset == cursor and count <= (size - cursor) // width,
                 E_RANGE, f"{label}: noncanonical offset or table overflow")
        cursor += count * width
    _require(str_off == cursor and 0 < str_size <= size - cursor,
             E_RANGE, "invalid string table range")
    strings = bytes(data[str_off:str_off + str_size])
    metadata_end = cursor + str_size
    _require(strings[-1] == 0, E_SYMBOL, "string table must end with NUL")

    def name(offset: int, library: bool = False) -> str:
        _require(offset < str_size and (offset == 0 or strings[offset - 1] == 0),
                 E_SYMBOL, "string offset is not a string start")
        end = strings.find(b"\0", offset, offset + (36 if library else 128))
        _require(end >= 0, E_SYMBOL, "unterminated or overlong name")
        try:
            text = strings[offset:end].decode("ascii")
        except UnicodeDecodeError as exc:
            raise SclError(E_SYMBOL, "names must be ASCII") from exc
        _require(bool((LIBRARY_NAME if library else SYMBOL_NAME).fullmatch(text)),
                 E_SYMBOL, "invalid library or symbol name")
        return text

    library_name = name(name_off, True)
    _require(seg_count > 0, E_FORMAT, "SCL requires at least one segment")
    segments = []
    previous_end = 0
    payload_end = metadata_end
    for i in range(seg_count):
        rva, mem, offset, length, access, alignment = struct.unpack_from("<6I", data, seg_off + i * 24)
        _require(mem > 0 and rva % PAGE_SIZE == 0 and mem % PAGE_SIZE == 0
                 and previous_end <= rva <= image_size and mem <= image_size - rva
                 and length <= mem and alignment == PAGE_SIZE,
                 E_RANGE, f"segment {i}: invalid image range or alignment")
        _require(bool(access & SEG_R) and access & ~7 == 0
                 and not (access & SEG_W and access & SEG_X),
                 E_FORMAT, f"segment {i}: invalid access flags")
        previous_end = rva + mem
        if length:
            _require(payload_end <= offset <= size and length <= size - offset,
                     E_RANGE, f"segment {i}: overlapping or invalid payload")
            _require(not any(data[payload_end:offset]), E_FORMAT, "nonzero payload padding")
            payload_end = offset + length
        else:
            _require(offset == 0, E_RANGE, "zero-sized payload needs zero file offset")
        segments.append(dict(rva=rva, mem_size=mem, file_offset=offset, file_size=length,
                             flags=access, alignment=alignment))
    _require(not any(data[payload_end:]), E_FORMAT, "nonzero trailing padding")
    segment_starts = [s["rva"] for s in segments]

    def segment(rva: int, length: int) -> dict[str, int]:
        index = bisect_right(segment_starts, rva) - 1
        _require(index >= 0, E_RANGE, "RVA precedes every segment")
        found = segments[index]
        relative = rva - found["rva"]
        _require(relative < found["mem_size"] and length <= found["mem_size"] - relative,
                 E_RANGE, "RVA range is outside a single segment")
        return found

    dependencies = []
    previous = ""
    for i in range(dep_count):
        offset, dep_major, min_minor, min_version, dep_flags = struct.unpack_from("<5I", data, dep_off + i * 20)
        dep_name = name(offset, True)
        _require(dep_major != 0, E_VERSION, "dependency ABI major must be nonzero")
        _require(dep_flags == 0, E_FORMAT, "unsupported dependency flags")
        _require(dep_name > previous, E_SYMBOL, "dependencies must have unique sorted names")
        previous = dep_name
        dependencies.append(dict(name=dep_name, abi_major=dep_major, min_minor=min_minor,
                                 min_version=min_version, flags=dep_flags))
    exports = []
    previous = ""
    for i in range(exp_count):
        offset, rva, length, kind, reserved_word = struct.unpack_from("<5I", data, exp_off + i * 20)
        symbol = name(offset)
        _require(length > 0 and kind in (FUNCTION, DATA), E_SYMBOL, "invalid export size or kind")
        _require(reserved_word == 0, E_FORMAT, "nonzero export reserved field")
        _require(symbol > previous, E_SYMBOL, "exports must have unique sorted names")
        previous = symbol
        access = segment(rva, length)["flags"]
        _require((kind == FUNCTION) == bool(access & SEG_X), E_SYMBOL, "export kind conflicts with segment")
        exports.append(dict(name=symbol, rva=rva, size=length, kind=kind))
    imports = []
    previous_key = (-1, "")
    for i in range(imp_count):
        offset, dependency, kind, length = struct.unpack_from("<4I", data, imp_off + i * 16)
        symbol = name(offset)
        _require(length > 0 and kind in (FUNCTION, DATA), E_SYMBOL, "invalid import size or kind")
        _require(dependency < dep_count, E_RANGE, "import dependency index outside table")
        key = (dependency, symbol)
        _require(key > previous_key, E_SYMBOL, "imports must be uniquely sorted by dependency and name")
        previous_key = key
        imports.append(dict(name=symbol, dep_index=dependency,
                            dependency=dependencies[dependency]["name"], kind=kind, size=length))
    relocations = []
    previous_end = 0
    for i in range(rel_count):
        patch, kind, target_kind, target, addend = struct.unpack_from("<5I", data, rel_off + i * 20)
        _require(patch >= previous_end, E_RANGE, "relocations overlap or are not sorted")
        segment(patch, 4)
        previous_end = patch + 4
        _require(kind in (ABS32, PC32), E_FORMAT, "unsupported relocation type")
        if target_kind == INTERNAL:
            segment(target, 1)
        elif target_kind == IMPORT:
            _require(target < imp_count, E_RANGE, "relocation import index outside table")
        else:
            raise SclError(E_FORMAT, "unsupported relocation target kind")
        relocations.append(dict(patch_rva=patch, type=kind, target_kind=target_kind,
                                target=target, addend=addend if addend < 0x80000000 else addend - 0x100000000))
    for label, rva in (("init", init), ("fini", fini)):
        if rva != NO_ENTRY:
            _require(bool(segment(rva, 1)["flags"] & SEG_X), E_RANGE, f"{label} is not executable")
    return dict(format="SCL1MIO", version=version, machine=machine, name=library_name,
                file_size=size, image_size=image_size, abi_major=major, abi_minor=minor,
                library_version=library_version, init_rva=init, fini_rva=fini,
                segments=segments, exports=exports, imports=imports,
                relocations=relocations, dependencies=dependencies,
                metadata_end=metadata_end, strings_offset=str_off, strings_size=str_size)


def build_scl(*, name: str, segments: list[dict], exports=(), imports=(),
              relocations=(), dependencies=(), abi_major=1, abi_minor=0,
              library_version=1, init_rva=NO_ENTRY, fini_rva=NO_ENTRY) -> bytes:
    """规范化排序后封装；导入重定位 target 是调用方 imports 列表的原索引。

    段 data 为原始 bytes，mem_size 为非零页对齐长度。构建器不生成机器码，
    不猜测外部符号归属；最终统一验证会拒绝任何无效输入和权限组合。
    """
    segs = sorted((dict(s) for s in segments), key=lambda s: s["rva"])
    deps = sorted((dict(d) for d in dependencies), key=lambda d: d["name"])
    dependency_index = {d["name"]: i for i, d in enumerate(deps)}
    exps = sorted((dict(e) for e in exports), key=lambda e: e["name"])
    imps = []
    for index, source in enumerate(imports):
        item = dict(source)
        _require(item["dependency"] in dependency_index, E_SYMBOL, "import has no declared dependency")
        item["dep_index"] = dependency_index[item["dependency"]]
        item["original_index"] = index
        imps.append(item)
    imps.sort(key=lambda i: (i["dep_index"], i["name"]))
    import_remap = {i["original_index"]: n for n, i in enumerate(imps)}
    rels = sorted((dict(r) for r in relocations), key=lambda r: r["patch_rva"])
    for r in rels:
        if r["target_kind"] == IMPORT:
            _require(r["target"] in import_remap, E_RANGE, "invalid builder import index")
            r["target"] = import_remap[r["target"]]
    strings = bytearray()
    offsets: dict[str, int] = {}

    def intern(value: str) -> int:
        if value not in offsets:
            _require(isinstance(value, str), E_SYMBOL, "string value required")
            try:
                raw = value.encode("ascii")
            except UnicodeEncodeError as exc:
                raise SclError(E_SYMBOL, "SCL names must be ASCII") from exc
            _require(b"\0" not in raw, E_SYMBOL, "embedded NUL in name")
            offsets[value] = len(strings)
            strings.extend(raw + b"\0")
        return offsets[value]

    name_offset = intern(name)
    export_words = [(intern(e["name"]), u32(e["rva"]), u32(e["size"]), kind_value(e["kind"]), 0) for e in exps]
    import_words = [(intern(i["name"]), i["dep_index"], kind_value(i["kind"]), u32(i["size"])) for i in imps]
    dependency_words = [(intern(d["name"]), u32(d["abi_major"]), u32(d.get("min_minor", 0)),
                         u32(d.get("min_version", 0)), u32(d.get("flags", 0))) for d in deps]
    cursor = HEADER_SIZE
    table_header = []
    for count, width in ((len(segs), 24), (len(exps), 20), (len(imps), 16), (len(rels), 20), (len(deps), 20)):
        table_header.extend((cursor, count))
        cursor += count * width
    string_offset = cursor
    cursor += len(strings)
    _require(cursor <= MAX_FILE_SIZE, E_RANGE, "metadata exceeds SCL file limit")
    segment_words = []
    payload = bytearray()
    image_size = 0
    for s in segs:
        raw = bytes(s.get("data", b""))
        rva, mem = u32(s["rva"]), u32(s["mem_size"])
        image_size = max(image_size, rva + mem)
        if raw:
            offset = align(cursor, 4)
            _require(offset + len(raw) <= MAX_FILE_SIZE, E_RANGE, "payload exceeds SCL file limit")
            payload.extend(bytes(offset - cursor))
            payload.extend(raw)
            cursor = offset + len(raw)
        else:
            offset = 0
        segment_words.append((rva, mem, offset, len(raw), u32(s["flags"]), PAGE_SIZE))
    header = (HEADER_SIZE, VERSION, MACHINE_I386, 0, cursor, u32(image_size),
              u32(abi_major), u32(abi_minor), u32(library_version), name_offset,
              *table_header, string_offset, len(strings), u32(init_rva), u32(fini_rva),
              0, 0, 0, 0, 0, 0)
    output = bytearray(MAGIC + struct.pack("<30I", *header))
    for row in segment_words:
        output.extend(struct.pack("<6I", *row))
    for row in export_words:
        output.extend(struct.pack("<5I", *row))
    for row in import_words:
        output.extend(struct.pack("<4I", *row))
    for r in rels:
        addend = r.get("addend", 0)
        _require(isinstance(addend, int) and -0x80000000 <= addend <= 0x7FFFFFFF,
                 E_RANGE, "relocation addend must be signed 32-bit")
        output.extend(struct.pack("<5I", u32(r["patch_rva"]), u32(r["type"]),
                                  u32(r["target_kind"]), u32(r["target"]), addend & 0xFFFFFFFF))
    for row in dependency_words:
        output.extend(struct.pack("<5I", *row))
    output.extend(strings)
    output.extend(payload)
    result = bytes(output)
    validate(result)
    return result


def relocated_image(blob: bytes, base: int, resolved_imports=()) -> bytes:
    """宿主检查用的独立重定位模型；只产生字节，不执行任何客体指令。"""
    parsed = validate(blob)
    u32(base, "base")
    _require(base % PAGE_SIZE == 0 and parsed["image_size"] <= 0x100000000 - base,
             E_RANGE, "runtime image range wraps or is misaligned")
    _require(len(resolved_imports) == len(parsed["imports"]), E_SYMBOL, "resolved import count differs")
    image = bytearray(parsed["image_size"])
    for s in parsed["segments"]:
        image[s["rva"]:s["rva"] + s["file_size"]] = blob[s["file_offset"]:s["file_offset"] + s["file_size"]]
    for r in parsed["relocations"]:
        symbol = base + r["target"] if r["target_kind"] == INTERNAL else u32(resolved_imports[r["target"]])
        value = symbol + r["addend"]
        if r["type"] == PC32:
            value -= base + r["patch_rva"]
        struct.pack_into("<I", image, r["patch_rva"], value & 0xFFFFFFFF)
    return bytes(image)
