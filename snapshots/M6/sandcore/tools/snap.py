#!/usr/bin/env python3
"""SandCore 截屏工具
连上 QEMU 监视器（-monitor tcp:...）发 screendump，
再把 PPM 转成 PNG（纯标准库，不依赖 PIL）。
用法: python tools/snap.py [--port=4444] [--quit] [out.png]"""
import os
import socket
import struct
import sys
import time
import zlib


def hmp_screendump(port, ppm):
    s = socket.create_connection(("127.0.0.1", port), timeout=20)
    s.settimeout(3)

    def drain():
        buf = b""
        try:
            while True:
                d = s.recv(65536)
                if not d:
                    break
                buf += d
        except socket.timeout:
            pass
        return buf

    time.sleep(0.3)
    drain()
    s.sendall(f"screendump {ppm}\n".encode())
    time.sleep(1.2)
    resp = drain()
    s.close()
    return resp


def ppm_to_png(src, dst):
    d = open(src, "rb").read()
    assert d[:2] == b"P6", "not a P6 ppm"
    i, vals = 2, []
    while len(vals) < 3:
        while i < len(d) and d[i:i + 1].isspace():
            i += 1
        j = i
        while j < len(d) and not d[j:j + 1].isspace():
            j += 1
        vals.append(int(d[i:j]))
        i = j
    i += 1
    w, h, _ = vals
    raw = d[i:i + w * h * 3]
    assert len(raw) == w * h * 3, "ppm truncated"

    def chunk(t, p):
        return (struct.pack(">I", len(p)) + t + p
                + struct.pack(">I", zlib.crc32(t + p) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    rows = b"".join(b"\x00" + raw[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    open(dst, "wb").write(png)


def main():
    out, port, quit_after = "build/shot.png", 4444, False
    for a in sys.argv[1:]:
        if a == "--quit":
            quit_after = True
        elif a.startswith("--port="):
            port = int(a[7:])
        else:
            out = a
    ppm = out.rsplit(".", 1)[0] + ".ppm"

    resp = hmp_screendump(port, ppm)
    if resp:
        print(resp.decode(errors="replace").strip())
    ppm_to_png(ppm, out)
    os.remove(ppm)
    print("saved:", out)

    if quit_after:
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
        s.sendall(b"quit\n")
        s.close()
        time.sleep(0.5)


if __name__ == "__main__":
    main()
