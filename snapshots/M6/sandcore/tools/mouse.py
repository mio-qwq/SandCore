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
            # QMP rel y 本身就是屏幕方向；PS/2 设备会完成协议转换。
            # 这里若再取反，mouse.py move 0 20 会向上，与文档和人手相反。
            dx, dy = int(args[i + 1]), int(args[i + 2])
            i += 3
            events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
            events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
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

    # 按下/松开必须分开发送，给内核主循环机会观察两个边沿。
    # 同一 QMP 包里的 down+up 可能在一次轮询前全部到达，结果“点击”变成
    # 从未按下。相对位移也单独提交，使 move/down/up 的顺序可被重放。
    for event in events:
        s.sendall(json.dumps({"execute": "input-send-event",
                              "arguments": {"events": [event]}}).encode() + b"\n")
        time.sleep(0.12)
        drain()
    s.close()
    print("mouse:", " ".join(args))


if __name__ == "__main__":
    main()
