#!/usr/bin/env python3
"""保存本批真实源码摘要、客体字节和定向证据，不复制镜像或整树。"""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]

def main():
    host = ROOT / 'build/m10a2-stdlib-guest-04'
    native = ROOT / 'build/m10a2-stdlib-guest-05'
    out = ROOT / 'evidence/m10a2-stdlib'
    if out.exists():
        raise FileExistsError('不能覆盖已保存的证据')
    reports = [json.loads((folder / 'stdlib.json').read_text(encoding='utf-8'))
               for folder in (host, native)]
    for report in reports:
        if not report['status'].startswith('DECLARED_') or not report['current_inputs_unchanged'] or not report['source_disk_unchanged']:
            raise ValueError('本批运行没有满足成功及输入保持条件')
        if any(case['status'] not in ('PASS', 'REUSED_PASS') for case in report['cases']):
            raise ValueError('成功批仍含失败步骤')
        for name, digest in report['input_sha256'].items():
            if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
                raise ValueError('已验证源码改变：' + name)
    copies = [
        (ROOT / 'build/m10a2-stdlib-bootstrap/stdlib-build-06.json', 'build.json'),
        (ROOT / 'build/m10a2-stdlib-host/report.json', 'compiler-host.json'),
        (host / 'stdlib.json', 'stdlib.json'), (native / 'stdlib.json', 'native.json'),
        (host / 'stdlib-desktop.png', 'host-desktop.png'),
        (native / 'stdlib-desktop.png', 'native-desktop.png'),
        (host / 'console.txt', 'host-console.txt'), (native / 'console.txt', 'native-console.txt'),
    ]
    for number, label in ((1, 'cold-desktop'), (2, 'unsupported-shell'), (3, 'double-job-close')):
        folder = ROOT / ('build/m10a2-stdlib-guest-%02d' % number)
        copies.append((folder / 'stdlib.json', 'failures/' + label + '.json'))
        if (folder / 'failed-code').exists():
            for path in (folder / 'failed-code').iterdir():
                if path.is_file():
                    copies.append((path, 'failures/' + label + '/' + path.name))
    for folder, label in ((host / 'native-libraries', 'native-libraries'),
                          (host / 'native-artifacts', 'native-artifacts')):
        for path in folder.iterdir():
            if path.is_file():
                copies.append((path, label + '/' + path.name))
    for name in ('SHARERUN.SCX',):
        copies.append((host / name, name))
    out.mkdir()
    entries = []
    for source, name in copies:
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            raise ValueError('证据复制不一致')
        entries.append(dict(path=name, bytes=target.stat().st_size, sha256=digest,
                            source=source.relative_to(ROOT).as_posix()))
    (out / 'archive.json').write_text(json.dumps(dict(scope='PHASE3_DIRECTED_EVIDENCE',
        full_M10a2=False, disk_images_copied=False, files=entries), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('ARCHIVED', len(entries), sum(item['bytes'] for item in entries), flush=True)

if __name__ == '__main__':
    main()
