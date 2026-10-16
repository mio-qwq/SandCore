"""保存已实际查看的代表画面和最终零差账，不把失败流程改称成功。"""
from pathlib import Path
import hashlib
import json
ROOT=Path(__file__).resolve().parents[2]
VM=ROOT/'build/m10a2-p6-gui-run-02'
OUT=ROOT/'evidence/m10a2-p6-gui02'
assert not OUT.exists()
report_path=VM/'p6-gui.json';d=json.loads(report_path.read_text(encoding='utf-8'))
assert d['status']=='P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING'
assert d['current_inputs_unchanged'] and d['source_disk_unchanged']
sha=lambda b:hashlib.sha256(b).hexdigest()
screens={Path(row['path']).name:row for row in d['screenshots']}
names=['p6-ordinary-desktop-apps-closed.png','p6-files-tree-details.png',
       'p6-notes-durable-saved.png','p6-canvas-stroke.png','p6-canvas-undo.png',
       'p6-canvas-save-final-status.png','p6-lens-native-pixels.png',
       'p6-calculator-history.png','p6-notes-focused-paste.png',
       'p6-studio-scw-build-progress.png','p6-desktop-after-move-refresh.png',
       'p6-desktop-manual-refresh.png','p6-palette-selected-accent.png',
       'p6-monitor-frozen.png','p6-monitor-cpu-history-resumed.png',
       'p6-sound-playing-long.png','p6-sound-mini.png']
rows=[]
for name in names:
    p=VM/name;b=p.read_bytes();assert sha(b)==screens[name]['sha256']
    rows.append((name,b,dict(file=name,sha256=sha(b),source=str(p.relative_to(ROOT)),root_visually_inspected=True)))
for p in sorted(VM.glob('*audit.json')):
    rows.append((p.name,p.read_bytes(),dict(file=p.name,sha256=sha(p.read_bytes()),source=str(p.relative_to(ROOT)))))
finish=d['p6_gui_runtime']['actions']['finish-gui2-coherent-owner-account']['result']
ledger=finish['ledger'];assert ledger['physical_delta']==ledger['unexplained_delta']==0 and not ledger['unexpected']
result=dict(status='GUI02_REPRESENTATIVE_EVIDENCE_WITH_KNOWN_DEFECTS',full_M10a2=False,
    report=str(report_path.relative_to(ROOT)),report_sha256=sha(report_path.read_bytes()),
    input_sha256=d['input_sha256'],source_disk_unchanged=True,current_inputs_unchanged=True,
    session_removed=finish['session_removed'],coherent_pair=finish['coherent_pair'],
    original_205_pages_exempted=finish['original_205_pages_exempted'],ledger=ledger,
    files=[r for _,_,r in rows],
    known_defects=['name_enter','notes_association_arguments','studio_sco_source_root',
      'shell_TEXT1','drop_source_view_stale','settings_installer_column_ID',
      'systemtools_notice_lifetime','sound_shortcut_case','sound_replay_unresolved'],
    sound_capture=d['p6_gui_runtime']['actions']['sound-stop-short-pcm']['result'],
    sound_pcm_observed=dict(samples=544816,nonzero=542546,peak=10080,
        scope='QEMU_MIXER_PCM_NOT_HOST_AUDIBILITY'))
OUT.mkdir(parents=True)
for name,body,row in rows:(OUT/name).write_bytes(body)
(OUT/'summary.json').write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode())
(OUT/'README.md').write_bytes('''# P6 普通用户GUI第二轮证据

本轮已正常关闭全部应用和worker、恢复原SID1并注销普通UID/GID1000测试SID4。最终一致物理页13922→13922、所有资源池差额0、未归属差额0；源盘及冻结输入不变。summary.json固定实际来源、原始报告完整SHA和代表截图。

Notes持久保存、Files COPY/CUT/MOVE、Canvas绘画/撤销重做/保存、Lens原生100%、Calculator结果/历史、Palette实际色值、Monitor冻结/历史/继续及Sound首次PCM均有实际键鼠和画面。跨窗文件日志证明真实提交，但原桌面自动来源刷新失败；Studio生成SCO后依赖检查失败；跨应用文本粘贴失败。失败画面与审计明确保留。本目录不表示完整M10a2验收通过，也不验证随后修复的新字节。

Sound录音为真实QEMU混音器48k/S16/双声道、5.675秒；非零样本542546、峰值10080。原WAV保存在本轮build目录，完整SHA见summary，不宣称宿主可听。EOF重播显示准备的完整根因尚未确定，不能据此推断worker死锁。

后续仅按实际修复影响定向复核。文档与接力入口：../../docs/M10A2-LIVE-GUI.md。

修订：2026-10-16，保存GUI02代表结果、明确失败与一致零差资源账。
'''.encode())
print('Saved GUI02 receipt:',len(rows),'files,',sum(len(b) for _,b,_ in rows),'bytes')
