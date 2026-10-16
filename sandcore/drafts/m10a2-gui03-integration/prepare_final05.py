"""从冻结执行器生成下一实际批次接线，保留原稿和旧layout全部原件。"""
from pathlib import Path
import argparse
import ast
import hashlib
import json
ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--native-sha256',required=True)
p.add_argument('--refresh-sha256',required=True)
args=p.parse_args()
native=ROOT/'build/m10a2-p6-target-refresh-05/p6-generation.json'
refresh=ROOT/'build/m10a2-p6-refresh-vm-05/refresh-vm.json'
assert sha(native)==args.native_sha256 and sha(refresh)==args.refresh_sha256
assert json.loads(native.read_bytes())['status']=='DECLARED_P6_NATIVE_GENERATION_PASS'
assert json.loads(refresh.read_bytes())['status']=='EXPLICIT_LABEL_REFRESH_GENERATED'
out=ROOT/'drafts/m10a2-p6-final-05-receipt'
out.mkdir(exist_ok=False)
old3=ROOT/'drafts/m10a2-p6-final-03-receipt'
old4=ROOT/'drafts/m10a2-p6-final-04-receipt'
binding=(old3/'bindings.py').read_text(encoding='utf-8')
binding=binding[:binding.index('    def layout(self, path, expected):')]
binding=binding.replace('final03','final05').replace('HOST09','HOST11').replace('native04','native05').replace('refresh04','refresh05')
binding=binding.replace('m10a2-p6-final-03','m10a2-p6-final-05').replace('m10a2-p6-target-refresh-04','m10a2-p6-target-refresh-05').replace('m10a2-p6-refresh-vm-04','m10a2-p6-refresh-vm-05')
binding=binding.replace('p6-unified-host-09.json','p6-unified-host-11.json').replace('879ceb99215972f60d1dfc76f0102d1a2255c8616685dd980eb4e8d5aba77ae4','15ae392845d5307dd3617778ee461ba53f2e87bc7533b31d29857db40b2b3785')
binding=binding.replace('    def __init__(self, native_sha, refresh_sha):',
    '''    def __init__(self, native_sha, refresh_sha, runtime_sha=None):
        require(native_sha==NATIVE_SHA and refresh_sha==REFRESH_SHA, 'native05与outer05必须是Root固定的实际PASS')
        require(runtime_sha==HOST_SHA, '必须明确固定实际HOST11')''')
binding=binding.replace("        for path in HERE.glob('*.py'):",
    "        self.runtime_metadata=dict(host_report=pin(HOST), producer_and_runtime_same_host=True)\n        for path in HERE.glob('*.py'):")
binding=binding.replace("LIBS = (",f"GENERATION_HOST=HOST\nGENERATION_HOST_SHA=HOST_SHA\nNATIVE_SHA={args.native_sha256!r}\nREFRESH_SHA={args.refresh_sha256!r}\nLAYOUT_SHA='6b828bbab032c4bb8ceb00ee6a94f7b55cdf0671ec18d2cb807d73240260e80a'\nLIBS = (")
layout=(old4/'bindings.py').read_text(encoding='utf-8').split('    def layout(self, path, expected):',1)[1]
layout='    def layout(self, path, expected):'+layout
layout=layout.replace('final04只复用','final05只复用').replace("proof.get('native_source_binding') == self.metadata", "proof.get('native_source_binding', {}).get('host_report', {}).get('sha256') == '879ceb99215972f60d1dfc76f0102d1a2255c8616685dd980eb4e8d5aba77ae4'")
layout=layout.replace("        require(set(proof['files'])", "        require(proof['native_source_binding']['native_report']['sha256']=='1ac7e951434d5465a665844019a731609a08c35c4a22a5f42bb6c43fac2b5191' and proof['native_source_binding']['refresh_vm_report']['sha256']=='226d9d99ae250c247744430b0247f7c7f30cc315ca1687e2a8e6b604f7317175', 'layout02实际历史native04/outer04来源不同')\n        require(set(proof['files'])")
layout=layout.replace('真实原生/HOST09/HOST10','实际native05/HOST11').replace('ONLY_FS_LEASE_IMPLEMENTATION_DELTA_NO_LAYOUT_HEADER_CHANGE','IMPLEMENTATION_FIXES_WITH_IDENTICAL_FIVE_STRUCTURE_HEADERS')
(out/'bindings.py').write_text(binding+layout,encoding='utf-8')
for name in ('finalize.py','inspect_final.py'):
    text=(old4/name).read_text(encoding='utf-8')
    text=text.replace('final-04','final-05').replace('final04','final05').replace('m10a2-p6-final-04','m10a2-p6-final-05')
    text=text.replace('HOST09','HOST11').replace('HOST10','HOST11').replace('native04','native05').replace('outer04','outer05')
    text=text.replace('all_native_user_sources_equal_host09_host10_and_actual_bytes','all_native_user_sources_equal_generation_runtime_and_actual_bytes')
    text=text.replace('HOST11原始输入/输出','HOST11实际输入/输出')
    (out/name).write_text(text,encoding='utf-8')
for path in out.glob('*.py'):ast.parse(path.read_text(encoding='utf-8'))
receipt=dict(status='CURRENT_FINAL05_WIRING_PREPARED_NOT_EXECUTED',
    original_helpers={str(path.relative_to(ROOT)):sha(path) for path in
        (old3/'bindings.py',old4/'bindings.py',old4/'finalize.py',old4/'inspect_final.py')},
    new_helpers={path.name:sha(path) for path in out.glob('*.py')},
    native_sha256=args.native_sha256,refresh_sha256=args.refresh_sha256,
    historical_layout_kept=True,layout_recompiled=False,core_built=False,sdk_staged=False)
(out/'PREPARATION.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(receipt['status'])
