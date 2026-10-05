#!/usr/bin/env python3
"""独立M9验收目录/源快照与带摘要的本地包，不改已有试玩会话。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT=Path(__file__).resolve().parents[1]
PLAY=ROOT.parent/'temp miotest'
DEST=PLAY/'M9-ACCEPTANCE'
ARCHIVE=ROOT/'build/SandCore-M9-acceptance-2026-10-05.zip'

def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while block:=f.read(1024*1024):h.update(block)
    return h.hexdigest()

def copy(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
    if sha(src)!=sha(dst):raise ValueError('复制字节不一致: '+str(src))

def preserved():
    result={}
    for name in ('M9','M9-FLAC'):
        p=PLAY/name/'images'
        result[name]={x.name:sha(x) for x in sorted(p.iterdir()) if x.is_file()}
    return result

def prepare():
    if DEST.exists():raise FileExistsError(DEST)
    DEST.mkdir();before=preserved()
    images={'sandcore.img':ROOT/'build/m9-work/sandcore.img','kernel.sym':ROOT/'build/m9-work/kernel.sym',
      'sanddata.img':ROOT/'build/m9-matrix-input/current-final-v5-31.img',
      'legacy-sanddata.img':ROOT/'build/m9-matrix-input/legacy-final-v5-31.img'}
    for name,p in images.items():copy(p,DEST/'images'/name)
    for name in ('scserial.py','serial_protocol.py','serial_progress.py','qemu_config.py'):copy(ROOT/'tools'/name,DEST/'tools'/name)
    launch=(PLAY/'M9-FLAC/launch.py').read_text(encoding='utf-8')
    launch=launch.replace('M9 测试版；完整验收仍在进行。','M9 验收候选包；验证范围和操作见 docs/M9-ACCEPTANCE.md。')
    (DEST/'launch.py').write_text(launch,encoding='utf-8')
    batch=(PLAY/'run-m9-flac.bat').read_bytes().replace(b'M9-FLAC',b'M9-ACCEPTANCE')
    batch.decode('ascii');(PLAY/'run-m9-acceptance.bat').write_bytes(batch)
    manifest=dict(status='ASSEMBLED_FINAL_SMOKE_PENDING',baseline_sha256={n:sha(DEST/'images'/n) for n in images},
      preserved_old_baselines=before,scope='Windows QEMU; local CLI 140/171 supported subsets; 310 reference rows retained',
      source_build='31: kernel30 unchanged; CORE same-pixel rectangle optimization',
      old_specialized_evidence='Per-test historical boot hashes preserved; unchanged subsystem evidence not relabeled as all final tests rerun')
    (DEST/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (DEST/'试玩说明.md').write_text('从上一级run-m9-acceptance.bat启动。:quit正常退出；--fresh从本包基线重开。\n原M9/M9-FLAC会话保留，新包使用自己的sessions目录。完整操作/截图/限制见docs/M9-ACCEPTANCE.md。\n',encoding='utf-8')
    if before!=preserved():raise AssertionError('旧基线变化')
    print('New acceptance directory assembled; old play baselines unchanged')

def seal():
    manifest=json.loads((DEST/'manifest.json').read_text(encoding='utf-8'))
    if manifest['preserved_old_baselines']!=preserved():raise AssertionError('旧基线变化')
    progress=ROOT/'build/m9-package-smoke-20261005-01/verification.json'
    if json.loads(progress.read_text(encoding='utf-8'))['status']!='HOST_PROGRESS_CASES_PASS':raise ValueError('随包工具未通过')
    for n,digest in manifest['baseline_sha256'].items():
        if sha(DEST/'images'/n)!=digest:raise ValueError('新基线变化')
    for p in (ROOT/'docs').rglob('*'):
        if p.is_file():copy(p,DEST/'docs'/p.relative_to(ROOT/'docs'))
    reports=[
      'm9-final-cli-core-20261005-02','m9-cli-behavior-20261005-03','m9-cli-limits-20261005-01',
      'm9-security-extra-20261005-07','m9-session-lifecycle-20261005-02','m9-powercut-20261005-02',
      'm9-iofault-20261005-03','m9-fs-20261005-05','m9-sse-encoding-20261005-01',
      'm9-verify-20261005-19','m9-fallbacks-20261005-01','m9-audio-matrix-20261005-01',
      'm9-mp3-oracle-20261005-02','m9-player-20261005-09','m9-player-nofpu-20261005-01',
      'm9-sound-20261005-03','m9-final-kernel-20261005-01','m9-desktop-fallback-20261005-02',
      'm9-player-stress-20261005-02','m9-performance-20261005-03','m9-package-smoke-20261005-01']
    index=[]
    for name in reports:
        source=ROOT/'build'/name
        matrix=source/'matrix.json'
        if not matrix.exists():matrix=source/'verification.json'
        report=json.loads(matrix.read_text(encoding='utf-8'))
        if report['status']=='FAIL' or report['status']=='RUNNING':raise ValueError('证据未通过: '+name)
        index.append(dict(path='evidence/'+name+'/'+matrix.name,status=report['status'],original=str(source)))
        for p in source.rglob('*'):
            if p.is_file() and (p.suffix=='.json' or p.suffix=='.png' or p.name in ('stdout.txt','stderr.txt')):
                copy(p,DEST/'evidence'/name/p.relative_to(source))
    for name in ('m9-build-integrity-08.json','m9-compat-static-08.json','m9-core-equivalence-01.json'):
        copy(ROOT/'build'/name,DEST/'evidence'/name)
    for disk in ('disk-1','disk-2'):
        for theme in ('aurora','classic'):
            for frame in ('large','mini'):
                # 最终同像素CORE版连续生命周期重新抓取两主题画面。
                copy(ROOT/'build/m9-player-stress-20261005-02'/disk/(theme+'-'+frame+'.png'),DEST/'preview'/(disk+'-'+theme+'-'+frame+'.png'))
    roots=('boot','kernel','modules','user','third_party','assets','tools','docs','tests/m9')
    for directory in roots:
        for p in (ROOT/directory).rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts:copy(p,DEST/'source/sandcore'/p.relative_to(ROOT))
    for name in ('Makefile','m9.mk','README.md'):copy(ROOT/name,DEST/'source/sandcore'/name)
    for name in ('AGENTS.md','HANDOFF.md'):copy(ROOT.parent/name,DEST/'source'/name)
    # 固定M7/M8a巨大历史包仍由原工作区提供；源码快照标清实际复现依赖。
    (DEST/'source/BUILD-NOTE.md').write_text('源码快照不含宿主FFmpeg/QEMU/GCC及历史ZIP。\n复现需固定SandCore-M7-2026-10-02.zip与SandCore-M8a-2026-10-04.zip置于sandcore/build/，摘要见tools/audit_m9_compat.py。\nWSL: make -j4 BUILD=build/m9-work M9_BUILD=1 m9-artifacts。最终两个盘有原创额外测试夹具，来源链/摘要见manifest及evidence。\n',encoding='utf-8')
    manifest.update(status='QEMU_VERIFIED_PENDING_USER_ACCEPTANCE',evidence=index,
      cli=dict(implemented=140,local_total=171,full_total=310,local_missing=31,contract='documented behavior subsets only'))
    (DEST/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    files=[p for p in DEST.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.name!='SHA256SUMS.txt']
    (DEST/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(DEST).as_posix()+'\n' for p in sorted(files)),encoding='utf-8')
    if ARCHIVE.exists():raise FileExistsError(ARCHIVE)
    with zipfile.ZipFile(ARCHIVE,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        z.write(PLAY/'run-m9-acceptance.bat','run-m9-acceptance.bat')
        for p in sorted(DEST.rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:z.write(p,'M9-ACCEPTANCE/'+p.relative_to(DEST).as_posix())
    with zipfile.ZipFile(ARCHIVE) as z:
        if z.testzip():raise ValueError('ZIP回读CRC错误')
        for n,digest in manifest['baseline_sha256'].items():
            if hashlib.sha256(z.read('M9-ACCEPTANCE/images/'+n)).hexdigest()!=digest:raise ValueError('ZIP镜像摘要变化')
    ARCHIVE.with_suffix('.zip.sha256').write_text(sha(ARCHIVE)+'  '+ARCHIVE.name+'\n',encoding='ascii')
    print('Acceptance ZIP verified: '+str(ARCHIVE)+'; '+str(ARCHIVE.stat().st_size)+' bytes')

def main():
    p=argparse.ArgumentParser();p.add_argument('--seal',action='store_true');a=p.parse_args()
    if a.seal:seal()
    else:prepare()

if __name__=='__main__':main()
