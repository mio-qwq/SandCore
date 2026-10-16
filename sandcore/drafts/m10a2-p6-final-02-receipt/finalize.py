"""final-02独立CORE/严格SDK执行器；默认只写计划，明确PASS通知后才使用--execute。"""
from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess,sys,time,traceback
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
OUT=ROOT/'build/m10a2-p6-final-02'
NATIVE=ROOT/'build/m10a2-p6-target-refresh-03'
VM_REPORT=ROOT/'build/m10a2-p6-refresh-vm-03/refresh-vm.json'
HOST=ROOT/'build/m10a2-main-studio-01/p6-unified-host-07.json'
HOST_SHA='22b85409627ff02b46bc47093a4f2c9482e0229efb23b403d469892997a0e39c'
LIBS=('SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL','SCUI.SCL','SCSHELL.SCL','SCGFX.SCL','SCTERM.SCL')
sys.path.insert(0,str(ROOT/'tools'))
import stage_m10a2_sdk as sdk

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
def rel(path):return path.resolve().relative_to(ROOT).as_posix()
def load(path):return json.loads(path.read_text(encoding='utf-8'))
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def require(value,why):
    if not value:raise ValueError(why)
def pin(path):return dict(path=rel(path),bytes=path.stat().st_size,sha256=sha(path))
def artifact_path(row):
    path=Path(row['path']);path=path if path.is_absolute() else NATIVE/path
    path=path.resolve(strict=True);require(path.is_relative_to(ROOT),'artifact离开工作区')
    require(path.stat().st_size==row['bytes'] and sha(path)==row['sha256'],'native artifact精确字节改变')
    return path
