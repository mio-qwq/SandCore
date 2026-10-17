"""独立试玩入口：只使用同目录两盘，临时熵文件在系统临时目录回收。"""
from pathlib import Path
import argparse
import os
import secrets
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def main():
    parser = argparse.ArgumentParser(description="SandCore M10a2 playable development snapshot")
    parser.add_argument("--check", action="store_true", help="check the launcher and disks without starting QEMU")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    boot, data = root / "sandcore.img", root / "sanddata.img"
    for path, size in ((boot, 1474560), (data, 256 * 1024 * 1024)):
        if not path.is_file() or path.stat().st_size != size:
            raise RuntimeError("Missing or incomplete disk: " + str(path))
    with boot.open("rb") as stream:
        if stream.read(512)[510:512] != b"\x55\xaa":
            raise RuntimeError("The boot disk signature is invalid.")
    directory = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "qemu"
    candidates = [directory / "qemu-system-i386.exe", directory / "qemu-system-x86_64.exe"]
    qemu = next((str(path) for path in candidates if path.is_file()), None)
    qemu = qemu or shutil.which("qemu-system-i386") or shutil.which("qemu-system-x86_64")
    if not qemu:
        raise RuntimeError("QEMU was not found. Install it in C:\\Program Files\\qemu or add it to PATH.")
    if args.check:
        print("Ready: run.bat or python run.py")
        print("Boot disk: 1.44 MiB; data disk: 256 MiB; QEMU: " + qemu)
        return 0
    # fresh fw_cfg熵不写入发行盘；临时目录离开with后自动回收，试玩目录始终四文件。
    with tempfile.TemporaryDirectory(prefix="sandcore-m10a2-") as temporary:
        entropy = Path(temporary) / "entropy.bin"
        entropy.write_bytes(secrets.token_bytes(32))
        command = [qemu, "-name", "SandCore M10a2 - The Application Platform Era",
            "-accel", "tcg", "-cpu", "qemu32", "-smp", "1", "-m", "256",
            "-vga", "std", "-boot", "a", "-display", "sdl,window-close=on",
            "-monitor", "none", "-serial", "none", "-serial", "none", "-parallel", "none",
            "-rtc", "base=utc", "-fw_cfg", "name=opt/sandcore/entropy,file=" + str(entropy),
            "-drive", "format=raw,if=floppy,file=" + str(boot),
            "-drive", "format=raw,if=ide,file=" + str(data),
            "-nic", "none", "-netdev", "user,id=sc-net,ipv6=off",
            "-device", "e1000,netdev=sc-net,mac=52:54:00:12:34:56",
            "-audiodev", "dsound,id=sand-audio", "-device", "AC97,audiodev=sand-audio"]
        print("Starting SandCore M10a2. Close the QEMU window to exit.", flush=True)
        return subprocess.call(command, cwd=root)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
