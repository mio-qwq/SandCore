from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[2]
old=ROOT/'build/m10a2-main-session-return-01'
new=ROOT/'build/m10a2-main-product-fix-01'
if new.exists():raise FileExistsError(new)
shutil.copytree(old,new,ignore=shutil.ignore_patterns('*.log','p6-unified-host-*.json'))
for p in new.rglob('*.d'):
    body=p.read_bytes()
    p.write_bytes(body.replace(b'build/m10a2-main-session-return-01',b'build/m10a2-main-product-fix-01'))
print('HOST_INTERMEDIATES_REUSED_HISTORICAL_DIRECTORY_UNCHANGED')
