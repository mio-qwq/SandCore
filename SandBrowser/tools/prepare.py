"""Copyright (c) 2026 mio. 仅在本工程内展开固定依赖并准备构建目录。"""
from pathlib import Path
import hashlib, json, os, subprocess, tarfile

ROOT = Path(__file__).resolve().parents[1]
for folder in ('build/obj', 'build/tmp', 'dist', 'src', 'include', 'resources', 'vendor'):
    (ROOT / folder).mkdir(parents=True, exist_ok=True)
os.environ['TMPDIR'] = str(ROOT / 'build/tmp')
os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
names = {'duktape-2.7.0.tar.xz': 'duktape-2.7.0', 'bearssl-0.6.tar.gz': 'bearssl-0.6', 'gumbo-0.10.1.tar.gz': 'gumbo-parser-0.10.1', 'litehtml-0.9.tar.gz':'litehtml-0.9', 'musl-1.2.5.tar.gz':'musl-1.2.5','stb.tar.gz':'stb-master','miniz-3.0.2.tar.gz':'miniz-3.0.2'}
for archive, directory in names.items():
    if (ROOT / 'vendor' / directory).exists():
        continue
    with tarfile.open(ROOT / 'vendor/archives' / archive) as tf:
        for member in tf.getmembers():
            target = (ROOT / 'vendor' / member.name).resolve()
            if not target.is_relative_to(ROOT / 'vendor'):
                raise ValueError('Unsafe archive path: ' + member.name)
        tf.extractall(ROOT / 'vendor', filter='data')
    print('Prepared', directory)
# Git只保存小型适配补丁。校验原始/已适配状态后应用，避免覆盖用户改动。
patch_dir = ROOT / 'vendor/patches'
patch_lock = json.loads((patch_dir / 'litehtml-0.9.json').read_text())
states = []
for item in patch_lock:
    target = ROOT / 'vendor/litehtml-0.9' / item['path']
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    if digest not in (item['before_sha256'], item['after_sha256']):
        raise ValueError('Unexpected local vendor changes: ' + item['path'])
    states.append(digest == item['after_sha256'])
if not all(states):
    if any(states):
        raise ValueError('Partially patched litehtml; inspect vendor/litehtml-0.9 before rebuilding')
    subprocess.run(['patch', '--batch', '--forward', '-p1', '-i',
                    str(patch_dir / 'litehtml-0.9.patch')],
                   cwd=ROOT / 'vendor/litehtml-0.9', check=True)
    for item in patch_lock:
        target = ROOT / 'vendor/litehtml-0.9' / item['path']
        if hashlib.sha256(target.read_bytes()).hexdigest() != item['after_sha256']:
            raise ValueError('Vendor patch result differs: ' + item['path'])
sysroot=ROOT/'build/sysroot'
sysroot.mkdir(exist_ok=True)
if not (sysroot/'usr').exists():
    subprocess.run(['dpkg-deb','-x',str(ROOT/'vendor/archives/lib32stdcpp.deb'),str(sysroot)],check=True)
if not (sysroot/'usr/lib/gcc/x86_64-linux-gnu/15/32/libgcc.a').exists():
    subprocess.run(['dpkg-deb','-x',str(ROOT/'vendor/archives/lib32gcc.deb'),str(sysroot)],check=True)
stubs=ROOT/'include/gnu'
stubs.mkdir(parents=True,exist_ok=True)
(stubs/'stubs-32.h').write_text('/* SandCore supplies its own runtime; no Linux ABI entry points. */\n')
probe = ROOT / 'build/compiler-probe.cpp'
probe.write_text('#include <vector>\n#include <string>\nint f(){std::vector<std::string> x; x.push_back("probe"); return x.size();}\n')
result = subprocess.run(['g++', '-m32', '-fno-exceptions', '-fno-rtti','-I'+str(ROOT/'include'),'-I'+str(sysroot/'usr/include/x86_64-linux-gnu/c++/15/32'),'-I/usr/include/x86_64-linux-gnu', '-c', str(probe), '-o', str(ROOT/'build/compiler-probe.o')], capture_output=True, text=True)
(ROOT/'build/compiler-probe.json').write_text(json.dumps({'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2))
print('C++ i386 headers:', 'available' if not result.returncode else result.stderr[:600])
