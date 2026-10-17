"""保存已实际查看的正常操作结果；不启动系统，不重写历史失败。"""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'evidence/m10a2-product-fix'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    old=ROOT/'build/m10a2-p6-gui-product-01'
    new=ROOT/'build/m10a2-p6-gui-product-02'
    reports=[json.loads((p/'product-flow.json').read_bytes()) for p in (old,new)]
    for r in reports:
        assert r['status']=='PRODUCT_INPUT_FINISHED_REVIEW_SEPARATE'
        assert r['qemu_closed'] and r['source_disk_unchanged'] and r['current_inputs_unchanged']
        assert r['historical_resource_matrix_not_run']
    OUT.mkdir(exist_ok=False)
    screens={
        'legacy-output.png':old/'legacy-output-end.png',
        'directory-navigation.png':old/'appfix-listed.png',
        'files-scroll-limit.png':old/'files-up-limit.png',
        'files-studio.png':old/'files-editor-settled.png',
        'studio-start.png':new/'studio-start.png',
        'project-details.png':new/'project-details.png',
        'helloworld-output.png':new/'helloworld-output.png',
        'syntax-error.png':new/'syntax-error-report.png',
    }
    images={}
    for name,p in screens.items():
        shutil.copyfile(p,OUT/name)
        images[name]=dict(sha256=sha(p),bytes=p.stat().st_size,original=str(p.relative_to(ROOT)))
    review=dict(status='REPORTED_STUDIO_FILES_PRODUCT_DEFECTS_VERIFIED',
        host_report='build/m10a2-main-product-fix-04/p6-unified-host-26.json',
        native_report='build/m10a2-p6-target-refresh-15/p6-generation.json',
        core_sha256=sha(ROOT/'build/m10a2-p6-final-15/core/fs/SYS/CORE/CORE.SKM'),
        actual_gui_reports=[dict(path=str((p/'product-flow.json').relative_to(ROOT)),sha256=sha(p/'product-flow.json')) for p in (old,new)],
        checked=dict(existing_c_build_enabled=True,legacy_scapi_program_compiled_and_ran=True,
            folder_double_click_navigation=True,files_scroll_bounded=True,files_edit_opens_studio2=True,
            start_page_actions_visible=True,default_project_type_visible=True,
            default_helloworld_built_and_ran=True,last_output_visible_without_manual_scrolling=True,
            compiler_error_file_line_and_message_visible=True,studio_closed_normally=True),
        reused_gui1='Header compatibility, dialog navigation, scroll and modern editor routing retain their checked behavior; GUI2 rechecked the changed project/output/type controls.',
        observed_output='Hello, world!',observed_compiler_error='SCCC ERROR HOME/HelloWorld/MAIN.C:1: expression needs a value',
        prior_failure_preserved='GUI1 default HelloWorld exposed a mixed-case candidate/manifest mismatch; fixed and verified in GUI2.',
        qemu_closed=True,additional_resource_matrix_run=False,whole_milestone_revalidated=False,
        screenshots=images)
    (OUT/'review.json').write_bytes((json.dumps(review,ensure_ascii=False,indent=2)+'\n').encode())
    (OUT/'README.md').write_bytes('''# Studio 与 Files 产品修复实测

Windows QEMU / WHPX / 1920×1080，使用 HMP 实际输入和 screendump；未使用 computer-use 或 subagent。只检查用户报告的日常操作，不运行历史资源矩阵。

第一轮确认旧 SCAPI.H 程序实际编译并输出 Hello legacy!、打开 C 可直接构建、共享选择器双击进入目录、Files 滚动有界、右键编辑进入 Studio2。第一轮也暴露了默认项目的临时文件名大小写错误，原失败保留。

修复后的第二轮确认：开始页三个入口完整可见；项目默认类型显示 C 控制台程序；保持默认 HelloWorld 名称与模板，F7 实际经过 SCCC/SCLD 成功，Ctrl+F5 输出 Hello, world!，输出末行自动可见。插入一处语法错误后，弹窗及输出都显示 MAIN.C:1 和 expression needs a value；随后正常关闭 Studio 并结束 VM。

第二轮绑定 HOST26/native15/final15。旧头、导航、滚动和编辑路由复用第一轮未改变的行为证据；没有重跑其它应用或已放下的资源事项。review.json 记录实际来源与图片摘要。此记录说明本轮具体缺陷的结果，不改写首发版本的历史结论。
'''.encode())
    print(review['status'])
if __name__=='__main__':main()
