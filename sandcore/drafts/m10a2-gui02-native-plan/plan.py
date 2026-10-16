"""只读生成GUI02修复后的实际SCO命令与提升表；不编译、复制或启动VM。"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shlex
import sys

sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
LABELS=('SCUI','SCSHELL','FILES2','DESKTOP','NOTES2','STUDIO2','SETTINGS2',
        'INSTALLER2','CALC2','PALETTE2','MONITOR2','SOUND2')
FIXED={
    'tools/check_m10a2_p6_objects.py':'6273f2cecc7e936a42fb04779cb29107133fa89a5cf46d9f7a57916acd85e4cd',
    'drafts/m10a2-p6-guest-generation/model.py':'3087b2c093dd99350e4049214478bf188a23edb585a4766f529dbc16e50f5bb2',
    'build/m10a2-p6-sco-08/report.json':'c35cb261372cb20067769d60833d36285af345622aee1e7da8e9b4fa807a2513',
    'build/m10a2-p6-adapter-02/build.json':'254c7271453a02d4554b2e9830cc3b152808937c12d632dd248c62ceec8de9bd',
    'build/m10a2-p6-adapter-02/compiler.c':'edb7a66b15ace9223740046e0da1ff0e2524006fcf9784b0efbfa095766bb99f',
    'build/m10a2-p6-adapter-02/sccc-host':'a6dfe92930876400b349ddc33924ac2c5bae76a5dcb8995c1bbbf93b08760696',
    'build/m10a2-terminal2-agent-native/scld-host':'339561669ebc16143ac8b1e7099830503e818ef150b6dfaa5ceb8a5cdf0ed034',
    'build/m10a2-p6-target-refresh-03/p6-generation.json':'fbc1c4519c9f432a67772eb9b7989b166edb7ce29a20a65cc88c5bae63774f2f',
}
COMPILER='build/m10a2-p6-adapter-02/sccc-host'
LINKER='build/m10a2-terminal2-agent-native/scld-host'
PREVIOUS='build/m10a2-p6-target-refresh-03/p6-generation.json'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-report',type=Path,required=True)
    parser.add_argument('--host-report-sha256',required=True)
    parser.add_argument('--out',type=Path,default=Path('build/m10a2-p6-sco-09'))
    args=parser.parse_args()
    for name,expected in FIXED.items():
        if sha(ROOT/name)!=expected:raise ValueError('固定来源改变：'+name)
    proof=args.host_report.resolve(strict=True)
    if not proof.is_relative_to(ROOT/'build') or sha(proof)!=args.host_report_sha256:
        raise ValueError('HOST报告必须来自build且完整SHA一致')
    host=read(proof)
    if host.get('exit_code')!=0 or any(host.get(k) not in (None,[]) for k in
            ('changed_source_files','source_files_modified_during_build')):
        raise ValueError('HOST报告不是源码保持的成功构建')
    host_inputs=host.get('inputs_sha256') or {row['path']:row['sha256'] for row in host['inputs']}
    spec=importlib.util.spec_from_file_location('gui02_existing_generation_model',ROOT/'drafts/m10a2-p6-guest-generation/model.py')
    contract=importlib.util.module_from_spec(spec);spec.loader.exec_module(contract)
    # 与已有refresh用同一literal闭包；不能靠入口摘要掩盖共享inc或头的改变。
    current,_=contract.source_plan()
    previous=read(ROOT/PREVIOUS)
    if previous.get('status')!='DECLARED_P6_NATIVE_GENERATION_PASS' or previous.get('current_inputs_unchanged') is not True:
        raise ValueError('原native来源不是守恒PASS')
    if current['compiler_closure']!=previous['preflight']['compiler_closure']:
        raise ValueError('SCCC闭包改变，不能复用原工具')
    changed=[name for name,closure in current['source_closures'].items()
             if closure!=previous['preflight']['source_closures'].get(name)]
    if 'SCLD' in changed or not set(changed)<=set(LABELS):
        raise ValueError('实际变化超出本批12标签或触及旧SCLD：'+repr(changed))
    selected=[name for name in LABELS if name in changed]
    if not selected:raise ValueError('本批没有实际变化，不生成空PASS命令')
    required=dict(current['compiler_closure'])
    for name in selected:required.update(current['source_closures'][name])
    # provider ABI版本同样进入manifest。HOST报告必须固定实际读取的9份目录。
    for name,folder in (('SCLDR','runtime'),('SC32API','sc32'),('SCCLIB','stdlib'),
            ('SCLIB','sclib'),('SCUI','scui'),('SCSHELL','scshell'),('SCGFX','scgfx'),
            ('SCTERM','scterm'),('SCSOUND','scsound')):
        stem='SCLROOT' if name=='SCLDR' else name
        suffix='.JSON' if name=='SCTERM' else '.json'
        key='user/'+folder+'/'+stem+suffix
        required[key]=sha(ROOT/key)
    for name,expected in required.items():
        if host_inputs.get(name)!=expected:raise ValueError('HOST报告未绑定当前输入：'+name)
    adapter=read(ROOT/'build/m10a2-p6-adapter-02/build.json')
    old_host=read(ROOT/'build/m10a2-p6-sco-08/report.json')
    if adapter.get('exit_code')!=0 or adapter['source_sha256']!=required['user/s3c.c'] or adapter['binary_sha256']!=FIXED[COMPILER]:
        raise ValueError('原HOST-IO适配器来源不匹配')
    if old_host.get('status')!='PASS' or old_host['compiler_sha256']!=FIXED[COMPILER] or old_host['linker_sha256']!=FIXED[LINKER]:
        raise ValueError('不是08实际执行的同一后端')
    out=args.out.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('需要build内新SCO输出目录')
    # 旧compiler路径缓冲保留64B；新驱动不悄悄截断长日志/map路径。
    out_relative=out.relative_to(ROOT).as_posix()
    if any(len((out_relative+'/'+name+'.SCO.map').encode())>63 for name in selected):
        raise ValueError('对象/map输出路径超过现有compiler路径合同')
    command=['python3','tools/check_m10a2_p6_objects.py','--out',out_relative,'--compiler',COMPILER]
    for name in selected:command+=['--case',name]
    promotion=[]
    for name in selected:
        template='user/'+current['cases'][name][1]
        target=Path(template).with_suffix('.NATIVE.JSON').as_posix()
        promotion.append(dict(label=name,source=out_relative+'/'+name+'.NATIVE.JSON',
            target=target,before_sha256=sha(ROOT/target),host_template=template,
            host_template_sha256=sha(ROOT/template)))
    result=dict(status='READ_ONLY_PLAN_NOT_BUILT_NOT_NATIVE_GENERATION_PASS',
        host_report=str(proof),host_report_sha256=sha(proof),fixed_source_sha256=FIXED,
        changed_labels=selected,unchanged_labels=[name for name in current['cases'] if name not in selected],
        cases={name:current['cases'][name] for name in selected},
        source_closures={name:current['source_closures'][name] for name in selected},
        required_HOST_input_sha256=required,command=command,
        wsl_command='wsl.exe --cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore '+shlex.join(command),
        root_only_promotion_after_all_cases_PASS=promotion,
        old_G1_and_native_SCCC_SCLD_unchanged=True,SCSOUND_exception='HOST_CODEC_EXCEPTION_PRESERVED')
    # 打印前再核输入；plan没有生成对象或提升主树的能力。
    if any(sha(ROOT/name)!=expected for name,expected in required.items()) or sha(proof)!=args.host_report_sha256:
        raise ValueError('只读计划期间输入改变')
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0


if __name__=='__main__':raise SystemExit(main())
