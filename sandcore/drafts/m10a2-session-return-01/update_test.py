"""验完后只迁移试玩盘内的系统载荷；独占写共享保护、原盘备份及逐对象守恒。"""
from pathlib import Path
from contextlib import contextmanager
import ctypes,ctypes.wintypes as wt,hashlib,json,os,shutil,sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'drafts/m10a2-p6-final-flow')]
from prepare_candidate import Candidate
from verify_m10a2_scl import prepare
from mkfs_m9 import fold
TEST=Path('C:/Users/Administrator/Desktop/test').resolve()
OUT=ROOT/'build/m10a2-test-session-update-01'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
@contextmanager
def deny_writers(path):
    # 已打开的QEMU写句柄会使此调用失败；持有期间新QEMU也不能写旧盘。
    # Windows的映射/替换可能仍拒绝活动读句柄；只在此锁内备份和准备，
    # 不在锁内替换目标。准备盘放build，最后释放句柄、复核原摘要再提交。
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    create=kernel.CreateFileW;create.argtypes=[wt.LPCWSTR,wt.DWORD,wt.DWORD,wt.LPVOID,wt.DWORD,wt.DWORD,wt.HANDLE];create.restype=wt.HANDLE
    close=kernel.CloseHandle;close.argtypes=[wt.HANDLE];close.restype=wt.BOOL
    handle=create(str(path),0x80000000,5,None,3,0x80,None)
    if handle==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
    try:yield
    finally:close(handle)
def main():
    gui=json.loads((ROOT/'build/m10a2-p6-gui-run-13/p6-gui.json').read_bytes())
    review=json.loads((ROOT/'evidence/m10a2-session-return/review.json').read_bytes())
    assert review['status']=='ACTUAL_EXIT_LOGOUT_LOCAL_SIGNIN_VERIFIED_RESOURCE_BOUNDARY_DECLARED' and all(review['gui'].values())
    assert review['qemu_closed'] and gui['current_inputs_unchanged'] and gui['source_disk_unchanged']
    owner=next(c for c in gui['cases'] if c['name']=='p6-gui-single-final-owner-account')
    assert owner['status']==review['resource']['raw_owner_verdict']=='FAIL' and owner['physical_delta']==review['resource']['compiled_state_pages']==8
    assert set(p.name for p in TEST.iterdir())=={'run.py','run.bat','sandcore.img','sanddata.img'}
    core=ROOT/'build/m10a2-p6-final-12/core'
    assert sha(core/'fs/SYS/CORE/CORE.SKM')=='44d0b635f4f2597ef8009067de0da299bb36270c367589a8898711a64361f6b2'
    # loader没有改动；不同引导盘不在本次单盘更新的授权/原子边界内。
    assert sha(core/'sandcore.img')==sha(TEST/'sandcore.img')
    candidate=Candidate(ROOT/'build/m10a2-p6-target-refresh-11/p6-generation.json',
        ROOT/'build/m10a2-main-session-return-01/p6-unified-host-21.json',ROOT/'build/m10a2-p6-final-12/sdk',core,ROOT/'build/m10a1/sanddata.img')
    OUT.mkdir(exist_ok=False)
    data=TEST/'sanddata.img';record=dict(status='STARTED_NOT_DELIVERED',target=str(data),user_file_preservation_required=True,
        gui_report_sha256=sha(ROOT/'build/m10a2-p6-gui-run-13/p6-gui.json'),core_sha256=sha(core/'fs/SYS/CORE/CORE.SKM'),
        behavior_review_sha256=sha(ROOT/'evidence/m10a2-session-return/review.json'),raw_gui_owner_verdict_preserved='FAIL',
        first_locked_view_retention_boundary_declared=True,delivery_scope='verified functional fixes, not whole milestone acceptance')
    save(OUT/'update.json',record)
    with deny_writers(data):
        before={p.name:sha(p) for p in TEST.iterdir()}
        backup=OUT/'sanddata-before.img';shutil.copyfile(data,backup)
        assert sha(backup)==before['sanddata.img']
        record.update(before=before,backup=str(backup),backup_sha256=sha(backup));save(OUT/'update.json',record)
        working=OUT/'sanddata.img';shutil.copyfile(backup,working)
        extra,metadata,plan=candidate.plan(working)
        record['metadata_plan']=plan;save(OUT/'update.json',record)
        result=prepare(working,core,extra,runtime_fixtures=False,metadata=metadata)
        # prepare独立核对全部原对象：不在明确系统清单内的正文、UID/GID、
        # 权限和generation全部保持；不是拿新原始基盘覆盖用户数据。
        assert sha(data)==before['sanddata.img']
        assert sha(backup)==before['sanddata.img']
    # 这两个目录在同一C盘；先关闭全部准备句柄，再一次性发布完整盘。
    # 调用者须保持QEMU关闭；如原盘变动则保留准备结果并拒绝覆盖。
    assert sha(data)==before['sanddata.img']
    os.replace(working,data)
    after={p.name:sha(p) for p in TEST.iterdir()}
    assert set(after)==set(before) and all(after[n]==before[n] for n in ('run.py','run.bat','sandcore.img'))
    assert sha(backup)==before['sanddata.img']
    record.update(status='SYSTEM_PAYLOAD_UPDATED_USER_OBJECTS_PRESERVED',after=after,prepare_result=result,
        all_unlisted_objects_byte_metadata_generation_preserved=True,launcher_bat_boot_unchanged=True,
        target_four_files_only=True,existing_qemu_writers_excluded=True,full_M10a2=False)
    save(OUT/'update.json',record)
    print(json.dumps(dict(status=record['status'],data_sha256=after['sanddata.img'],backup=str(backup)),ensure_ascii=True))
if __name__=='__main__':main()
