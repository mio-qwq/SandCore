#!/usr/bin/env python3
"""mio：把 M6 当前源码、运行双盘、诊断符号与验收证据装进一个可核对包。

build/ 中也有早期故障的超大中断日志、暂存内存和 Python 缓存，不能整目录
无条件压缩。源码树完整收录，构建产物则显式列清单；两套测试盘同时保留，
这样接手者能检查系统内实际写出的 SCX/文本/壁纸，而不只看到截图。
包内 manifest.json 对每个收录文件保存 SHA256，打包后逐项从 ZIP 重读校验。
只读源文件与数据盘，输出独立 ZIP/校验文件，不重建、不覆盖工作数据盘。
"""
import hashlib
import json
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent.parent
SYSTEM = ROOT / "sandcore"
OUTPUT = SYSTEM / "build/SandCore-M6-2026-10-02.zip"
FONT_HASH = "1ef3876e7adbf0f42dc432dbdf21c927282a7e28bb076613c6cb50fbf0de9b55"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def collect():
    """用集合消除多规则命中的重复项；相对根目录命名，解压仍保留工程层级。"""
    files = {ROOT / "AGENTS.md", ROOT / "HANDOFF.md", ROOT / "vonwaon-bitmap.ttf.zip"}
    for item in SYSTEM.rglob("*"):
        relative = item.relative_to(SYSTEM)
        if item.is_file() and relative.parts[0] != "build":
            if "__pycache__" not in relative.parts and item.suffix != ".pyc":
                files.add(item)
    # 平映像/双盘直接可运行；ELF 与符号仅用于定位和复现实验，不当作系统格式。
    for name in ("boot.bin", "kernel.bin", "kernel.elf", "kernel.sym",
                 "sandcore.img", "sanddata.img", "m6a-source-baseline.zip",
                 "m6-build.log", "m6-build-final.log", "m6-integrity.json"):
        files.add(SYSTEM / "build" / name)
    files.update((SYSTEM / "build").glob("user-*.sym"))
    files.update(item for item in (SYSTEM / "build/fs").rglob("*") if item.is_file())
    for milestone, expected in (("m6a", 11), ("m6", 24)):
        directory = SYSTEM / "build" / milestone
        shots = list(directory.glob("*.png"))
        if len(shots) != expected:
            raise RuntimeError(f"{milestone} 截图数量不符：{len(shots)}，预期 {expected}")
        files.update(shots)
        for name in ("results.json", "qemu-command.json", "sanddata-test.img"):
            files.add(directory / name)
    # 缺关键证据时停止，不能做出一个外表完整、实际不可复验的验收包。
    for item in files:
        if not item.is_file():
            raise FileNotFoundError(item)
    return sorted(files, key=lambda item: item.relative_to(ROOT).as_posix())


def main():
    font = SYSTEM / "kernel/font16.txt"
    if sha256(font.read_bytes()) != FONT_HASH:
        raise RuntimeError("用户中文字库与接手基线不一致，停止打包")
    manifest = {"author": "mio", "milestone": "M6a + M6",
                "status": "自动化通过，待用户界面验收", "files": {}}
    with zipfile.ZipFile(OUTPUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as package:
        for item in collect():
            name = item.relative_to(ROOT).as_posix()
            data = item.read_bytes()
            manifest["files"][name] = {"size": len(data), "sha256": sha256(data)}
            package.writestr(name, data)
        package.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
    # 从压缩包读取而不是再次读源目录：证据必须说明交付字节本身可恢复。
    with zipfile.ZipFile(OUTPUT) as package:
        if package.testzip() is not None:
            raise RuntimeError("ZIP CRC 校验失败")
        for name, record in manifest["files"].items():
            data = package.read(name)
            if len(data) != record["size"] or sha256(data) != record["sha256"]:
                raise RuntimeError("ZIP SHA256 校验失败：" + name)
    checksum = sha256(OUTPUT.read_bytes())
    OUTPUT.with_suffix(".sha256").write_text(checksum + "  " + OUTPUT.name + "\n", encoding="ascii")
    print(f"PASS: {len(manifest['files'])} files, {OUTPUT.stat().st_size} bytes")
    print(OUTPUT)
    print("SHA256:", checksum)


if __name__ == "__main__":
    main()
