"""仅做本轮产品正常流程；复用现有HMP输入，不运行历史资源矩阵。"""
from pathlib import Path
import argparse,contextlib,json,sys,traceback

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
sys.dont_write_bytecode=True
for p in (ROOT/'tools',ROOT/'drafts/m10a2-p6-final-flow',ROOT/'drafts/m10a2-p6-gui-flow',ROOT/'drafts/m10a2-p6-gui-runtime',ROOT/'drafts/m10a2-p6-gui-followup'):
    sys.path.insert(0,str(p))
from prepare_candidate import Candidate
from scserial import QemuSession,sha
from m10_guest_boot import unique_symbols,wait_first_desktop
from verify_m10a2_ui_gui import sessions
from heartbeat_guest import FollowupGuest
from scui_input import ScuiInput
from flow_input import FlowInput
from runtime import GuiRuntime,serve
import verify_m10a2_ui_platform as first
import refresh_m10a2_ui_native as refresh

class ProductRuntime(GuiRuntime):
    def request(self,request):
        op=request.get('op') if isinstance(request,dict) else None
        if op=='finish':
            self.finished=True
            self.h.report['status']='PRODUCT_INPUT_FINISHED_REVIEW_SEPARATE'
            self._persist()
            return dict(status='FINISHED_NOT_WHOLE_MILESTONE_PASS',resource_matrix_not_run=True)
        if op=='wheel':
            self._ordinary()
            count=request.get('count',1)
            direction=request.get('direction','wheel-down')
            if type(count)!=int or not 1<=count<=24 or direction not in ('wheel-down','wheel-up'):raise ValueError('需要明确的有界真实滚轮输入')
            for i in range(count):self.flow.inputs.wheel(direction)
            self._persist()
            return dict(status='ACTUAL_PS2_WHEEL_EFFECT_REQUIRES_SCREEN',count=count,direction=direction)
        return super().request(request)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('native-report','host-report','sdk-directory','core-build','source-image','out'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--accel',choices=('whpx','tcg'),default='whpx')
    args=p.parse_args()
    candidate=Candidate(args.native_report,args.host_report,args.sdk_directory,args.core_build,args.source_image)
    # 仅一份可见旧源码，作为“打开已有C”的正常输入；原盘不写。
    candidate.add('HOME/APPFIX/LEGACY.C',b'#include "SCAPI.H"\nint main(void) { sc_puts("Hello legacy!\\n"); return 0; }\n','LEGACY_SOURCE_COMPATIBILITY_NORMAL_INPUT')
    out=args.out.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('需要build内新目录')
    report=dict(status='RUNNING',scope='STUDIO_FILES_NORMAL_PRODUCT_FLOWS',cases=[],screenshots=[],physical_input=[],warm_observations=[],whole_milestone_pass=False,
        historical_resource_matrix_not_run=True,native_report=str(candidate.native_path),host_report=str(candidate.host_path))
    def persist():(out/'product-flow.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    first.BUILD=candidate.core
    with first.harness_configuration():
        with QemuSession(candidate.core/'sandcore.img',candidate.source,out,args.accel,True,audio=False,memory=256,data_prepare=candidate.install) as vm:
            report['qemu_pid']=vm.process.pid
            guest=FollowupGuest(vm)
            try:
                symbols=unique_symbols(candidate.core/'core.sym')
                harness=refresh.RefreshHarness(vm,guest,report,symbols,persist,first.local_elf_symbols(candidate.core/'core.elf'))
                wait_first_desktop(guest,symbols,report)
                guest.connect()
                harness.warm_desktop()
                flow=FlowInput(ScuiInput(vm,guest,symbols,report['physical_input'].append),harness)
                runtime=ProductRuntime(flow,harness,session_parser=sessions)
                rows=sessions(harness.run('product-current-session','sessionctl list')['output'].encode())
                current=[r for r in rows if r['active'] and r['uid']==1000 and r['type']=='user']
                if len(current)!=1:raise ValueError('没有唯一普通用户桌面')
                bound=runtime.request(dict(id='bind-current-user',op='bind',session=current[0]['id']))
                print(json.dumps(dict(status='PRODUCT_FLOW_READY',session=current[0]['id'],binding=bound),ensure_ascii=True),flush=True)
                serve(runtime)
                if not runtime.finished:raise EOFError('产品流程未明确结束')
            except BaseException as error:
                report.update(status='FAILED_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error)))
                persist()
                raise
            finally:
                guest.close()
                report['source_disk_unchanged']=sha(candidate.source)==candidate.source_sha
                report['current_inputs_unchanged']=not candidate.input_pins.unchanged()
                persist()
    report['qemu_closed']=True
    persist()

if __name__=='__main__':main()
