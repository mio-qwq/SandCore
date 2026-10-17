"""复用小型编译中间物并保存本次输入差异；不复制数据盘。"""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
OLD=ROOT/'build/m10a2-main-appfix-01'
NEW=ROOT/'build/m10a2-main-release-drag-01'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    resume=sys.argv[1:]==['--resume-owned-preparation']
    if NEW.exists() and not resume:raise FileExistsError(NEW)
    if (HERE/'PREPARATION.json').exists():raise FileExistsError('已有正式准备凭证，不能重放')
    NEW.mkdir(exist_ok=resume);copied=[]
    for path in OLD.rglob('*'):
        if not path.is_file() or path.suffix.lower() not in {'.o','.d','.h','.elf','.bin','.scl','.scx','.scu','.sym','.img','.skm'}:continue
        relative=path.relative_to(OLD)
        if path.suffix.lower()=='.img' and relative.as_posix()!='sandcore.img':continue
        target=NEW/relative;expected=path.read_bytes()
        if path.suffix=='.d':expected=expected.replace(b'build/m10a2-main-appfix-01',b'build/m10a2-main-release-drag-01')
        if target.exists() and target.read_bytes()!=expected:raise ValueError('本次中断目录含未知修改：'+str(relative))
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        if path.suffix=='.d':target.write_bytes(target.read_bytes().replace(b'build/m10a2-main-appfix-01',b'build/m10a2-main-release-drag-01'))
        copied.append({'path':relative.as_posix(),'bytes':target.stat().st_size,'sha256':sha(target)})
    changed=subprocess.check_output(['git','diff','--relative','--name-only','2728eb6','--','kernel','user','tools','m10a2.mk'],cwd=ROOT).decode().splitlines()
    preimages={}
    for path in changed:
        body=subprocess.check_output(['git','show','2728eb6:sandcore/'+path],cwd=ROOT)
        target=HERE/'before'/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(body)
        preimages[path]={'before_sha256':hashlib.sha256(body).hexdigest(),'after_sha256':sha(ROOT/path)}
    record={'baseline_commit':'2728eb6','source_build':str(OLD.relative_to(ROOT)),'new_build':str(NEW.relative_to(ROOT)),
        'data_disks_copied':False,'files':len(copied),'bytes':sum(x['bytes'] for x in copied),'preimages':preimages,'artifacts':copied}
    (HERE/'PREPARATION.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:record[key] for key in ('files','bytes','data_disks_copied')}))

if __name__=='__main__':main()
