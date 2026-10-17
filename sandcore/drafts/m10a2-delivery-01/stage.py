"""文档统一后仅严格restage SDK；增加实际规范，不改构建证明或运行结论。"""
from pathlib import Path
import sys,json,hashlib
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'drafts/m10a2-p6-final-12-receipt')]
import stage_m10a2_sdk as sdk
from finalize import commands
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    _,argv,_,_,_=commands()
    old=argv.index('--out')+1;argv[old]='build/m10a2-sdk-delivery-02'
    additions=('M10A2-ACCEPTANCE.md','M10A2-RELEASE.md','M10A2-DEFAULT-ENTRY.md')
    sdk.DOCS=tuple(dict.fromkeys(sdk.DOCS+additions))
    sys.argv=argv[2:]
    # argv[2]为脚本名；原main的全部来源与输入守恒检查照常执行。
    sdk.main()
    out=ROOT/'build/m10a2-sdk-delivery-02';m=json.loads((out/'MANIFEST.json').read_bytes())
    assert m['inputs_unchanged'] and not m['SDK_runtime_verified']
    assert len(m['libraries'])==9
    receipt={'status':'STRICT_SDK_RESTAGED_CURRENT_DOCS','output':str(out.relative_to(ROOT)),
        'manifest_sha256':sha(out/'MANIFEST.json'),'files':len(m['files']),
        'payload_bytes':sum(x['bytes'] for x in m['files'].values()),
        'additional_docs':list(additions),'ordinary_native_libraries':8,
        'host_codec_exception':'SCSOUND.SCL','runtime_evidence':'docs/M10A2-ACCEPTANCE.md',
        'staging_runtime_flag_unchanged':False,'stage_source_sha256':sha(Path(__file__)),
        'original_staging_tool_sha256':sha(ROOT/'tools/stage_m10a2_sdk.py')}
    (Path(__file__).parent/'sdk-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(receipt))
if __name__=='__main__':main()
