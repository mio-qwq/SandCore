"""单独刷新真实变化的DESKTOP，旧原生库和编译运行底座继续严格复用。"""
from pathlib import Path
import ast,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    src=ROOT/'drafts/m10a2-p6-followup-refresh-v9'
    out=ROOT/'drafts/m10a2-p6-followup-refresh-v10';out.mkdir(exist_ok=False)
    prev='build/m10a2-p6-target-refresh-10/p6-generation.json'
    witness='build/m10a2-p6-refresh-vm-10/refresh-vm.json'
    host='build/m10a2-main-release-drag-01/p6-unified-host-20.json'
    assert sha(ROOT/prev)=='1e39068eeec9de3027f1c04e666b0f24b60ec4795d5ef860a7f376994ffad740'
    assert sha(ROOT/witness)=='d37f10f5904e1ff3e5d9cd472047f20ca7ce7679b82abcf2eabddc78c3ed3b0c'
    assert sha(ROOT/host)=='4f0980b97bb3e8b95b99779a4273269108917df17936b864e5a541c913c658b7'
    body=(src/'followup.py').read_text(encoding='utf-8')
    body=body.replace("path=ROOT/'drafts/m10a2-p6-followup-refresh-v8/followup.py'", "path=ROOT/'drafts/m10a2-p6-followup-refresh-v9/followup.py'")
    body=body.replace('v4.v4.v6.v4.v4.prepared_header_map','v4.v4.v4.v6.v4.v4.prepared_header_map')
    body=body.replace('sha(ROOT/v4.v4.PREVIOUS)==v4.v4.PREVIOUS_SHA','sha(ROOT/v4.v4.v4.PREVIOUS)==v4.v4.v4.PREVIOUS_SHA')
    replacements={
      'build/m10a2-p6-target-refresh-09/p6-generation.json':prev,
      '57fa08a3e2ce36e350a0f88f6d56ab93a2418789ec22723ce2c762d10813f32b':sha(ROOT/prev),
      'build/m10a2-p6-refresh-vm-09/refresh-vm.json':witness,
      'build/m10a2-main-appfix-01/p6-unified-host-16.json':host}
    for old,new in replacements.items():
        assert body.count(old)==1
        body=body.replace(old,new)
    marker='        return super().source(name,expected)'
    assert body.count(marker)==1
    body=body.replace(marker,"""        # 编译VM仍是旧final04；认证正文仅从其真实存档读取，绝不拿
        # 新auth.c冒充旧CORE的编译源。布局和观察逻辑不变。
        archived=ROOT/'drafts/m10a2-session-return-01/before'/name
        if archived.is_file() and sha(archived)==expected:
            return self.pin(archived,expected,archived.stat().st_size)
"""+marker)
    body=body.replace('"""native09作产物继承，final04仍作明确冻结的编译运行底座。"""','"""native10作产物继承，final04仍作明确冻结的编译运行底座。"""')
    (out/'followup.py').write_text(body,encoding='utf-8')
    body=(src/'driver.py').read_text(encoding='utf-8')
    body=body.replace('HOST20','HOST21').replace('p6-unified-host-20.json','p6-unified-host-21.json')
    (out/'driver.py').write_text(body,encoding='utf-8')
    pins=json.loads((src/'PINS.json').read_bytes())
    for p in src.iterdir():
        if p.suffix in ('.py','.json'):
            pins['files'][p.relative_to(ROOT).as_posix()]=dict(sha256=sha(p),bytes=p.stat().st_size)
    for name in (prev,witness,host):pins['files'][name]=dict(sha256=sha(ROOT/name),bytes=(ROOT/name).stat().st_size)
    (out/'PINS.json').write_text(json.dumps(pins,indent=2)+'\n',encoding='utf-8')
    for p in out.glob('*.py'):ast.parse(p.read_text(encoding='utf-8'))
    (HERE/'REFRESH_PREPARATION.json').write_text(json.dumps(dict(status='PREPARED_NOT_EXECUTED',previous=prev,
        historical_reports_unchanged=True,expected_changed_labels=['DESKTOP'],current_kernel_change='auth.c local fixed LOGIN2 policy only',
        runtime_observer_layout_unchanged=True,files={p.name:sha(p) for p in out.iterdir()}),indent=2)+'\n',encoding='utf-8')
    print('DESKTOP_REFRESH_PREPARED_NOT_EXECUTED')
if __name__=='__main__':main()
