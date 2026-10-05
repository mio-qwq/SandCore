#!/usr/bin/env python3
"""mio：把真实系统内编译的 M7 产物发布到默认运行盘。

宿主 make 负责引导器、内核与首次启动的 GCC 版本；发布不能只改说明，
而必须从已通过行为验证的 QEMU 测试盘读取实际 SCX/.map 字节。先核对
三代收敛、全部图形测试、源码/API/UI 与当前工作区一致，再更新资源树。
旧默认盘只保存一份 bootstrap 快照，随后构造全零的新盘并调用 mkfs，
使用户第一次运行时不会带入自动测试的临时文件、存档或旧磁盘残留。
此工具不修改用户字体、不重新编译、不伪造任何 QEMU 验证结果。
"""
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APPS = ('files', 'ide', 'debugger', 'lumen', 'race', 'world', 'probe')


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def read_file(disk, path):
    """按 v4 实际布局读取，大小写折叠与 SandFS 运行规则保持一致。"""
    raw = disk.read_bytes()
    count, sectors, version = struct.unpack_from('<3I', raw, 12)
    assert raw[:9] == b'SANDFSMIO' and version == 4 and sectors == 32
    assert count <= 192
    for index in range(count):
        name, lba, size = struct.unpack_from('<64sII', raw, 512 + index * 72)
        if name.split(b'\0', 1)[0].decode().upper() == path.upper():
            assert lba >= 33 and lba * 512 + size <= len(raw)
            return raw[lba * 512:lba * 512 + size]
    raise FileNotFoundError(f'{disk}: {path}')


def main():
    compiler_dir = ROOT / 'build/m7-compiler'
    graphics_dir = ROOT / 'build/m7-graphics'
    compiler_disk = compiler_dir / 'sanddata-test.img'
    graphics_disk = graphics_dir / 'sanddata-test.img'
    cr = json.loads((compiler_dir / 'results.json').read_text(encoding='utf-8'))
    gr = json.loads((graphics_dir / 'results.json').read_text(encoding='utf-8'))
    assert cr['C_semantics_18_groups'] == 'PASS'
    assert cr['negative_diagnostics_preserve_output_4'] == 'PASS'
    assert cr['self_generation2_equal'] and cr['self_generation3_equal']
    assert cr['second_generation_compiles_feature_equal']
    for key, value in gr.items():
        if not key.endswith('_sha256'):
            assert value == 'PASS', key
    compiler = read_file(compiler_disk, 'HOME/S3C2.SCX')
    assert digest(compiler) == cr['self_generation1_sha256'] == cr['self_generation3_sha256']
    assert compiler == read_file(graphics_disk, 'HOME/S3C2.SCX')
    # 编译器本体依赖四个实现片段；任何一个更新都要重新做自编译，
    # 不能只比较 main 文件后误发布与当前源码不对应的旧机器码。
    for name in ('s3c.c', 's3c_lex.inc', 's3c_pp.inc', 's3c_parse.inc', 's3c_emit.inc'):
        assert read_file(compiler_disk, 'SYS/SRC/' + name) == (ROOT / 'user' / name).read_bytes(), name
    api = (ROOT / 'user/SCAPI.H').read_bytes()
    assert read_file(compiler_disk, 'SYS/INC/SCAPI.H') == api
    assert read_file(graphics_disk, 'SYS/INC/SCAPI.H') == api
    assert read_file(graphics_disk, 'SYS/SRC/UI.inc') == (ROOT / 'user/UI.inc').read_bytes()
    outputs = {'bin/s3c.scx': compiler,
               'bin/s3c.scx.map': read_file(compiler_disk, 'HOME/S3C2.SCX.map')}
    provenance = {'author': 'mio', 'status': 'PASS', 'compiler': digest(compiler), 'files': {}}
    for app in APPS:
        source = (ROOT / 'user' / (app + '.c')).read_bytes()
        assert read_file(graphics_disk, 'SYS/SRC/' + app + '.c') == source, app
        blob = read_file(graphics_disk, 'apps/' + app + '.scx')
        assert blob[:8] == b'SCX1MIO\0' and digest(blob) == gr[app + '_native_sha256']
        outputs['apps/' + app + '.scx'] = blob
        outputs['apps/' + app + '.scx.map'] = read_file(graphics_disk, 'apps/' + app + '.scx.map')
        provenance['files'][app] = {'source_sha256': digest(source), 'scx_sha256': digest(blob)}
    bootstrap = ROOT / 'build/M7-bootstrap-sanddata.img'
    if not bootstrap.exists():
        shutil.copy2(ROOT / 'build/sanddata.img', bootstrap)
    for name, blob in outputs.items():
        (ROOT / 'build/fs' / name).write_bytes(blob)
    # mkfs 保留输入磁盘的容量；发布先清零，以便得到无测试杂项的新镜像。
    disk = ROOT / 'build/sanddata.img'
    disk.write_bytes(bytes(8 * 1024 * 1024))
    subprocess.run([sys.executable, str(ROOT / 'tools/mkfs.py')], check=True)
    for name, blob in outputs.items():
        assert read_file(disk, name) == blob, name
    provenance['api_sha256'] = digest(api)
    provenance['images'] = {name: digest((ROOT / 'build' / name).read_bytes())
                            for name in ('sandcore.img', 'sanddata.img')}
    (ROOT / 'build/m7-native-runtime.json').write_text(
        json.dumps(provenance, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS: default runtime uses verified native G2 and seven M7 applications')


if __name__ == '__main__':
    main()
