"""仅用既有严格发行工具同步本轮已核验产物和公开文档。"""
from pathlib import Path
import hashlib,json,sys
ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'drafts/m10a2-p6-final-15-receipt')]
import stage_m10a2_sdk as sdk
from finalize import commands
def main():
    _,argv,_,_,_=commands()
    argv[argv.index('--out')+1]='build/m10a2-sdk-delivery-03'
    sdk.DOCS=tuple(dict.fromkeys(sdk.DOCS+('M10A2-APPLICATION-PRODUCT-FIX.md','M10A2-ACCEPTANCE.md','M10A2-RELEASE.md','M10A2-DEFAULT-ENTRY.md')))
    sys.argv=argv[2:]
    sdk.main()
    out=ROOT/'build/m10a2-sdk-delivery-03'
    m=json.loads((out/'MANIFEST.json').read_bytes())
    assert m['inputs_unchanged'] and m['SDK_runtime_verified'] is False
    for name in ('SCAPI.H','SCIO.H','SCMEM.H'):
        assert (out/'SYS/INC'/name).read_bytes()==(ROOT/'user'/name).read_bytes()
    print('CURRENT_PRODUCT_SDK_STAGED',len(m['files']))
if __name__=='__main__':main()
