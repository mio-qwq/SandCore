#!/usr/bin/env python3
"""仅供用户亲自运行的离线签署界面；代理不得执行本工具的签署流程。

私钥位置/密码仅由本机用户在对话框输入，不进入命令行、日志或公开
结果。待签文件先核对清单，结果只含公钥/签名/公开摘要；不联网。
"""
import hashlib
import json
import os
from pathlib import Path
import sys
import time


MESSAGES = (
    'Z10-INIT.SKM.msg', 'A20-SERVICE.SKM.msg', 'B30-FAIL.SKM.msg',
    'WALL100.SKM.msg', 'BAD-ABI.SKM.msg', 'BAD-RELOC.SKM.msg',
)


def outside_workspace(path, workspace):
    # resolve同时解析已有目录的链接；不能把私钥放进受版本管理的树。
    return not path.resolve().is_relative_to(workspace.resolve())


def checked_messages(bundle):
    manifest = json.loads((bundle / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('algorithm') != 'Ed25519':
        raise ValueError('待签清单的算法不是Ed25519')
    entries = manifest.get('files', [])
    if len(entries) != len(MESSAGES) or {item['file'] for item in entries} != set(MESSAGES):
        raise ValueError('待签清单必须恰好包含约定的六份消息')
    records = {item['file']: item for item in entries}
    result = {}
    for name in MESSAGES:
        message = (bundle / name).read_bytes()
        if hashlib.sha256(message).hexdigest() != records[name]['message_sha256']:
            raise ValueError('待签消息摘要不符：' + name)
        result[name] = message
    return result


def main():
    if os.name != 'nt' or len(sys.argv) != 2:
        raise SystemExit('请在Windows中亲自双击签署批处理文件。')
    import tkinter as tk
    from tkinter import filedialog, messagebox, simpledialog
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    bundle = Path(sys.argv[1]).resolve()
    workspace = Path(__file__).resolve().parents[2]
    root = tk.Tk()
    root.withdraw()
    root.attributes('-topmost', True)
    saved_key = False
    try:
        messages = checked_messages(bundle)
        proceed = messagebox.askokcancel(
            'SandCore：由你亲自签署',
            '本工具只在本机运行，不联网。\n\n'
            '本次签署：初始化、常驻服务、失败回滚、壁纸四份扩展，'
            '以及两份只用于测试拒绝错误格式的消息。\n'
            '私钥由你保存，公开结果只有公钥和六份签名。\n\n'
            '继续后选择密钥保存位置并设置密码。', parent=root)
        if not proceed:
            return
        use_existing = messagebox.askyesno(
            '密钥选择', '你已有本工具生成的、加密的Ed25519私钥吗？\n\n'
            '第一次使用请选“否”，创建自己的密钥。', parent=root)
        if use_existing:
            choice = filedialog.askopenfilename(
                title='选择你自己的加密私钥（不要交给代理）',
                filetypes=[('加密私钥', '*.pem')], parent=root)
            if not choice:
                return
            key_path = Path(choice).resolve()
            if not outside_workspace(key_path, workspace):
                raise ValueError('私钥必须存放在projectos仓库之外。')
            password = simpledialog.askstring('解锁私钥', '输入私钥密码：', show='*', parent=root)
            if password is None:
                return
            key = serialization.load_pem_private_key(key_path.read_bytes(), password=password.encode('utf-8'))
            password = None
            if not isinstance(key, Ed25519PrivateKey):
                raise ValueError('选择的私钥不是Ed25519。')
        else:
            choice = filedialog.askdirectory(
                title='选择仓库外的文件夹保存你的私钥（也可选U盘）',
                mustexist=True, parent=root)
            if not choice:
                return
            parent = Path(choice).resolve()
            if not outside_workspace(parent, workspace):
                raise ValueError('请选择projectos仓库之外的文件夹。')
            password = simpledialog.askstring('私钥密码', '设置私钥密码（至少12个字符）：', show='*', parent=root)
            if password is None:
                return
            if len(password) < 12:
                raise ValueError('私钥密码至少需要12个字符。')
            repeat = simpledialog.askstring('确认密码', '再输入一次相同密码：', show='*', parent=root)
            if repeat is None:
                return
            if repeat != password:
                raise ValueError('两次密码不同；尚未生成私钥。')
            repeat = None
            key_directory = parent / ('SandCore-Ed25519-' + time.strftime('%Y%m%d-%H%M%S'))
            key_directory.mkdir(exist_ok=False)
            key_path = key_directory / 'SandCore-private-encrypted.pem'
            # 此调用仅发生于用户亲自运行后的界面流程；代理不代跑。
            key = Ed25519PrivateKey.generate()
            encoded = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                        serialization.BestAvailableEncryption(password.encode('utf-8')))
            password = None
            with key_path.open('xb') as handle:
                handle.write(encoded)
                handle.flush()
                os.fsync(handle.fileno())
            encoded = None
            saved_key = True
            messagebox.showinfo('私钥已保存',
                                '已在你选择的文件夹内新建SandCore-Ed25519目录。\n'
                                '请保存好其中的加密私钥和你设置的密码；之后签署仍使用这把密钥。\n'
                                '该文件和密码不要交给代理。', parent=root)
        public = key.public_key()
        public_bytes = public.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        results = {}
        for name, message in messages.items():
            signature = key.sign(message)
            public.verify(signature, message)
            if len(signature) != 64:
                raise ValueError('签名长度错误')
            results[name + '.sig'] = signature
        key = None
        output = bundle / ('user-signatures-' + time.strftime('%Y%m%d-%H%M%S'))
        output.mkdir(exist_ok=False)
        (output / 'public-key.bin').write_bytes(public_bytes)
        for name, signature in results.items():
            (output / name).write_bytes(signature)
        report = dict(status='USER_SIGNED_PUBLIC_RESULTS_ONLY', algorithm='Ed25519',
                      public_key_sha256=hashlib.sha256(public_bytes).hexdigest(),
                      signatures=[dict(file=name, sha256=hashlib.sha256(data).hexdigest())
                                  for name, data in results.items()],
                      messages=[dict(file=name, sha256=hashlib.sha256(data).hexdigest())
                                for name, data in messages.items()])
        (output / 'PUBLIC-RESULTS.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        # 公开路径可回报给代理；私钥路径没有写入任何结果文件。
        print('PUBLIC_RESULTS=' + str(output), flush=True)
        root.clipboard_clear()
        root.clipboard_append(str(output))
        messagebox.showinfo('签署完成',
                            '公开结果已保存，目录路径已复制到剪贴板：\n\n' + str(output) +
                            '\n\n回到Codex，粘贴这个目录路径即可。', parent=root)
    except Exception:
        # 异常正文可能包含用户选择的私钥路径，不能进入代理可见日志。
        messagebox.showerror('签署未完成',
                             '操作未完成：请检查所选位置、私钥密码及待签包。\n'
                             + ('新私钥已保存；重新运行时请选择使用已有私钥。' if saved_key else
                                '若已选已有私钥，它不会被修改。'), parent=root)
    finally:
        root.destroy()


if __name__ == '__main__':
    main()
