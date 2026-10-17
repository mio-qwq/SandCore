"""保存本批真实事实，不把07失败改名，不归档磁盘或重复裸图。"""
from pathlib import Path
import gzip,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'evidence/m10a2-native08';out.mkdir(exist_ok=False)
sha=lambda b:hashlib.sha256(b).hexdigest()
paths=('build/m10a2-p6-target-refresh-08/p6-generation.json',
 'build/m10a2-p6-refresh-vm-08/refresh-vm.json',
 'build/m10a2-p6-artifact-qualification-07/qualified-artifacts.json',
 'build/m10a2-main-studio-01/p6-unified-host-14.json',
 'build/m10a2-p6-sco-16/report.json')
files={}
for name in paths:
 p=ROOT/name;b=p.read_bytes();leaf=p.parent.name+'-'+p.name+'.gz'
 (out/leaf).write_bytes(gzip.compress(b,mtime=0));files[name]=dict(archive=leaf,bytes=len(b),sha256=sha(b))
native=json.loads((ROOT/paths[0]).read_bytes());outer=json.loads((ROOT/paths[1]).read_bytes())
assert native['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and outer['status']=='EXPLICIT_LABEL_REFRESH_GENERATED'
assert native['current_inputs_unchanged'] and native['source_disk_unchanged'] and not native['changed_input_output_paths']
ledger=next(c for c in native['cases'] if c['name']=='target-refresh-single-owner-account')
assert ledger['status']=='PASS' and ledger['unexplained_delta']==0 and not ledger['unexpected']
for name in ('layout.bin','layout.o'):
 p=ROOT/'build/m10a2-parent-heap-layout-01'/name;shutil.copy2(p,out/name)
files.update({name:dict(bytes=(out/name).stat().st_size,sha256=sha((out/name).read_bytes())) for name in ('layout.bin','layout.o')})
review=dict(status='ACTUAL_NATIVE08_GENERATION_AND_OWNER_PASS_NOT_NEW_GUI_ACCEPTANCE',
 fresh_labels=['SCUI'],reused_qualified_native07_labels=['FILES2','DEBUG2','SETTINGS2','INSTALLER2'],
 original_native07_status='FAIL_OR_INTERRUPTED',original_native07_reclassified=False,
 object_count=len(native['objects']),ordinary_libraries=len(native['native_libraries']),artifacts=len(native['native_artifacts']),
 strict_sdk_reader_unchanged=native['strict_reader_audit']['checks_not_weakened'],files=files,
 owner={k:ledger[k] for k in ('before_physical_pages','after_physical_pages','physical_delta','attributed_physical_delta','unexplained_delta','actual_user_clock_glyph_cache_pages','unexpected')},
 shortcut_confirmation=dict(source='USER_ACTUAL_USE',statement='我试了现在可以快捷键',date='2026-10-17',agent_keypress_pass_claimed=False),
 user_playable_disk_modified=False,full_M10a2=False)
(out/'review.json').write_bytes((json.dumps(review,ensure_ascii=False,indent=2)+'\n').encode())
print(review['status'])
