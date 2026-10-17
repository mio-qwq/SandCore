"""保存默认入口故障/修正证据及GUI06已发生事实，不补造退出或回收PASS。"""
from pathlib import Path
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
destination=ROOT/'evidence/m10a2-default-entry-01'
destination.mkdir(exist_ok=False)
paths={
    'user-reported-blurry.png':Path(r'C:\Users\ADMINI~1\AppData\Local\Temp\codex-clipboard-44cf4314-6859-4139-8ec5-030f4dd12abf.png'),
    'default-settings.png':ROOT/'build/m10a2-default-entry-01/settings-default-2.png',
    'default-file-dialog.png':ROOT/'build/m10a2-default-entry-01/settings-open-dialog.png',
    'entry.json':ROOT/'build/m10a2-default-entry-01/entry.json',
    'qemu.stderr.txt':ROOT/'build/m10a2-default-entry-01/qemu.stderr.txt',
    'original-sdl-dpi.json':Path(__file__).with_name('original-sdl-dpi.json'),
    'acceleration-probe.json':Path(__file__).with_name('acceleration-probe.json')}
records={}
for name,source in paths.items():
    shutil.copyfile(source,destination/name)
    records[name]=dict(source=str(source),bytes=source.stat().st_size,sha256=sha(source))
record=dict(scope='DEFAULT_ENTRY_HOST_DPI_AND_COLD_GUEST_VISUAL_REVIEW',files=records,
    original_sdl=dict(awareness=0,window_dpi=96,paused_diskless=True),
    corrected_gtk=dict(pid=10012,hwnd=6489912,awareness=2,window_dpi=144,
        observed_by='READ_ONLY_GetProcessDpiAwareness_GetDpiForWindow',host_applied_dpi=144),
    hardware_accelerator_initialization='WHPX_PROBE_EXIT_0',
    cold_guest_boot='ACTUAL_GTK_WHPX_PREFERRED_LAUNCH_WITH_SNAPSHOT',
    raw_guest_display=dict(width=1920,height=1080,scale_percent=150),
    gui_shortcut_keypress_verification='NOT_CONFIRMED_BY_NATIVE_INPUT_HELPER',
    monitor_configuration='GRAPHICAL_VC_PRESENT',
    diagnostic_shutdown=dict(qemu_exit_code=0,driver_exit_code=1,
        reason='QMP_QUIT_SUCCEEDED_THEN_DRIVER_DUPLICATE_QUIT_CONNECTION_RESET',
        helper_fixed_after_shutdown=True,guest_failure=False),
    user_directory_files={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in Path(r'C:\Users\Administrator\Desktop\test').iterdir()},
    user_disks_rewritten_by_diagnostic=False,other_resolutions='PENDING_TARGETED_GUI_REVIEW',full_M10a2=False)
(destination/'review.json').write_bytes((json.dumps(record,ensure_ascii=False,indent=2)+'\n').encode())

gui=ROOT/'build/m10a2-p6-gui-run-06'
target=ROOT/'evidence/m10a2-p6-gui06-partial'
target.mkdir(exist_ok=False)
names=['p6-gui.json','p6-gui6-settings-property-applied.png','p6-gui6-settings-environment-saved.png',
       'p6-gui6-installer-restored.png','p6-gui6-installer-durable-details.png',
       'p6-gui6-installer-normal-exit-result.png','p6-gui6-files-cancel-real-exit-code.png',
       'gui6-installed-DATA.RDT','gui6-source-DATA.RDT']
items={}
for name in names:
    source=gui/name
    if not source.is_file():
        if name.startswith('gui6-'):
            candidates=list(gui.glob(name+'*'))
            if len(candidates)==1:source=candidates[0]
        if not source.is_file():raise FileNotFoundError(name)
    shutil.copyfile(source,target/source.name)
    items[source.name]=dict(bytes=source.stat().st_size,sha256=sha(source))
(target/'interruption.json').write_bytes((json.dumps(dict(
    status='VM_ABSENT_AND_CONTROL_HANDLE_MISSING_OBSERVED',
    raw_report_status_preserved='RUNNING',qemu_pid=20472,exec_handle=36349,
    owner_end_snapshot=False,normal_session_logout=False,
    files_cancel=dict(actual_target_exit=313,passed=False),
    installer=dict(actual_target_exit=0,durable=True),
    files=items,full_M10a2=False),indent=2)+'\n').encode())
print('Archived default-entry proof and GUI06 partial evidence; no owner PASS')
