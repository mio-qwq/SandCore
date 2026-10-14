#!/usr/bin/env python3
"""mio：完整检查并展示 SCL 表；依赖目录显式指定，不搜索 CWD/PATH。"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import scl_format as scl


def inspect_file(path: Path) -> dict:
    if not scl.HEADER_SIZE <= path.stat().st_size <= scl.MAX_FILE_SIZE:
        raise scl.SclError(scl.E_RANGE, f"{path}: unsupported SCL file size")
    blob = path.read_bytes()
    parsed = scl.validate(blob)
    strings = blob[parsed["strings_offset"]:parsed["strings_offset"] + parsed["strings_size"]]
    entries, offset = [], 0
    for value in strings.split(b"\0")[:-1]:
        entries.append(dict(offset=offset, text=value.decode("ascii", errors="backslashreplace")))
        offset += len(value) + 1
    return dict(parsed, path=str(path), sha256=hashlib.sha256(blob).hexdigest(), strings=entries)


def check_dependencies(root: dict, directory: Path) -> dict:
    """用显式工作栈检查完整图，不让合法长链或恶意环耗尽 Python 调用栈。"""
    if not directory.is_dir():
        raise scl.SclError(-5, f"dependency directory not found: {directory}")
    modules = {root["name"]: root}
    export_indexes = {}
    order, edges = [], []
    active = [root["name"]]
    active_index = {root["name"]: 0}
    completed = set()
    cursors = [0]
    while active:
        name = active[-1]
        module = modules[name]
        cursor = cursors[-1]
        if cursor < len(module["dependencies"]):
            requirement = module["dependencies"][cursor]
            cursors[-1] += 1
            target = requirement["name"]
            if target not in modules:
                path = directory / target
                if not path.is_file():
                    raise scl.SclError(-5, f"{name}: missing dependency {target} in {directory}")
                dependency = inspect_file(path)
                if dependency["name"] != target:
                    raise scl.SclError(scl.E_VERSION, f"{path}: declared name differs from dependency {target}")
                modules[target] = dependency
            dependency = modules[target]
            if (dependency["abi_major"] != requirement["abi_major"] or
                dependency["abi_minor"] < requirement["min_minor"] or
                dependency["library_version"] < requirement["min_version"]):
                raise scl.SclError(scl.E_VERSION, f"{name}: incompatible version of {target}")
            edges.append(dict(source=name, dependency=target, abi_major=requirement["abi_major"],
                              min_minor=requirement["min_minor"], min_version=requirement["min_version"]))
            if target in active_index:
                cycle = active[active_index[target]:] + [target]
                raise scl.SclError(-7, "dependency cycle: " + " -> ".join(cycle))
            if target not in completed:
                active_index[target] = len(active)
                active.append(target)
                cursors.append(0)
        else:
            # 依赖的导出表已经排序，建立一次索引以避免逐导入扫描整张表。
            for entry in module["imports"]:
                dependency = modules[entry["dependency"]]
                if entry["dependency"] not in export_indexes:
                    export_indexes[entry["dependency"]] = {e["name"]: e for e in dependency["exports"]}
                symbol = export_indexes[entry["dependency"]].get(entry["name"])
                if symbol is None or symbol["kind"] != entry["kind"] or symbol["size"] < entry["size"]:
                    raise scl.SclError(scl.E_SYMBOL, f"{name}: missing/incompatible {entry['dependency']}:{entry['name']}")
            completed.add(name)
            order.append(name)
            active.pop()
            cursors.pop()
            del active_index[name]
    return dict(status="PASS", initialization_order=order, edges=edges,
                modules=[dict(name=name, abi_major=modules[name]["abi_major"], abi_minor=modules[name]["abi_minor"],
                              library_version=modules[name]["library_version"], sha256=modules[name].get("sha256"))
                         for name in order])


def print_tables(parsed: dict) -> None:
    def address(value: int) -> str:
        return "none" if value == scl.NO_ENTRY else f"0x{value:08x}"

    print(f"{parsed['name']}  SCL v{parsed['version']} / i386")
    print(f"ABI {parsed['abi_major']}.{parsed['abi_minor']}  library version {parsed['library_version']}")
    print(f"file {parsed['file_size']} bytes  image {parsed['image_size']} bytes  metadata {parsed['metadata_end']} bytes")
    print(f"init {address(parsed['init_rva'])}  fini {address(parsed['fini_rva'])}")
    print(f"SHA256 {parsed['sha256']}")
    print(f"segments ({len(parsed['segments'])})")
    for index, entry in enumerate(parsed["segments"]):
        access = "".join(letter for bit, letter in ((1, "R"), (2, "W"), (4, "X")) if entry["flags"] & bit)
        print(f"  [{index}] RVA 0x{entry['rva']:08x} memory {entry['mem_size']} file @{entry['file_offset']}+{entry['file_size']} {access} align {entry['alignment']}")
    print(f"exports ({len(parsed['exports'])})")
    for index, entry in enumerate(parsed["exports"]):
        kind = "function" if entry["kind"] == scl.FUNCTION else "data"
        print(f"  [{index}] {entry['name']} RVA 0x{entry['rva']:08x} size {entry['size']} {kind}")
    print(f"imports ({len(parsed['imports'])})")
    for index, entry in enumerate(parsed["imports"]):
        kind = "function" if entry["kind"] == scl.FUNCTION else "data"
        print(f"  [{index}] {entry['dependency']}:{entry['name']} dep[{entry['dep_index']}] minimum size {entry['size']} {kind}")
    print(f"relocations ({len(parsed['relocations'])})")
    for index, entry in enumerate(parsed["relocations"]):
        kind = "ABS32" if entry["type"] == scl.ABS32 else "PC32"
        target = f"RVA 0x{entry['target']:08x}" if entry["target_kind"] == scl.INTERNAL else f"import[{entry['target']}]"
        print(f"  [{index}] patch 0x{entry['patch_rva']:08x} {kind} -> {target} addend {entry['addend']}")
    print(f"dependencies ({len(parsed['dependencies'])})")
    for index, entry in enumerate(parsed["dependencies"]):
        print(f"  [{index}] {entry['name']} ABI {entry['abi_major']}.{entry['min_minor']}+ version {entry['min_version']}+ flags {entry['flags']}")
    print(f"strings ({len(parsed['strings'])}) file @{parsed['strings_offset']} size {parsed['strings_size']}")
    for entry in parsed["strings"]:
        print(f"  @{entry['offset']} {entry['text']!r}")
    if "dependency_check" in parsed:
        check = parsed["dependency_check"]
        print("dependency check PASS; init order: " + " -> ".join(check["initialization_order"]))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Inspect SCL v1 tables and recursively check declared dependencies")
    parser.add_argument("file", type=Path)
    parser.add_argument("--json", action="store_true", dest="as_json")
    parser.add_argument("--deps", type=Path, metavar="DIRECTORY", help="explicit directory containing dependency SCL files")
    args = parser.parse_args(argv)
    try:
        parsed = inspect_file(args.file)
        if args.deps is not None:
            parsed["dependency_check"] = check_dependencies(parsed, args.deps)
        if args.as_json:
            print(json.dumps(parsed, ensure_ascii=True, indent=2))
        else:
            print_tables(parsed)
        return 0
    except (OSError, scl.SclError) as exc:
        code = getattr(exc, "code", -11)
        if args.as_json:
            print(json.dumps(dict(error=code, message=str(exc)), ensure_ascii=True), file=sys.stderr)
        else:
            print(f"scldump: error {code}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
