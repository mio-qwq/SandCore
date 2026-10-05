import sys,json,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'sandcore/tools'))
from scserial import QemuSession
from verify_m9 import Guest,png_from_ppm
base=root/'backup-output/clean-clone-m9-02/sandcore/build/m9-work'
out=root/'sandcore/build/m9-clean-clone-native-01'
report=dict(scope='clean clone M9 native compiler and screenshot smoke',cases=[])
with QemuSession(base/'sandcore.img',base/'sanddata.img',out,'tcg',True) as vm:
    guest=Guest(vm)
    try:
        guest.connect()
        guest.put_bytes(b'#include "SCAPI.H"\nint main(){sc_puts("CLEAN-SOURCE-BUILD-PASS\\n");return 23;}\n','/TMP/CLEAN.C','source')
        for command,expected,marker in [('sccc /TMP/CLEAN.C /TMP/CLEAN.SCX',0,None),('/TMP/CLEAN.SCX',23,b'CLEAN-SOURCE-BUILD-PASS')]:
            code,text,elapsed,cpu=guest.command(command,timeout=120)
            if code!=expected or (marker and marker not in text):raise AssertionError((command,code,text))
            report['cases'].append(dict(command=command,exit_code=code,expected_exit=expected,status='PASS',wall_seconds=elapsed,output=text.decode('utf-8','replace')))
        vm.hmp('sendkey shift')
        screenshot=vm.out/'desktop.ppm'
        vm.hmp('screendump "'+screenshot.as_posix()+'"')
        report['screenshot']=png_from_ppm(screenshot)
        report['status']='CLEAN_NATIVE_AND_HMP_SMOKE_PASS'
    finally:guest.close()
report['source_unchanged']=vm.report['source_unchanged']
(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