def commands(native=None):
    libprov=NATIVE/'native-libraries/provenance.json';appprov=NATIVE/'native-artifacts/provenance.json'
    codec=[sys.executable,'-B','tools/make_m10a2_codec_proof.py','--build-report',rel(HOST),'--out',rel(OUT/'codec-proof.json')]
    stage=[sys.executable,'-B','tools/stage_m10a2_sdk.py','--profile','native-preview','--out',rel(OUT/'sdk'),'--build-report',rel(HOST),
        '--compiler-provenance',rel(appprov),'--linker-provenance',rel(appprov),'--host-codec-proof',rel(OUT/'codec-proof.json')]
    for name in LIBS:stage+=['--native-library',name+'='+rel(libprov)]
    # SDK目录是已严格核对的bundle镜像；若所有原生五库同目录，直接绑定原目录。
    libdir=NATIVE/'native-libraries';desktop=NATIVE/'native-artifacts/SCSHELL.SCX';login=NATIVE/'native-artifacts/LOGIN2.SCX';mode='DIRECT_NATIVE_BUNDLE_DIRECTORY'
    if native:
        desktop=artifact_path(native['native_artifacts']['SCSHELL.SCX']);login=artifact_path(native['native_artifacts']['LOGIN2.SCX'])
        for name in LIBS[:5]:
            source=artifact_path(native['native_libraries'][name]);staged=OUT/'sdk/SYS/LIB'/name
            require(sha(staged)==sha(source),'SDK五库不是所选native bundle')
        candidates={artifact_path(native['native_libraries'][name]).parent for name in LIBS[:5]}
        if len(candidates)==1 and all((next(iter(candidates))/name).is_file() for name in LIBS[:5]):libdir=next(iter(candidates))
        else:libdir=OUT/'sdk/SYS/LIB';mode='SDK_VERIFIED_EXACT_NATIVE_BUNDLE_MIRROR'
    make=['make','-j4','BUILD='+rel(OUT/'core'),'M9_BUILD=1','M10_BUILD=1','SHELL_HOST_IMAGE='+rel(desktop),
        'LOGIN2_ISSUE_IMAGE='+rel(login),'LOGIN2_ISSUE_LIBRARIES='+rel(libdir),'M10_PUBLIC_KEY=assets/trust/M10-OWNER-ED25519.PUB',
        rel(OUT/'core/sandcore.img'),rel(OUT/'core/fs/SYS/CORE/CORE.SKM')]
    cwd=ROOT.as_posix();wsl='/mnt/'+cwd[0].lower()+'/'+cwd[3:]
    command='cd '+shlex.quote(wsl)+' && PYTHONDONTWRITEBYTECODE=1 '+' '.join(shlex.quote(x) for x in make)
    return codec,stage,['wsl.exe','sh','-lc',command],mode,make

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--execute',action='store_true');parser.add_argument('--native-report-sha256');args=parser.parse_args()
    require(sha(HOST)==HOST_SHA,'明确HOST07报告发生变化')
    codec,stage,core,mode,make=commands()
    plan=dict(schema=1,status='PREPARED_WAITING_FOR_ROOT_EXPLICIT_NATIVE_PASS_NO_EXECUTION',native_report=rel(NATIVE/'p6-generation.json'),host_report=pin(HOST),
        out=rel(OUT),steps=[dict(name='codec-proof',args=codec),dict(name='sdk-stage',args=stage),dict(name='core-build',args=core,make_args=make)],
        scopes=dict(main_write=False,vm_start=False,disk_copy=False,core_targets_only=2,ordinary_native_libraries=8,host_codec_exception='SCSOUND.SCL_ONLY'))
    if not args.execute:save(HERE/'PLAN.json',plan);print(plan['status']);return
    require(args.native_report_sha256 is not None,'执行时必须显式固定实际PASS报告SHA')
    npath=NATIVE/'p6-generation.json';native=load(npath)
    require(sha(npath)==args.native_report_sha256,'PASS报告不等于通知后固定的原像')
    require(native['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and native['current_inputs_unchanged'] is True and native['source_disk_unchanged'] is True and native['changed_input_output_paths']==[],'refresh尚未完整守恒PASS')
    checked=sdk.Inputs();proof,recorded,outputs=sdk.verified_build(HOST,checked)
    require(proof['exit_code']==0 and proof['source_files_modified_during_build']==[],'HOST07不满足严格源/输出守恒')
    # preflight.build_report属于继承的历史HOST03，不能改写它伪称同轮生成。
    # 当前准备态由自己的完整source snapshot与真实refresh VM的HOST07固定输入绑定。
    require(native.get('source_snapshot_scope')=='CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN','refresh未分层当前准备快照/原generation')
    snapshot=native['source_input_sha256'];require(snapshot and snapshot==native['input_sha256'],'当前完整源码snapshot不一致')
    for name,wanted in snapshot.items():
        require(recorded.get(name)==wanted,'native当前源与HOST07实际输入不等：'+name)
        checked.read(sdk.relative(name),wanted)
    vm=load(VM_REPORT)
    require(vm['status']=='THREE_TARGET_REFRESH_GENERATED' and vm['bundle_status']=='DECLARED_P6_NATIVE_GENERATION_PASS','refresh VM尚未完成明确三目标生成')
    require(Path(vm['bundle_report']).resolve()==npath.resolve(),'refresh VM不是所选完整bundle')
    require(Path(vm['host_report']).resolve()==HOST.resolve(),'refresh VM没有绑定明确HOST07')
    host_pins=[value for name,value in vm['current_input_sha256'].items() if Path(name).resolve()==HOST.resolve()]
    require(host_pins==[HOST_SHA],'refresh VM实际HOST07输入SHA不同')
    require(vm['current_inputs_unchanged'] is True and vm['source_disk_unchanged'] is True and vm['changed_input_output_paths']==[],'refresh VM最终源码/原盘不守恒')
    require(not OUT.exists(),'final-02必须是全新目录，失败不覆盖')
    OUT.mkdir(parents=True,exist_ok=False)
    report=dict(schema=1,status='RUNNING',scope='ACTUAL_NATIVE_BOUND_CORE_BUILD_AND_STRICT_SDK_STAGING_NOT_GUEST_BOOT',native_report=pin(npath),host_report=pin(HOST),steps=[],
        historical_native_preflight=dict(build_report=native['preflight']['build_report'],build_report_sha256=native['preflight']['build_report_sha256']),
        current_refresh_binding=dict(vm_report=pin(VM_REPORT),source_snapshot_scope=native['source_snapshot_scope'],exact_host07_source_snapshot_count=len(snapshot),all_current_sources_equal_host07_and_actual_bytes=True,vm_current_inputs_unchanged=True,vm_source_disk_unchanged=True),
        main_modified=False,vm_started=False,disk_copied=False,guest_boot=False,guest_runtime_verified=False,full_M10a2=False)
    report_path=OUT/'finalization.json'
    def persist():save(report_path,report)
    def run(name,argv):
        log=OUT/(name+'-01.log');require(not log.exists(),'拒绝覆盖步骤日志')
        row=dict(name=name,args=argv,log=rel(log),status='RUNNING');report['steps'].append(row);persist();print('START '+name,flush=True);start=time.monotonic()
        env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
        with log.open('xb') as stream:
            result=subprocess.run(argv,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT)
        row.update(exit_code=result.returncode,wall_seconds=time.monotonic()-start,status='PASS' if result.returncode==0 else 'FAIL',log_sha256=sha(log));persist();print('END '+name+' exit='+str(result.returncode),flush=True)
        require(result.returncode==0,'实际步骤失败：'+name)
    try:
        run('codec-proof',codec);run('sdk-stage',stage)
        manifest=load(OUT/'sdk/MANIFEST.json');require(manifest['profile']=='native-preview' and manifest['inputs_unchanged'] is True,'SDK未严格完成')
        expected=set(LIBS)|{'SCSOUND.SCL'};require(set(manifest['libraries'])==expected,'SDK不是八普通原生+唯一codec')
        for name,value in manifest['libraries'].items():
            source=value['origin']['source']
            require(source==('USER_APPROVED_HOST_CODEC_EXCEPTION_NOT_GUEST_NATIVE' if name=='SCSOUND.SCL' else 'ACTUAL_SCCC_NATIVE'),'SDK来源被混淆：'+name)
        require(manifest['compiler']['source']=='ACTUAL_GUEST_SCCC_G1_CLI' and manifest['linker']['source']=='ACTUAL_GUEST_SCCC_SCLD_CLI','SDK工具不是真实native来源')
        _,_,core,mode,make=commands(native)
        watched={name:sha(ROOT/name) for name in {v['path'] for v in proof['inputs']}|{'core.ld','assets/trust/M10-OWNER-ED25519.PUB','kernel/font16.txt','assets/font/phoenix-ascii.json'}}
        before_path=OUT/'core-source-before.json';save(before_path,watched)
        report.update(core_make_args=make,issuer_library_source_mode=mode,core_source_before=pin(before_path),sdk_manifest=pin(OUT/'sdk/MANIFEST.json'),codec_proof=pin(OUT/'codec-proof.json'));persist()
        run('core-build',core)
        changed=[name for name,wanted in watched.items() if not (ROOT/name).is_file() or sha(ROOT/name)!=wanted]
        report['core_source_files_changed_during_build']=changed;report['core_watched_source_count']=len(watched);persist();require(not changed,'CORE构建期间明确源集合改变')
        require(not checked.unchanged(),'HOST07原始输入/输出在执行期间改变')
        report.update(status='BUILT_AND_STAGED_ARTIFACT_INSPECTION_PENDING',sdk_runtime_verified=False);persist()
        print('CORE_AND_SDK_READY_FOR_READ_ONLY_RECEIPT',flush=True)
    except BaseException as e:
        report.update(status='FAIL_OR_INTERRUPTED_NOT_DELIVERABLE',error=dict(message=str(e),traceback=traceback.format_exc()));persist();raise
if __name__=='__main__':main()
