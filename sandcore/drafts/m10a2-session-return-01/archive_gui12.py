"""归档已结束的GUI12；新登录修复不倒填进这轮证据。"""
from pathlib import Path
import gzip,hashlib,json,shutil,sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'build/m10a2-p6-gui-run-12'
OUT=ROOT/'evidence/m10a2-gui12'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
    r=json.loads((SOURCE/'p6-gui.json').read_bytes())
    assert r['status']=='P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING'
    assert r['current_inputs_unchanged'] and r['source_disk_unchanged'] and not r['changed_input_output_paths']
    owner=next(c for c in r['cases'] if c['name']=='p6-gui-single-final-owner-account')
    assert owner['status']=='PASS' and owner['physical_delta']==owner['unexplained_delta']==0 and not owner['unexpected']
    result=(SOURCE/'gui12-up-result.log').read_text(encoding='utf-8')
    assert result.count('pass=1')==9 and 'checks_failed=0' in result and 'uid=1000 session=4' in result
    assert sha(SOURCE/'gui12-source-hello.c')==sha(SOURCE/'gui12-target-hello.c')=='535eca2bfc045f2002d8dfa844b7696c49cb90c9b0896b54234ab38cda4473cd'
    assert (SOURCE/'gui12-installer.log').stat().st_size==0
    ps=next(c for c in r['cases'] if c['name']=='p6-cli-gui12-post-close-ps')
    assert ps['status']=='PASS' and not any(s in ps['output'] for s in ('TERMINAL2','INSTALLER2','UP.SCX','FILES2'))
    OUT.mkdir(exist_ok=False)
    files={}
    def copy(p,name,compress=False):
        q=OUT/name;q.parent.mkdir(parents=True,exist_ok=True)
        if compress:q.write_bytes(gzip.compress(p.read_bytes(),mtime=0))
        else:shutil.copyfile(p,q)
        files[name]=dict(source=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p),archive_sha256=sha(q))
    copy(SOURCE/'p6-gui.json','p6-gui.json.gz',True)
    for name in ('session.json','heartbeat.jsonl','gui12-up-compile.log','gui12-up-link.log','gui12-up-result.log','gui12-installer.log',
                 'gui12-copy.REC','gui12-source-hello.c','gui12-target-hello.c','gui12-up-native.scx'):
        copy(SOURCE/name,name)
    for label in ('files-home','fast-drop','cancel-result','copy-dialog','copy-settled','inactive-tooltip','after-files',
                  'probe-current','probe-return','installer-ready','installer-exit-shell-result'):
        copy(SOURCE/('gui12-'+label+'.png'),'screenshots/'+label+'.png')
    for c in r['cases']:
        if c.get('stdout_path') and any(s in c['name'] for s in ('post-close-ps','restore-original','remove-finished')):
            copy(Path(c['stdout_path']),'stdout/'+c['name']+'.bin')
    for name in ('DROPUP.C','README.md'):copy(ROOT/'drafts/m10a2-drop-release-probe'/name,'probe/'+name)
    copy(ROOT/'build/m10a2-drop-release-probe-03/UP.JSON','probe/UP.JSON')
    sys.path.insert(0,str(ROOT/'drafts/m10a2-p6-gui-runtime'))
    from evidence import journal
    rec=journal((SOURCE/'gui12-copy.REC').read_bytes(),session=4)
    save(OUT/'copy-journal.json',rec)
    review=dict(status='SCOPED_GUI12_NATIVE_FAST_RELEASE_AND_INSTALLER_REVIEW_PASS',full_M10a2=False,
        driver_exit_code=0,qemu_pid=r['qemu_pid'],qemu_closed=True,host_report=r['host_report'],host_report_sha256=r['host_report_sha256'],
        native_report=r['native_report'],native_report_sha256=r['native_report_sha256'],core_binding=r['core_binding'],
        actual_gui=dict(fast_drop_extra_hold_ms=0,cancel_source_retained=True,copy_source_target_bytes=168,
            copy_equal_sha256=sha(SOURCE/'gui12-source-hello.c'),copy_durable=True,desktop_auto_refresh_observed=True,
            inactive_tooltip_did_not_show_error_or_stop_files=True,files_normal_ctrl_w=True,
            installer_result_column_visible_at_150_percent=True,installer_normal_exit=0,installer_log_bytes=0,
            probe_guest_compile_and_link=True,probe_ordinary_uid=1000,probe_session=4,probe_normal_exit=0,probe_checks=9,probe_failed=0),
        probe_result=result,probe_native=dict(bytes=(SOURCE/'gui12-up-native.scx').stat().st_size,sha256=sha(SOURCE/'gui12-up-native.scx')),
        owner={k:owner[k] for k in ('before_physical_pages','after_physical_pages','physical_delta','unexplained_delta','unexpected','actual_pool_deltas','actual_font_page_delta','actual_desktop_page_delta')},
        source_disk_unchanged=True,current_inputs_unchanged=True,
        limitations=['Original early command typed during foreground linker lost characters; retained, not counted as successful execution',
          'Early U.LOG snapshot before foreground exit was absent; retained as input uncertainty',
          'One hand-copied screenshot digest rejected before mouse input; retried with actual file digest',
          'Initial Files status -301 before HOME navigation is not reclassified as success',
          'Installer actual data write from unchanged GUI10 is reused; this run checks changed initial column sizing',
          'Newly reported logout/passwordless local login faults were discovered after this run; not covered here'],files=files)
    save(OUT/'review.json',review)
    (OUT/'README.md').write_text('''# GUI12 实际定向结果

Windows WHPX / 双盘 / 1920×1080、150%，普通UID/GID1000、SID4。HOST20/native10/final11。

真实PS/2快拖不加350ms停留，出现四按钮确认；默认取消保源。再次快拖选择Copy，168B源目标摘要相同，恢复记录CRC及DURABLE结果完整，桌面自动出现目标图标。失焦tooltip没有弹出拒权错误或终止Files，随后CtrlW正常关闭。

独立DROPUP仅安装源码/清单，在普通Terminal内实际SCCC生成SCO、SCLD生成SCX并运行。真实松开手势检查capture、无UP、错误序号/落点/保留字段、正确UP、重复消费、取消ticket、release capture，共9项通过、输出保持符合预期，失败0、exit0。宿主SCX没有安装到客体。

Installer改变后的默认表格已显示结果列，CtrlW正常exit0，日志0B。原真实安装数据/持久写证据复用GUI10。应用和Shell正常结束，原SID1恢复、SID4注销；唯一同暂停owner终账13950→13950，各池/字体/桌面增量0、未解释0。driver实际exit0，QEMU4644已关闭；源盘和运行输入守恒。

原始输入失误、提前读取未落盘日志及初始Files -301均保留，详见review.json，不将它们抹掉或泛化成全产品无错误。新报告的退出/空密码登录问题单独修复，不倒填此证据。完整M10a2仍待最终合同审阅与交付。
''',encoding='utf-8')
    print(json.dumps(dict(status=review['status'],files=len(files),review_sha256=sha(OUT/'review.json'))))
if __name__=='__main__':main()
