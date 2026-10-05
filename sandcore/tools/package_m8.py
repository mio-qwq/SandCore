#!/usr/bin/env python3
"""mio：完整M8验收包封存工具，第一阶段写源码，第二阶段批准后才执行。

输入是publish_m8_native产生的独立双盘、完整阶段清单及该双盘最终
冷启动报告。所有报告、截图、源、双盘精确摘要先核对，再封存与重读。
没有最终成功证据拒绝打包；既有失败/历史阶段留原处，不冒充本版成功。
输出为新文件，不改默认/用户盘，不自动启动QEMU或宣告goal完成。
"""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path
from publish_m8_native import ROOT, GROUPS, input_fingerprints, local_file, required, sha

M7_SHA = '516811f6477c33a5028f8bfd736d9f7b98cdfc40bce84d35ae730f298d3278fc'
CONTENT_DIRS = ('boot', 'kernel', 'modules', 'user', 'docs', 'tools', 'assets', 'legacy', 'third_party')
CONTENT_FILES = ('README.md', 'Makefile', 'linker.ld', 'build.bat', 'run.bat', 'run.sh')


def digest_file(path):
    # 镜像/历史ZIP可能很大，流式计算不把所有资料同时装进宿主内存。
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while True:
            block = stream.read(1024*1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def referenced_report(item):
    required(item['status'] == 'PASS' and item.get('screenshots'), '没有完整报告/真实截图')
    paths = [local_file(item['report'])]
    required(digest_file(paths[0]) == item['sha256'], '报告摘要变化')
    for record in item['screenshots']:
        path = local_file(record['path'])
        required(digest_file(path) == record['sha256'], '截图摘要变化')
        paths.append(path)
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, help='完整第二阶段验收清单')
    parser.add_argument('--release', required=True, help='独立原生发布目录，位于build内')
    parser.add_argument('--cold-boot', required=True, help='该精确双盘最终冷启动验收记录')
    parser.add_argument('--output', default='build/SandCore-M8-2026-10-03.zip', help='新建ZIP，已有文件拒绝覆盖')
    args = parser.parse_args()
    manifest_path = local_file(args.manifest)
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    required(manifest.get('phase') == 2 and manifest.get('author') == 'mio'
             and manifest.get('status') == 'PASS', '缺少第二阶段完整成功清单')
    required(manifest['input_sources'] == input_fingerprints(), '验收输入源已变化')
    sources = {p.relative_to(ROOT).as_posix(): digest_file(p) for p in (ROOT/'user').rglob('*') if p.is_file()}
    required(manifest['sources'] == sources, '原生生成后源码已变化')
    release = (ROOT/args.release).resolve()
    required(release.is_relative_to((ROOT/'build').resolve()) and release.is_dir(), '发布目录越界或不存在')
    provenance_path = local_file('provenance.json', release)
    provenance = json.loads(provenance_path.read_text(encoding='utf-8'))
    required(provenance['author'] == 'mio' and provenance['manifest_sha256'] == digest_file(manifest_path)
             and provenance['sources'] == sources and provenance['input_sources'] == manifest['input_sources'], '发布来源不一致')
    floppy = local_file('sandcore.img', release)
    data = local_file('sanddata.img', release)
    required(digest_file(floppy) == provenance['sandcore_sha256']
             and digest_file(data) == provenance['sanddata_sha256'], '发布双盘发生变化')
    cold_path = local_file(args.cold_boot)
    cold = json.loads(cold_path.read_text(encoding='utf-8'))
    required(cold.get('phase') == 2 and cold.get('author') == 'mio' and cold.get('status') == 'PASS'
             and cold['sandcore_sha256'] == provenance['sandcore_sha256']
             and cold['sanddata_sha256'] == provenance['sanddata_sha256'], '最终冷启动没有绑定此精确双盘')
    # 冷启动报告不能只是输入盘哈希：完整视觉/性能/组件/API回归结果
    # 由第二阶段真实验证产生，并沿用分组合同。这里只核对来源和完整性。
    selected = {manifest_path, cold_path, provenance_path, floppy, data}
    for group in GROUPS:
        selected.update(referenced_report(manifest['groups'][group]))
        selected.update(referenced_report(cold['groups'][group]))
    selected.add(local_file(manifest['native_disk']['path']))
    required(digest_file(local_file(manifest['native_disk']['path'])) == manifest['native_disk']['sha256'], '原生生成盘变化')
    for directory in CONTENT_DIRS:
        selected.update(p for p in (ROOT/directory).rglob('*') if p.is_file()
                        and '__pycache__' not in p.parts and p.suffix not in ('.pyc', '.tmp'))
    selected.update(local_file(name) for name in CONTENT_FILES)
    # 已验收M7包完整保留，不能重建一份同名SCX称为LEGACY基线。
    m7 = local_file('build/SandCore-M7-2026-10-02.zip')
    required(digest_file(m7) == M7_SHA, '历史M7验收包变化')
    selected.add(m7)
    # 根工作区指令/目标/交接是独立交付内容，来源只允许明确三个路径。
    root_docs = {ROOT.parent/name for name in ('AGENTS.md', 'HANDOFF.md', 'M8_GOAL.md')}
    required(all(p.is_file() for p in root_docs), '缺少工作区交接/目标')
    entries = {f'sandcore/{p.relative_to(ROOT).as_posix()}': p for p in selected}
    entries.update({p.name: p for p in root_docs})
    # 解压后run.bat直接使用build/sandcore.img和build/sanddata.img，
    # 因此明确另放运行双盘，不能让旧HOST开发盘占默认执行位置。
    entries['sandcore/build/sandcore.img'] = floppy
    entries['sandcore/build/sanddata.img'] = data
    hashes = {name: digest_file(path) for name, path in entries.items()}
    output = (ROOT/args.output).resolve()
    required(output.is_relative_to((ROOT/'build').resolve()) and output.suffix == '.zip'
             and not output.exists(), '只允许build下尚未存在的新ZIP')
    output.parent.mkdir(parents=True, exist_ok=True)
    inventory = {'author': 'mio', 'status': 'VERIFIED_INPUTS_ARCHIVED', 'phase': 2,
                 'manifest_sha256': digest_file(manifest_path), 'files': hashes}
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, path in sorted(entries.items()):
            archive.write(path, name)
        archive.writestr('M8-INVENTORY.json', json.dumps(inventory, ensure_ascii=False, indent=2).encode('utf-8'))
    # 逐项重新从ZIP读取，不把“zip写完无异常”当作字节完整证明。
    with zipfile.ZipFile(output) as archive:
        required(archive.testzip() is None, 'ZIP CRC失败')
        for name, expected in hashes.items():
            digest = hashlib.sha256()
            with archive.open(name) as stream:
                while True:
                    block = stream.read(1024*1024)
                    if not block:
                        break
                    digest.update(block)
            required(digest.hexdigest() == expected, f'ZIP内字节变化：{name}')
    summary = {'author': 'mio', 'archive': str(output), 'sha256': digest_file(output),
               'files': len(entries), 'status': 'CRC_AND_SHA256_RECHECKED', 'phase': 2}
    output.with_suffix('.sha256.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'完整验收资料已封存：{output}')


if __name__ == '__main__':
    main()
