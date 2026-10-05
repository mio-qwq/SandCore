#!/usr/bin/env python3
"""mio：M8原生发布接线，第一阶段只写入本工具，不自动执行。

由第二阶段完整验收产生清单后，从真实客体盘提取SCCC生成的SCX/
符号；按清单检查源码、API、核、证据和产物哈希，再生成独立发布盘。
不调用宿主编译、不运行测试、不把GCC版偷换成原生版，不改默认盘或
用户正在使用的盘。压缩服务明确保留HOST_BOOTSTRAP来源，不能说它
由当前尚不支持第三方全部语法的SCCC生成。
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ('ls', 'pwd', 'cat', 'echo', 'stat', 'cp', 'mkdir', 'rm', 'mv', 'mem',
       'uptime', 'help', 'whoami', 'env', 'lsblk', 'partitions', 'df', 'ps', 'cpu')
GROUPS = ('display', 'themes', 'components', 'images', 'configuration', 'native',
          'games', 'film', 'performance', 'resources', 'api_abi', 'm6_m7')
SOURCE_DIRS = ('boot', 'kernel', 'modules', 'user', 'third_party')
SOURCE_FILES = ('Makefile', 'linker.ld', 'user/linker.ld')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fold(name):
    # SandFS只折叠ASCII大小写，不能把汉字/其它Unicode做宿主casefold。
    return ''.join(chr(ord(c)-32) if 'a' <= c <= 'z' else c for c in name)


def required(condition, message):
    # 发布约束不是Python调试assert，python -O也不能跳过它。
    if not condition:
        raise ValueError(message)


def local_file(name, base=ROOT):
    path = (base / name).resolve()
    required(path.is_relative_to(base.resolve()), f'路径越过约定目录：{name}')
    required(path.is_file(), f'缺少文件：{path}')
    return path


def input_fingerprints():
    # 用户库之外也绑定启动器/内核/解码器依赖，防止新内核源配旧软盘。
    # 发布树单独绑定：原生替换之前的每个资源都有明确源盘摘要。
    paths = {ROOT/name for name in SOURCE_FILES}
    for name in SOURCE_DIRS:
        paths.update(p for p in (ROOT/name).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    return {p.relative_to(ROOT).as_posix(): sha(p.read_bytes()) for p in sorted(paths)}


def read_volume(path):
    raw = path.read_bytes()
    required(len(raw) >= 33*512 and len(raw) % 512 == 0 and raw[:9] == b'SANDFSMIO', '无效SandFS镜像')
    count, sectors, version = struct.unpack_from('<3I', raw, 12)
    required(version == 4 and sectors in (32, 80) and count <= (192 if sectors == 32 else 512), '无效v4目录')
    result = {}
    for index in range(count):
        name, start, size = struct.unpack_from('<64sII', raw, 512+index*72)
        required(name[0] and name[-1] == 0, '无效目录名称')
        name = name.split(b'\0', 1)[0].decode('utf-8')
        key = fold(name)
        required(key not in result, f'重复目录项：{name}')
        if start == 0 and size == 0:
            result[key] = None
        else:
            required(start >= 1+sectors and start*512+size <= len(raw), f'文件越界：{name}')
            result[key] = raw[start*512:start*512+size]
    return result


def scx_check(blob):
    required(blob is not None and len(blob) >= 37 and blob[:8] == b'SCX1MIO\0' and blob[32:36] == b'MIO\0', '无效SCX头')
    entry, load, bss, stack, flags, base = struct.unpack_from('<6I', blob, 8)
    required(0 < load <= 262108 and entry < load and load+bss <= 0x3D0000
             and 4096 <= stack <= 131072 and base == 0x400000 and not flags & ~3, 'SCX加载合同越界')
    required(len(blob) >= load+36, 'SCX正文截断')
    if flags & 2:
        icon = blob[load+36:]
        required(len(icon) >= 32 and icon[:8] == b'SCB2MIO\0', '无效SCX内置图标')
        w, h, fmt, reserved, size, author = struct.unpack_from('<6I', icon, 8)
        required(1 <= w <= 128 and 1 <= h <= 128 and fmt == 2 and reserved == 0
                 and size == w*h*4 and len(icon) == size+32 and author == 0x004F494D, '图标长度/维度越界')
    else:
        required(len(blob) == load+36, 'SCX未知尾部')


def factory_volume(files):
    required(len(files) <= 512, '发布目录超过512项')
    raw = bytearray(64*1024*1024)
    raw[:9] = b'SANDFSMIO'
    struct.pack_into('<4I', raw, 12, len(files), 80, 4, len(raw)//512)
    lba = 81
    for index, (name, blob) in enumerate(sorted(files.items())):
        encoded = name.encode('utf-8')
        required(0 < len(encoded) <= 63 and '\0' not in name, f'无效发布路径：{name}')
        at = 512+index*72
        raw[at:at+len(encoded)] = encoded
        if blob is None:
            continue
        required(lba*512+len(blob) <= len(raw), '发布资源超过64MB')
        struct.pack_into('<2I', raw, at+64, lba, len(blob))
        raw[lba*512:lba*512+len(blob)] = blob
        lba += (len(blob)+511)//512
    return bytes(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, help='第二阶段真实完整验收清单JSON，路径在项目内')
    parser.add_argument('--output', default='build/M8-release', help='新建的独立发布目录；已有目录拒绝覆盖')
    args = parser.parse_args()
    manifest_path = local_file(args.manifest)
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    required(manifest.get('author') == 'mio' and manifest.get('status') == 'PASS'
             and manifest.get('phase') == 2, '只能发布第二阶段完整成功证据')
    # 哈希只证明准确对应字节，不能独自证明视觉/交互。每组必须引用
    # 完整报告和实际截图；报告里的测量、用户视觉要求由第二阶段审阅。
    for group in GROUPS:
        item = manifest['groups'][group]
        required(item.get('status') == 'PASS', f'未通过完整验收：{group}')
        report = local_file(item['report'])
        required(sha(report.read_bytes()) == item['sha256'], f'证据报告变化：{group}')
        required(item.get('screenshots'), f'没有实际截图：{group}')
        for shot in item['screenshots']:
            path = local_file(shot['path'])
            required(sha(path.read_bytes()) == shot['sha256'], f'证据截图变化：{path}')
    fingerprints = {path.relative_to(ROOT).as_posix(): sha(path.read_bytes())
                    for path in (ROOT/'user').rglob('*') if path.is_file()}
    required(manifest['sources'] == fingerprints, '原生生成后用户源码/库已发生变化')
    required(manifest['input_sources'] == input_fingerprints(), '内核/引导/第三方来源在验收后变化')
    required(manifest['api_sha256'] == sha((ROOT/'user/SCAPI.H').read_bytes()), 'API来源不一致')
    font = (ROOT/'kernel/font16.txt').read_bytes()
    required(sha(font) == '8f286f8ac7e9c1d714a2dec6613bb9416660a7232fc8d78ffa8024b3c23e782b', '用户字体发生变化')
    disk_path = local_file(manifest['native_disk']['path'])
    required(sha(disk_path.read_bytes()) == manifest['native_disk']['sha256'], '真实原生盘变化')
    native = read_volume(disk_path)
    # 当前发布树是出厂资源输入，客体测试盘只提供明确列出的原生产物；
    # 不复制测试临时文件/存档/坏图片，不把用户作品移进新出厂盘。
    files = {}
    for path in (ROOT/'build/fs').rglob('*'):
        if path.is_file():
            key = fold(path.relative_to(ROOT/'build/fs').as_posix())
            required(key not in files, f'重复资源树路径：{key}')
            files[key] = path.read_bytes()
    required(manifest['factory_resources'] == {name: sha(blob) for name, blob in files.items()},
             '出厂资源树在验收后变化，不能混入另一批图标/配置/程序')
    native_paths = set()
    for path in (ROOT/'user').glob('*.c'):
        name = path.stem
        if name == 'cli':
            native_paths.update(f'BIN/{name.upper()}.SCX' for name in CLI)
        else:
            prefix = 'BIN' if name in ('s3c', 'shell', 'assembler') else 'APPS'
            native_paths.add(f'{prefix}/{"ASM" if name == "assembler" else name.upper()}.SCX')
    required(set(manifest['outputs']) == native_paths, '原生清单没有覆盖全部正式C程序/19CLI')
    for target in sorted(native_paths):
        item = manifest['outputs'][target]
        blob = native[fold(item['path'])]
        required(item['source'] == 'SCCC_GUEST' and sha(blob) == item['sha256'], f'原生产物来源不一致：{target}')
        scx_check(blob)
        mapping = native[fold(item['map_path'])]
        required(mapping is not None and sha(mapping) == item['map_sha256'], f'符号变化：{target}')
        files[target] = blob
        files[target+'.MAP'] = mapping
    codec = files['SYS/CORE/IMAGE.SCX']
    required(manifest['codec']['source'] == 'HOST_BOOTSTRAP' and sha(codec) == manifest['codec']['sha256'], '解码服务来源未准确标注')
    scx_check(codec)
    floppy = (ROOT/'build/sandcore.img').read_bytes()
    required(len(floppy) == 1474560 and sha(floppy) == manifest['floppy_sha256'], '引导盘变化')
    # 输出只新建；不能用--output覆写默认沙核盘、用户盘或已封存验收包。
    output = (ROOT/args.output).resolve()
    required(output.is_relative_to((ROOT/'build').resolve()) and output != (ROOT/'build').resolve()
             and not output.exists(), '发布目录必须是build下尚未存在的新目录')
    data = factory_volume(files)
    output.mkdir(parents=True)
    (output/'sandcore.img').write_bytes(floppy)
    (output/'sanddata.img').write_bytes(data)
    provenance = {'author': 'mio', 'status': 'NATIVE_ASSEMBLED_REQUIRES_FINAL_COLD_BOOT',
                  'manifest_sha256': sha(manifest_path.read_bytes()), 'sources': fingerprints,
                  'input_sources': manifest['input_sources'], 'groups': manifest['groups'],
                  'codec': manifest['codec'], 'files': {name: sha(blob) for name, blob in files.items() if blob is not None},
                  'sandcore_sha256': sha(floppy), 'sanddata_sha256': sha(data)}
    (output/'provenance.json').write_text(json.dumps(provenance, ensure_ascii=False, indent=2), encoding='utf-8')
    # 新盘装配后还要在这两个精确镜像上做冷启动与验收回归；装配成功
    # 不自封完整PASS，不改active goal或生成伪造的运行结果。
    print(f'原生发布盘已装配，尚须最终冷启动验收：{output}')


if __name__ == '__main__':
    main()
