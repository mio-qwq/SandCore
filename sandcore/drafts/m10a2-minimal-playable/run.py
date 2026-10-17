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
    parser.add_argument("--accel", choices=("auto", "whpx", "tcg"), default="auto",
                        help="auto prefers WHPX hardware acceleration and falls back to TCG")
    parser.add_argument("--display", choices=("sdl", "gtk"), default="sdl",
                        help="SDL uses native relative mouse input; GTK remains available for diagnosis")
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
    candidates = [directory / "qemu-system-x86_64.exe", directory / "qemu-system-i386.exe"]
    qemu = next((str(path) for path in candidates if path.is_file()), None)
    qemu = qemu or shutil.which("qemu-system-x86_64") or shutil.which("qemu-system-i386")
    if not qemu:
        raise RuntimeError("QEMU was not found. Install it in C:\\Program Files\\qemu or add it to PATH.")
    available = subprocess.run([qemu, "-accel", "help"], capture_output=True, text=True, timeout=5)
    has_whpx = available.returncode == 0 and "whpx" in available.stdout.split()
    if args.accel == "whpx" and not has_whpx:
        raise RuntimeError("This QEMU binary does not include WHPX support.")
    acceleration = "whpx:tcg" if args.accel == "auto" and has_whpx else "tcg" if args.accel == "auto" else args.accel
    if args.check:
        print("Ready: run.bat or python run.py")
        print("Boot disk: 1.44 MiB; data disk: 256 MiB; QEMU: " + qemu)
        print("Acceleration preference: " + acceleration + "; display: " + args.display + "; graphical monitor enabled.")
        return 0
    display = "sdl,gl=off,window-close=on" if args.display == "sdl" else "gtk,zoom-to-fit=off,show-menubar=on,show-tabs=on,window-close=on"
    # fresh fw_cfg熵不写入发行盘；临时目录离开with后自动回收，试玩目录始终四文件。
    with tempfile.TemporaryDirectory(prefix="sandcore-m10a2-") as temporary:
        entropy = Path(temporary) / "entropy.bin"
        entropy.write_bytes(secrets.token_bytes(32))
        command = [qemu, "-name", "SandCore M10a2 - The Application Platform Era",
            "-machine", "accel=" + acceleration, "-cpu", "qemu32", "-smp", "1", "-m", "256",
            "-vga", "std", "-boot", "a", "-display",
            display,
            "-monitor", "vc", "-serial", "none", "-serial", "none", "-parallel", "none",
            "-rtc", "base=utc", "-fw_cfg", "name=opt/sandcore/entropy,file=" + str(entropy),
            "-drive", "format=raw,if=floppy,file=" + str(boot),
            "-drive", "format=raw,if=ide,file=" + str(data),
            "-nic", "none", "-netdev", "user,id=sc-net,ipv6=off",
            "-device", "e1000,netdev=sc-net,mac=52:54:00:12:34:56",
            "-audiodev", "dsound,id=sand-audio", "-device", "AC97,audiodev=sand-audio"]
        # 点阵界面按原生像素显示；兼容层只传给此QEMU子进程，避免
        # Windows把未声明DPI感知的窗口再作模糊拉伸，不改系统缩放设置。
        environment = os.environ.copy()
        layers = environment.get("__COMPAT_LAYER", "").split()
        if "HIGHDPIAWARE" not in [value.upper() for value in layers]:
            layers.append("HIGHDPIAWARE")
        environment["__COMPAT_LAYER"] = " ".join(layers)
        if args.display == "sdl":
            # PS/2客体需要真实相对量。GTK靠宿主坐标差及边缘回中，
            # Windows全屏捕获失效时会撞隐形墙；SDL改走Raw Input。
            # 坐标保持物理像素，由QEMU只转换一次；最近邻避免字形再被滤糊。
            environment.update(SDL_WINDOWS_DPI_AWARENESS="permonitorv2",
                               SDL_WINDOWS_DPI_SCALING="0",
                               SDL_MOUSE_RELATIVE_MODE_WARP="0",
                               SDL_MOUSE_RELATIVE_SCALING="0",
                               SDL_RENDER_SCALE_QUALITY="nearest")
        print("Starting SandCore M10a2 (" + acceleration + "). Close the QEMU window to exit.", flush=True)
        if args.display == "sdl":
            print("Ctrl+Alt+F: fullscreen/windowed; Ctrl+Alt+G: release/grab when windowed; Ctrl+Alt+2: toggle monitor.", flush=True)
        else:
            print("Ctrl+Alt+F: fullscreen/windowed; Ctrl+Alt+G: release/grab; Ctrl+Alt+2/1: monitor/guest display.", flush=True)
        return subprocess.call(command, cwd=root, env=environment)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
