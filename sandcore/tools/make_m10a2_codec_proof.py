"""只从真实现存host构建报告提取固定SCSOUND例外，不生成库或native证明。"""
from pathlib import Path
import argparse,hashlib,json,sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent if HERE.name=='tools' else HERE.parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import m10a2_sdk_final as policy
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build-report',type=Path,required=True);parser.add_argument('--out',type=Path,required=True);args=parser.parse_args()
    if args.out.exists():raise FileExistsError('proof output must be new')
    report_path=args.build_report.resolve(strict=True);report=json.loads(report_path.read_text(encoding='utf-8'))
    if not report_path.is_relative_to(ROOT) or report.get('exit_code')!=0 or report.get('source_files_modified_during_build')!=[] or not str(report.get('scope','')).startswith('M10A2_'):raise ValueError('not an actual successful conserving M10a2 host report')
    sources={}
    for row in report['inputs']:
        if row['path'] in sources:raise ValueError('duplicate build source')
        sources[row['path']]=row['sha256']
    required=policy.codec_sources(ROOT)|{policy.CODEC_MANIFEST};pins={}
    for component in policy.codec_components(json.loads((ROOT/'third_party/M9-SOURCES.json').read_text(encoding='utf-8'))):
        for file,digest in component['files'].items():
            if hashlib.sha256((ROOT/component['source']/file).read_bytes()).hexdigest()!=digest:raise ValueError('fixed codec vendor/license changed')
        for file in component['local_files']:(ROOT/component['source']/file).read_bytes()
    for name in sorted(required):
        actual=hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        if sources.get(name)!=actual:raise ValueError('host report lacks current codec implementation/header: '+name)
        pins[name]=actual
    artifacts=[v for v in report['outputs'] if Path(v['path']).name==policy.CODEC]
    if len(artifacts)!=1:raise ValueError('host report must contain exact one SCSOUND.SCL')
    artifact=artifacts[0];body=(ROOT/artifact['path']).read_bytes()
    if len(body)!=artifact['bytes'] or hashlib.sha256(body).hexdigest()!=artifact['sha256']:raise ValueError('host artifact changed')
    proof=dict(schema=1,mode='USER_APPROVED_HOST_CODEC_EXCEPTION',name=policy.CODEC,build_report=report_path.relative_to(ROOT).as_posix(),
        build_report_sha256=hashlib.sha256(report_path.read_bytes()).hexdigest(),artifact=artifact['path'],sha256=artifact['sha256'],bytes=artifact['bytes'],source_input_sha256=pins,license_manifest='third_party/M9-SOURCES.json')
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print('HOST_CODEC_PROOF_WRITTEN_NOT_NATIVE')
if __name__=='__main__':main()
