"""为两处确证应用修复复用编译中间物；不复制数据盘或覆盖HOST15凭证。"""
from pathlib import Path
import hashlib,json,shutil,subprocess

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
OLD=ROOT/'build/m10a2-main-pio-01'
NEW=ROOT/'build/m10a2-main-appfix-01'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    if NEW.exists():raise FileExistsError(NEW)
    NEW.mkdir()
    kept=[]
    suffixes={'.o','.d','.h','.elf','.bin','.scl','.scx','.scu','.sym','.img','.skm'}
    for path in OLD.rglob('*'):
        if not path.is_file() or path.suffix.lower() not in suffixes:continue
        rel=path.relative_to(OLD)
        # 这里只复用构建器产物；严禁把用户/测试数据盘当中间物复制。
        if path.suffix.lower()=='.img' and rel.as_posix()!='sandcore.img':continue
        target=NEW/rel;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(path,target)
        if path.suffix=='.d':
            source=target.read_bytes();target.write_bytes(source.replace(b'build/m10a2-main-pio-01',b'build/m10a2-main-appfix-01'))
        kept.append(dict(path=rel.as_posix(),bytes=target.stat().st_size,sha256=sha(target)))
    preimages={}
    for name in ('user/terminal2/terminal_ui.c','user/settings2/settings_save.inc'):
        body=subprocess.check_output(['git','show','bc55021:sandcore/'+name],cwd=ROOT)
        saved=HERE/'before'/name;saved.parent.mkdir(parents=True,exist_ok=True);saved.write_bytes(body)
        preimages[name]=dict(before_sha256=hashlib.sha256(body).hexdigest(),after_sha256=sha(ROOT/name))
    (HERE/'PREPARATION.json').write_text(json.dumps(dict(source_build=str(OLD.relative_to(ROOT)),new_build=str(NEW.relative_to(ROOT)),files=len(kept),bytes=sum(v['bytes'] for v in kept),data_disks_copied=False,preimages=preimages,artifacts=kept),indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(files=len(kept),bytes=sum(v['bytes'] for v in kept),data_disks_copied=False)))

if __name__=='__main__':main()
