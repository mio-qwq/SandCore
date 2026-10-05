#!/usr/bin/env python3
"""SandCore 按键注入工具
通过 QEMU 监视器 (HMP sendkey) 模拟键盘输入, 用于无头自动化验证。
用法: python tools/keys.py [--port=4444] a b ret shift-c ...
键名遵循 HMP/QEMU 约定: 字母直接写, 回车是 ret, 组合键如 shift-a。"""
import socket
import sys
import time


def main():
    port, keys = 4444, []
    for a in sys.argv[1:]:
        if a.startswith("--port="):
            port = int(a[7:])
        else:
            keys.append(a)
    if not keys:
        print("usage: keys.py [--port=4444] <key> [key...]")
        return
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    s.settimeout(2)

    def drain():
        try:
            while True:
                if not s.recv(4096):
                    break
        except socket.timeout:
            pass

    time.sleep(0.3)
    drain()
    for k in keys:
        s.sendall(f"sendkey {k}\n".encode())
        time.sleep(0.15)              # 给内核 IRQ 处理和主循环留时间
        drain()
    s.close()
    print("sent:", " ".join(keys))


main()
