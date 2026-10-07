"""Read-only binary/platform checks. Run after make; does not start a VM."""
from pathlib import Path
import hashlib, json, struct, subprocess
from pack_payload import compress, decompress
root=Path(__file__).resolve().parents[1]
records=[]
for name in ['pcalc','sheet','hexed','blocks','raider','SandCore_ExtraSoftware_Pack_1']:
    path=root/('dist' if name.startswith('SandCore_') else 'build')/(name+'.scx')
    data=path.read_bytes();magic,entry,load,bss,stack,flags,base,author=struct.unpack_from('<8s7I',data)
    assert magic==b'SCX1MIO\0' and entry==0 and 0<load<=262108
    assert load+bss<=0x3D0000 and stack==131072 and flags==2 and base==0x400000 and author==0x004F494D
    for identity in [b'SandCore_ExtraSoftware_Pack_1',b'copyright (c) mio 2026',b'https://github.com/mio-qwq/',b'made with']:
        assert identity in data[36:36+load], (name,identity)
    icon=data[36+load:];assert icon[:8]==b'SCB2MIO\0'
    w,h,fmt,ifl,body,ia=struct.unpack_from('<6I',icon,8)
    assert (w,h,fmt,ifl,body,ia)==(32,32,2,0,4096,0x004F494D) and len(icon)==4128
    assert decompress(compress(data))==data
    unresolved=subprocess.check_output(['nm','-u',str(root/'build'/(name+'.pe'))],text=True)
    assert not unresolved.strip(), unresolved
    records.append(dict(name=name,bytes=len(data),load=load,bss=bss,sha256=hashlib.sha256(data).hexdigest()))
for name in ['SCAPI.H','NUI.inc','SCMEM.inc','SCMEM.H']:
    assert (root.parent/'libex'/name).read_bytes()==(root.parents[1]/'sandcore/user'/name).read_bytes(), name+' modified'
assert (root.parent/'tools/mkscx.py').read_bytes()==(root.parents[1]/'sandcore/tools/mkscx.py').read_bytes()
(root/'build/preflight.json').write_text(json.dumps(records,indent=2)+'\n')
for row in records:print(f"{row['name']}: {row['bytes']} B / load {row['load']} B / BSS {row['bss']} B / SCX, icon, roundtrip, symbols OK")
print('Platform ABI and UI fragments unchanged; all checks passed. No guest boot.')
