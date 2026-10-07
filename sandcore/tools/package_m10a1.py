#!/usr/bin/env python3
"""只打包已验收候选、源码与有效证据；不启动构建或QEMU。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
EVIDENCE = (
    'm10a1-curl-final-01', 'm10a1-network-13', 'm10a1-dhcp-lifecycle-03',
    'm10a1-dhcp-long-03', 'm10a1-core-11', 'm10a1-preserved-recovery-01',
    'm10a1-font-06', 'm10a1-font-edges-02', 'm10a1-notes-03',
    'm10a1-taskstate-03', 'm10a1-lifecycle-01', 'm10a1-sessions-12',
    'm10a1-fault-desktop-04', 'm10a1-fault-multicard-01',
    'm10a1-network-resources-01', 'm10a1-tcp-faults-01',
    'm10a1-shell-01', 'm10a1-scheduling-before-03',
    'm10a1-scheduling-after-03',
)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024*1024):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT/'build/SandCore-M10a1-acceptance.zip')
    args = parser.parse_args()
    destination = args.out.resolve()
    if not destination.is_relative_to(ROOT/'build'):
        raise ValueError('验收包只写入项目build目录')
    curl = json.loads((ROOT/'build/m10a1-curl-final-01/curl.json').read_text(encoding='utf-8'))
    audit = json.loads((ROOT/'build/m10a1/final-image-audit.json').read_text(encoding='utf-8'))
    if curl['status'] != 'DECLARED_CURL_AND_STARTUP_PASS' or not all(c['status']=='PASS' for c in curl['cases']):
        raise ValueError('curl有限验收未通过')
    if digest(ROOT/'build/m10a1/sanddata.img') != audit['final_disk_sha256']:
        raise ValueError('最终数据盘与只读交付审计不一致')
    files = {}

    def add(name, path):
        path = path.resolve(strict=True)
        if path.is_symlink() or name in files:
            raise ValueError('重复成员或链接：'+name)
        files[name] = path

    for name in ('sandcore.img', 'sanddata.img', 'core.sym'):
        add('images/'+name, ROOT/'build/m10a1'/name)
    add('sanddata_editor.exe', REPO/'sanddata_editor/build/m10a1/sanddata_editor.exe')
    add('launch.py', ROOT/'tools/launch_m10a1_acceptance.py')
    add('README.md', ROOT/'docs/M10A1-ACCEPTANCE.md')
    for name in ('scserial.py', 'serial_protocol.py', 'serial_progress.py', 'qemu_config.py', 'm10_guest_boot.py'):
        add('tools/'+name, ROOT/'tools'/name)
    for path in sorted((ROOT/'docs').glob('*.md')):
        add('docs/'+path.name, path)
    for path in sorted((ROOT/'docs').glob('M10*.tsv')):
        add('docs/'+path.name, path)
    # 源码直接流式入ZIP，不冻结整树、不复制新的256MiB测试盘。
    # 限定当前产品目录，排除用户文件、旧发布包和历史快照；完整上游固定
    # 源码与许可仍同时在source/third_party和客体/SYS/LICENSE中。
    names = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=REPO).decode('utf-8').split('\0')
    for name in sorted(set(names)):
        if not name or not (name.startswith(('sandcore/', 'sanddata_editor/')) or name in ('AGENTS.md', 'HANDOFF.md', 'README.md', 'run-m10a1.bat', 'sign-m10a1-yourself.bat', '.gitignore')):
            continue
        path = REPO/name
        if '/build/' in name or path.suffix.lower() in ('.exe', '.img'):
            continue
        if path.is_file():
            add('source/'+name, path)
    for directory in EVIDENCE:
        path = ROOT/'build'/directory
        if not path.is_dir():
            raise ValueError('有效证据目录缺失：'+directory)
        for entry in sorted(path.rglob('*')):
            if entry.is_file() and entry.suffix.lower() in ('.json', '.log', '.txt'):
                add('evidence/'+directory+'/'+entry.relative_to(path).as_posix(), entry)
    # 只选代表图，不把所有旧截图再次压包。
    selected = {
        'screenshots/desktop.png': ROOT/'build/m10a1-curl-final-01/desktop.png',
        'screenshots/notes-chinese.png': ROOT/'build/m10a1-notes-03/disk-1/Notes-original-Chinese-source.png',
    }
    for name, path in selected.items():
        add(name, path)
    for directory, limit in (('m10a1-sessions-12', 3), ('m10a1-fault-multicard-01', 2)):
        for path in sorted((ROOT/'build'/directory).rglob('*.png'))[:limit]:
            add('screenshots/'+directory+'/'+path.relative_to(ROOT/'build'/directory).as_posix(), path)
    for directory in ('m10a1',):
        for path in sorted((ROOT/'build'/directory).glob('*.json')):
            add('evidence/final/'+path.name, path)
    for path in sorted((REPO/'sanddata_editor/build/m10a1-final-check').glob('*')):
        if path.is_file() and path.suffix.lower() in ('.json', '.log', '.txt'):
            add('evidence/editor/'+path.name, path)
    manifest = dict(status='DEVELOPMENT_ACCEPTANCE_PACKAGE', user_acceptance='PENDING',
                    release_created=False, private_key_included=False,
                    git_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO).decode().strip(),
                    core_sha256=curl['core_sha256'], curl_declared_checks=len(curl['cases']),
                    network_tools=19, original_reference_coverage='18/55 plus curl',
                    final_image_audit=audit, files=[])
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('xb') as output, zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as package:
        for name, path in sorted(files.items()):
            before = path.stat()
            package.write(path, name)
            file_sha = digest(path)
            after = path.stat()
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise RuntimeError('打包期间输入变化：'+str(path))
            manifest['files'].append(dict(path=name, bytes=before.st_size, sha256=file_sha))
        launcher = '@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\npython launch.py %*\r\nexit /b %errorlevel%\r\n'
        package.writestr('run-m10a1.bat', launcher.encode('ascii'))
        package.writestr('manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    # 只检查一次ZIP成员CRC与必要入口，不再次运行客体行为。
    with zipfile.ZipFile(destination) as package:
        if package.testzip() is not None:
            raise RuntimeError('验收包CRC失败')
        required = {'images/sandcore.img', 'images/sanddata.img', 'launch.py', 'run-m10a1.bat', 'sanddata_editor.exe', 'manifest.json'}
        if not required <= set(package.namelist()):
            raise RuntimeError('验收包入口缺失')
    result = dict(status='PACKAGE_CRC_AND_CONTENTS_PASS', package=str(destination), bytes=destination.stat().st_size,
                  sha256=digest(destination), files=len(files)+2, git_commit=manifest['git_commit'])
    destination.with_suffix('.zip.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
