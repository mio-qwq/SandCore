#!/usr/bin/env python3
"""SCLDR 的代表性宿主验证；不执行 i386 代码，不生成或启动系统镜像。"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import struct
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from scl_format import (ABS32, DATA, FUNCTION, IMPORT, INTERNAL, PC32,
                        SEG_R, SEG_W, SEG_X, build_scl)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "m10a2-runtime-host"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dependency(name: str, *, major=1) -> dict:
    return dict(name=name, abi_major=major, min_minor=2, min_version=3)


def library(name: str, prefix: str, initial: int, **kwargs) -> bytes:
    # 回调与导出地址固定，C 夹具根据这些已知 RVA 独立计算重定位结果。
    code = bytearray(256)
    code[0x20:0x23] = b"\x31\xc0\xc3"
    code[0x40] = code[0x80] = 0xC3
    data = bytearray(0x104)
    struct.pack_into("<I", data, 0x100, initial)
    return build_scl(
        name=name, abi_major=1, abi_minor=2, library_version=3,
        init_rva=0x40, fini_rva=0x80,
        segments=[dict(rva=0, mem_size=4096, flags=SEG_R | SEG_X, data=code),
                  dict(rva=4096, mem_size=4096, flags=SEG_R | SEG_W, data=data)],
        exports=[dict(name=f"{prefix}_function", rva=0x20, size=3, kind=FUNCTION),
                 dict(name=f"{prefix}_value", rva=0x1100, size=4, kind=DATA)],
        **kwargs,
    )


def relocation(patch: int, kind: int, target_kind: int, target: int, addend=0) -> dict:
    return dict(patch_rva=patch, type=kind, target_kind=target_kind,
                target=target, addend=addend)


def fixtures() -> dict[str, bytes]:
    files = {"BASE.SCL": library("BASE.SCL", "base", 0x11223344)}
    files["MID.SCL"] = library(
        "MID.SCL", "mid", 0x33445566,
        dependencies=[dependency("BASE.SCL")],
        imports=[dict(name="base_function", dependency="BASE.SCL", kind=FUNCTION, size=3),
                 dict(name="base_value", dependency="BASE.SCL", kind=DATA, size=4)],
        relocations=[relocation(0x10, PC32, IMPORT, 0, -4),
                     relocation(0x1000, ABS32, IMPORT, 1)],
    )
    files["SIDE.SCL"] = library(
        "SIDE.SCL", "side", 0x778899AA,
        dependencies=[dependency("BASE.SCL")],
        imports=[dict(name="base_value", dependency="BASE.SCL", kind=DATA, size=4)],
        relocations=[relocation(0x1000, ABS32, IMPORT, 0)],
    )
    # 故意逆序声明依赖和导入；builder 排序后仍必须保留原导入索引的语义。
    files["ROOT.SCL"] = library(
        "ROOT.SCL", "root", 0xAABBCCDD,
        dependencies=[dependency("SIDE.SCL"), dependency("MID.SCL")],
        imports=[dict(name="side_value", dependency="SIDE.SCL", kind=DATA, size=4),
                 dict(name="mid_value", dependency="MID.SCL", kind=DATA, size=4),
                 dict(name="mid_function", dependency="MID.SCL", kind=FUNCTION, size=3)],
        relocations=[relocation(0x10, ABS32, INTERNAL, 0x1100, 5),
                     relocation(0x14, PC32, INTERNAL, 0x20, -4),
                     relocation(0x18, ABS32, IMPORT, 2, 7),
                     relocation(0x1C, PC32, IMPORT, 2, -4),
                     relocation(0x1000, ABS32, IMPORT, 1, 12),
                     relocation(0x1004, PC32, IMPORT, 0, -8)],
    )
    files["MISSINGLIB.SCL"] = library(
        "MISSINGLIB.SCL", "missinglib", 0, dependencies=[dependency("ABSENT.SCL")])
    for name, symbol, kind, size in (
        ("BADSYMBOL.SCL", "base_missing", FUNCTION, 3),
        ("WRONGKIND.SCL", "base_value", FUNCTION, 3),
        ("TOOWIDE.SCL", "base_value", DATA, 8),
    ):
        files[name] = library(
            name, name.split(".")[0].lower(), 0,
            dependencies=[dependency("BASE.SCL")],
            imports=[dict(name=symbol, dependency="BASE.SCL", kind=kind, size=size)],
        )
    files["BADVERSION.SCL"] = library(
        "BADVERSION.SCL", "badversion", 0,
        dependencies=[dependency("BASE.SCL", major=2)])
    files["CYCLEA.SCL"] = library(
        "CYCLEA.SCL", "cyclea", 0, dependencies=[dependency("CYCLEB.SCL")])
    files["CYCLEB.SCL"] = library(
        "CYCLEB.SCL", "cycleb", 0, dependencies=[dependency("CYCLEA.SCL")])
    corrupt = bytearray(library("FORMAT.SCL", "format", 0))
    corrupt[0] ^= 1
    files["FORMAT.SCL"] = bytes(corrupt)
    corrupt = bytearray(library("RANGE.SCL", "range", 0))
    struct.pack_into("<I", corrupt, 48, 0xFFFFFFF0)
    files["RANGE.SCL"] = bytes(corrupt)
    files["MISNAMED.SCL"] = files["BASE.SCL"]
    return files


def run(command: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)


def main() -> int:
    if platform.system() != "Linux" or struct.calcsize("P") != 8:
        raise SystemExit("Use 64-bit WSL Linux: python3 tools/test_scl_runtime.py")
    OUT.mkdir(parents=True, exist_ok=True)
    fixture_dir = OUT / "fixtures"
    fixture_dir.mkdir(exist_ok=True)
    for name, data in fixtures().items():
        (fixture_dir / name).write_bytes(data)
    sources = [ROOT / "tests/m10a2/runtime_host.c", ROOT / "user/runtime/scldr.c",
               ROOT / "user/runtime/scl_format.c"]
    inputs = sources + [ROOT / "user/runtime/SCLDR.H", ROOT / "user/runtime/SCLFMT.H",
                        Path(__file__).resolve(), ROOT / "tools/scl_format.py"]
    input_sha = {str(path.relative_to(ROOT)): digest(path) for path in inputs}
    fixture_sha = {path.name: digest(path) for path in sorted(fixture_dir.glob("*.SCL"))}
    compiler = os.environ.get("CC", "cc")
    command = [compiler, "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
               "-fno-builtin", "-fsanitize=undefined", "-fno-sanitize-recover=all",
               "-I", str(ROOT / "user/runtime"), *map(str, sources),
               "-o", str(OUT / "runtime_host")]
    compiler_version = run([compiler, "--version"])
    compile_result = run(command)
    (OUT / "compile.log").write_text(compile_result.stdout + compile_result.stderr, encoding="utf-8")
    result = None
    rows: list[dict] = []
    if compile_result.returncode == 0:
        result = run([str(OUT / "runtime_host"), str(fixture_dir)])
        (OUT / "runtime.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        for line in result.stdout.splitlines():
            rows.append(json.loads(line))
    summary = next((row for row in rows if row.get("summary")), None)
    unchanged = all(digest(path) == input_sha[str(path.relative_to(ROOT))] for path in inputs)
    passed = compile_result.returncode == 0 and result is not None and result.returncode == 0 and unchanged
    report = dict(
        status="HOST_RUNTIME_PASS" if passed else "HOST_RUNTIME_FAIL",
        checked_at=datetime.now(timezone.utc).isoformat(),
        scope="64-bit WSL host loader algorithm, actual mmap/mprotect, simulated init/fini callbacks",
        executes_i386=False, guest_qemu=False, guest_ring3_acceptance=False,
        shared_physical_code_pages=False, input_sha256=input_sha,
        fixture_sha256=fixture_sha, inputs_unchanged_during_run=unchanged,
        compiler=compiler_version.stdout.splitlines()[0] if compiler_version.stdout else compiler,
        compile_command=command, compile_exit=compile_result.returncode,
        run_exit=result.returncode if result else None,
        cases=[row for row in rows if "case" in row], summary=summary,
        fixture_bytes=sum(path.stat().st_size for path in fixture_dir.glob("*.SCL")),
    )
    (OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in (
        "status", "executes_i386", "guest_qemu", "inputs_unchanged_during_run", "summary", "fixture_bytes")},
        ensure_ascii=False))
    if not passed:
        print((compile_result.stderr if compile_result.returncode else result.stderr)[:6000])
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
