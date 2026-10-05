#!/usr/bin/env python3
"""源码备份及每版一份精简构建包；只读历史包，按原字节核对镜像和来源。"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'sandcore/build'
OUT=ROOT/'releases'
VERSIONS={
 'M6':('SandCore-M6-2026-10-02.zip','65d61fd4cf33f287d97259becf08d59cb2e865bd20a3ac596542102193ed7d09'),
 'M7':('SandCore-M7-2026-10-02.zip','516811f6477c33a5028f8bfd736d9f7b98cdfc40bce84d35ae730f298d3278fc'),
 'M8a':('SandCore-M8a-2026-10-04.zip','e9db18c1dd3c6987c4b66a2a33e1bf354e39b6da8b2b920db2cb366f42ca61e5'),
 'M9':('SandCore-M9-acceptance-2026-10-05.zip','0f06e20cf1ca69cd4020d301aa21be14482ae00a7c5c3c597155618d150d0e3d')}

def sha(raw):return hashlib.sha256(raw).hexdigest()

def file_sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while raw:=f.read(1048576):h.update(raw)
    return h.hexdigest()

def safe(name):
    path=PurePosixPath(name)
    if path.is_absolute() or any(x=='..' or ':' in x or '\\' in x for x in path.parts):
        raise ValueError('来源成员路径不安全')
    return path

def put(path,raw):
    path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists():
        if path.read_bytes()!=raw:raise ValueError('已有文件不同，禁止覆盖: '+str(path))
    else:path.write_bytes(raw)

def snapshot(version,archive):
    records=[]
    for info in archive.infolist():
        if info.is_dir() or not info.filename.startswith('sandcore/') or info.filename.startswith('sandcore/build/'):
            continue
        rel=safe(info.filename);raw=archive.read(info)
        put(ROOT/'snapshots'/version/rel,raw)
        records.append(dict(path=info.filename,size=len(raw),sha256=sha(raw)))
    return records

def make_product(version,archive,source,origin_sha):
    files={};origins={}
    def take(name,target=None):
        target=target or name;files[target]=archive.read(name);origins[target]=name
    if version!='M9':
        for name in ('sandcore.img','sanddata.img'):take('sandcore/build/'+name,'build/'+name)
        take('sandcore/run.bat','run.bat')
    else:
        take('run-m9-acceptance.bat')
        take('M9-ACCEPTANCE/launch.py')
        for name in ('sandcore.img','sanddata.img','kernel.sym'):take('M9-ACCEPTANCE/images/'+name)
        for info in archive.infolist():
            if info.filename.startswith('M9-ACCEPTANCE/tools/') and info.filename.endswith('.py'):take(info.filename)
        baselines={Path(k).name:sha(v) for k,v in files.items() if k.startswith('M9-ACCEPTANCE/images/')}
        files['M9-ACCEPTANCE/manifest.json']=(json.dumps(dict(status='QEMU_VERIFIED_PENDING_USER_ACCEPTANCE',baseline_sha256=baselines),indent=2)+'\n').encode()
        # 运行代码和协议工具不改；帮助页指向Git中完整的验收说明与截图，避免重复打包。
        files['M9-ACCEPTANCE/docs/M9-ACCEPTANCE.md']=('完整说明与截图：\n\nhttps://github.com/mio-qwq/SandCore/blob/main/sandcore/docs/M9-ACCEPTANCE.md\n').encode('utf-8')
    for name,raw in files.items():
        if name.endswith('.bat'):raw.decode('ascii')
    status={'M6':'历史M6交付，独立界面验收未另确认','M7':'用户已验收的历史M7交付',
            'M8a':'用户批准的M8a部分发布，原完整M8未完成目标继续封存',
            'M9':'Windows QEMU验证通过，待用户验收'}[version]
    files['README.md']=(f'# SandCore {version} 精简构建产物\n\n{status}。\n\n'
        '仅取该版本一套原启动盘、数据盘及启动工具；镜像和原运行工具逐字节不变。\n'
        '安装 Windows QEMU 后解压运行 '+('run-m9-acceptance.bat（另需Python 3.12）' if version=='M9' else 'run.bat')+'。\n'
        '数据盘运行时会产生用户修改，请先保留一份解压基线。\n\n'
        '源码、文档和第三方许可见 https://github.com/mio-qwq/SandCore 。\n').encode('utf-8')
    entries=[dict(path=k,size=len(v),sha256=sha(v),origin_member=origins.get(k,'lightweight package metadata')) for k,v in sorted(files.items())]
    manifest=dict(version=version,status=status,source_archive=source,source_archive_sha256=origin_sha,files=entries)
    target=OUT/('SandCore-'+version+'-build.zip')
    if target.exists():raise ValueError('精简包已存在，禁止覆盖')
    with zipfile.ZipFile(target,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as result:
        for name,raw in sorted(files.items()):
            info=zipfile.ZipInfo(name,date_time=(2026,10,5,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            result.writestr(info,raw,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
        result.writestr('manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2).encode('utf-8'))
    # 实际重新打开新包检查全部CRC和SHA，而不是只核对待写入的缓冲。
    with zipfile.ZipFile(target) as check:
        if check.testzip():raise ValueError('构建包CRC失败')
        for item in entries:
            raw=check.read(item['path'])
            if len(raw)!=item['size'] or sha(raw)!=item['sha256']:raise ValueError('构建包内容不一致')
    return dict(version=version,file=target.name,size=target.stat().st_size,sha256=file_sha(target),
                source_archive=source,source_archive_sha256=origin_sha,verified_files=len(entries),image_count=2,status=status)

def main():
    OUT.mkdir(exist_ok=True);products=[];sources={}
    for version,(name,expected) in VERSIONS.items():
        original=BUILD/name
        if file_sha(original)!=expected:raise ValueError('原交付包摘要不一致: '+name)
        with zipfile.ZipFile(original) as archive:
            if version!='M9':sources[version]=snapshot(version,archive)
            products.append(make_product(version,archive,name,expected))
        if file_sha(original)!=expected:raise ValueError('原交付包被修改: '+name)
        print(version+' PASS '+str(products[-1]['size'])+' bytes',flush=True)
    extra=BUILD/'m6a-source-baseline.zip'
    with zipfile.ZipFile(extra) as archive:sources['M6a']=snapshot('M6a',archive)
    metadata=dict(status='CRC_SHA256_AND_ORIGINAL_IMAGE_BYTES_PASS',products=products,
                  product_bytes=sum(p['size'] for p in products),historical_sources=sources)
    (OUT/'MANIFEST.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (OUT/'SHA256SUMS.txt').write_text(''.join(p['sha256']+'  '+p['file']+'\n' for p in products),encoding='ascii')
    print('TOTAL_PRODUCTS '+str(metadata['product_bytes'])+' bytes',flush=True)

if __name__=='__main__':main()
