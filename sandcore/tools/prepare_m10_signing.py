#!/usr/bin/env python3
"""生成可审阅的M10用户离线签署包，只构建消息，不接收或使用私钥。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

from mkext import check_message

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--wall', required=True, type=Path)
    args = parser.parse_args()
    if os.name == 'nt':
        parser.error('扩展ELF32夹具在WSL工具链构建；本工具无签署入口')
    directory = args.out.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    shutil.copy2(ROOT/'tests/m10/M10EXT.c', directory/'M10EXT.c')
    shutil.copy2(ROOT/'kernel/module.h', directory/'module.h')
    shutil.copy2(ROOT/'kernel/io.h', directory/'io.h')
    sources = ['tests/m10/M10EXT.c', 'kernel/module.h', 'kernel/io.h', 'kernel/palette.h',
               'modules/core.c', 'modules/linker.ld', 'tools/mkskm.py', 'tools/mkext.py']
    source_report = []
    for name in sources:
        destination = directory/'source'/name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT/name, destination)
        source_report.append(dict(path='source/'+name, sha256=digest(destination.read_bytes())))
    wall = args.wall.resolve(strict=True)
    shutil.copy2(wall, directory/'WALL-V1.SKM')
    reports = []
    with (directory/'build.log').open('x', encoding='utf-8') as log:
        def run(command):
            log.write(json.dumps(command, ensure_ascii=False)+'\n');log.flush()
            subprocess.run(command, cwd=directory/'source', stdout=log, stderr=subprocess.STDOUT, check=True)

        flags = ['-m32', '-ffreestanding', '-nostdlib', '-fno-builtin', '-fno-stack-protector',
                 '-fno-pie', '-fno-asynchronous-unwind-tables', '-fno-exceptions',
                 '-msoft-float', '-mno-sse', '-mno-mmx', '-O2', '-Wall', '-Wextra']
        for name, number, callbacks, failure in [('Z10-INIT', 10, False, False),
                                                ('A20-SERVICE', 20, True, False),
                                                ('B30-FAIL', 30, True, True)]:
            base = directory/name
            defines = ['-DFIXTURE_NUMBER='+str(number)]
            if callbacks: defines.append('-DFIXTURE_CALLBACKS=1')
            if failure: defines.append('-DFIXTURE_FAIL=1')
            run(['gcc', *flags, *defines, '-c', 'tests/m10/M10EXT.c', '-o', str(base.with_suffix('.o'))])
            run(['ld', '-m', 'elf_i386', '--emit-relocs', '-T', 'modules/linker.ld',
                 str(base.with_suffix('.o')), '-o', str(base.with_suffix('.elf'))])
            run(['objcopy', '-O', 'binary', str(base.with_suffix('.elf')), str(base.with_suffix('.bin'))])
            run([sys.executable, 'tools/mkskm.py', str(base.with_suffix('.elf')),
                 str(base.with_suffix('.bin')), str(base.with_suffix('.v1.skm'))])
            message = directory/(name+'.SKM.msg')
            run([sys.executable, 'tools/mkext.py', '--legacy', str(base.with_suffix('.v1.skm')),
                 '--number', str(number), '--out', str(message)])
            reports.append(dict(file=message.name, purpose='INIT_FAIL_ROLLBACK' if failure else
                                'RESIDENT_IRQ_SERVICE' if callbacks else 'INIT_ORDER_BSS_RELOCATION',
                                **check_message(message.read_bytes())))
        wall_message = directory/'WALL100.SKM.msg'
        run([sys.executable, 'tools/mkext.py', '--legacy', str(directory/'WALL-V1.SKM'), '--number', '100', '--out', str(wall_message)])
        reports.append(dict(file=wall_message.name, purpose='PRODUCTION_WALLPAPER',
                            **check_message(wall_message.read_bytes())))

    # 两个已签坏格式夹具用于核对格式拒绝，而不是扩大发布器接受范围。
    # 它们只进入受控验证副本，不能由正式signed-dir发布。所有变更字段
    # 仍先重算正文SHA/头CRC，用户签署的是这份精确原字节。
    source = (directory/'Z10-INIT.SKM.msg').read_bytes()
    for name, purpose, change in [('BAD-ABI', 'REJECT_SIGNED_UNSUPPORTED_ABI', 'abi'),
                                  ('BAD-RELOC', 'REJECT_SIGNED_OUT_OF_RANGE_RELOCATION', 'reloc')]:
        data = bytearray(source)
        if change == 'abi':
            struct.pack_into('<II', data, 28, 2, 2)
        else:
            image = struct.unpack_from('<I', data, 44)[0]
            count = struct.unpack_from('<I', data, 60)[0]
            if not count: raise ValueError('夹具必须有真实重定位')
            struct.pack_into('<I', data, 128+image, image+1)
        data[72:104] = hashlib.sha256(data[128:]).digest()
        struct.pack_into('<I', data, 124, zlib.crc32(data[:124]))
        path = directory/(name+'.SKM.msg');path.write_bytes(data)
        reports.append(dict(file=path.name, purpose=purpose, message_sha256=digest(data),
                            bytes=len(data), invalid_format_intended=True))

    manifest = dict(status='UNSIGNED_USER_SIGNATURE_REQUIRED', algorithm='Ed25519',
                    private_key_access='NONE', source_sha256=digest((directory/'M10EXT.c').read_bytes()),
                    source_files=source_report, wall_legacy_sha256=digest((directory/'WALL-V1.SKM').read_bytes()),
                    files=reports, signing_bytes='Entire .SKM.msg file, raw Ed25519, not the digest text',
                    results='User returns raw public key (32B) and separate .sig per message (64B)')
    (directory/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    instructions = '''# M10a1 用户独立签署包

这里包含完整测试源码、服务表、ELF/平映像、重定位、原SKM1、六份
待签消息与逐份SHA256。没有私钥，也没有代理生成的公钥或签名。

先检查M10EXT.c和manifest.json。Z10编号10只写初始化顺序/BSS次数；
A20编号20注册PIT计数和每100tick服务、持有8192B直到重启；B30编号30
故意返回7，观察内核撤销注册项/辅助页。WALL100是独立壁纸编号100。
BAD-ABI/BAD-RELOC刻意不符合格式，验证签名合法仍不能越过格式门槛，
只装受控测试副本，不进入正式发布；重复编号测试复用同一签名文件。

用户在自己持有私钥的环境签每个.SKM.msg的完整原字节（不是摘要文本），
输出对应.SKM.msg.sig的64B原始Ed25519签名。使用工具与私钥路径由用户
自行选择；代理不会运行签署命令，也不需要知道私钥位置。

仅交回32B原始公钥文件的位置、六份64B签名的位置。后续代理只用公钥
做宿主验签/装包、客体排序/拒绝/初始化与常驻生命周期验证。宿主通过
不代表客体通过；缺结果时正向项继续待验，其它M10a1工作照常推进。

首次没有密钥时，可亲自双击仓库根目录sign-m10a1-yourself.bat：
“已有私钥吗”选择“否”，选择projectos之外的文件夹，设置并记住
至少12字符密码。只将最后复制的公开结果目录路径交给代理。之后
仍使用同一私钥；该脚本不会自动上传，代理不得代跑生成/签署流程。
此批处理默认指向m10a1-signing-01；其它包须显式修改公开包参数。
'''
    (directory/'README.md').write_text(instructions, encoding='utf-8')
    print(json.dumps(dict(status=manifest['status'], messages=len(reports), out=str(directory))))


if __name__ == '__main__':
    main()
