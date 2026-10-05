#!/usr/bin/env python3
"""SandCore 鼠标注入工具 —— 通过 QMP 向客户机注入鼠标事件
用法:
  python tools/mouse.py move <dx> <dy>   相对移动
  python tools/mouse.py down             按下左键
  python tools/mouse.py up               松开左键
  python tools/mouse.py click            左键单击
需要 QEMU 以 -qmp tcp:127.0.0.1:4445,server,nowait 启动。"""

import json
import socket
import sys
import time

PORT = 4445


def main():
    s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
    s.settimeout(2)

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

    drain()                                       # QMP 问候语
    s.sendall(b'{"execute":"qmp_capabilities"}\n')
    time.sleep(0.1)
    drain()

    events = []
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        a = args[i]
        if a == "move":
            # 屏幕约定: +dy = 向下。(QMP rel y 与 PS/2 相反, 这里取负转接)
            dx, dy = int(args[i + 1]), int(args[i + 2])
            i += 3
            events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
            events.append({"type": "rel", "data": {"axis": "y", "value": -dy}})
        elif a == "down":
            events.append({"type": "btn", "data": {"down": True, "button": "left"}})
            i += 1
        elif a == "up":
            events.append({"type": "btn", "data": {"down": False, "button": "left"}})
            i += 1
        elif a == "click":
            events.append({"type": "btn", "data": {"down": True, "button": "left"}})
            events.append({"type": "btn", "data": {"down": False, "button": "left"}})
            i += 1
        else:
            i += 1

    if events:
        s.sendall(json.dumps({"execute": "input-send-event",
                              "arguments": {"events": events}}).encode() + b"\n")
        time.sleep(0.2)
        drain()
    s.close()
    print("mouse:", " ".join(args))


main()
