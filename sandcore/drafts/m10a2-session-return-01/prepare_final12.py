"""只在native11实际成功后绑定final12；不改旧凭证，不自动执行构建。"""
from pathlib import Path
import ast,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    native=ROOT/'build/m10a2-p6-target-refresh-11/p6-generation.json'
    outer=ROOT/'build/m10a2-p6-refresh-vm-11/refresh-vm.json'
    host=ROOT/'build/m10a2-main-session-return-01/p6-unified-host-21.json'
    n,v,h=[json.loads(p.read_bytes()) for p in (native,outer,host)]
    assert n['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and n['current_inputs_unchanged'] and n['source_disk_unchanged']
    assert v['status']=='EXPLICIT_LABEL_REFRESH_GENERATED' and v['current_inputs_unchanged'] and v['source_disk_unchanged']
    assert n['qemu_pid']==v['qemu_pid'] and v['host_report_sha256']==sha(host)
    assert h['exit_code']==0 and not h['source_files_modified_during_build']
    src=ROOT/'drafts/m10a2-p6-final-11-receipt';out=ROOT/'drafts/m10a2-p6-final-12-receipt';out.mkdir(exist_ok=False)
    changes={
      'build/m10a2-p6-target-refresh-10':'build/m10a2-p6-target-refresh-11',
      'build/m10a2-p6-refresh-vm-10':'build/m10a2-p6-refresh-vm-11',
      'build/m10a2-main-release-drag-01/p6-unified-host-20.json':host.relative_to(ROOT).as_posix(),
      '4f0980b97bb3e8b95b99779a4273269108917df17936b864e5a541c913c658b7':sha(host),
      '1e39068eeec9de3027f1c04e666b0f24b60ec4795d5ef860a7f376994ffad740':sha(native),
      'd37f10f5904e1ff3e5d9cd472047f20ca7ce7679b82abcf2eabddc78c3ed3b0c':sha(outer)}
    for name in ('bindings.py','finalize.py','inspect_final.py'):
        body=(src/name).read_text(encoding='utf-8')
        # 增量make的输入是实际final11；新构建仅写final12。
        body=body.replace('m10a2-p6-final-10','ACTUAL_PREVIOUS_CORE_PLACEHOLDER')
        body=body.replace('m10a2-p6-final-11','m10a2-p6-final-12')
        body=body.replace('ACTUAL_PREVIOUS_CORE_PLACEHOLDER','m10a2-p6-final-11')
        for old,new in changes.items():body=body.replace(old,new)
        body=body.replace('HOST20','HOST21').replace('native10','native11').replace('outer10','outer11').replace('refresh10','refresh11').replace('final11','final12')
        ast.parse(body);(out/name).write_text(body,encoding='utf-8')
    (out/'PREPARATION.json').write_text(json.dumps(dict(status='ACTUAL_NATIVE11_BOUND_NOT_EXECUTED',native_sha256=sha(native),
        outer_sha256=sha(outer),host_sha256=sha(host),source_helpers={p.name:sha(p) for p in src.glob('*.py')},
        new_helpers={p.name:sha(p) for p in out.glob('*.py')},layout02_reused_five_headers_unchanged=True),indent=2)+'\n',encoding='utf-8')
    print('FINAL12_PREPARED_NOT_EXECUTED')
if __name__=='__main__':main()
