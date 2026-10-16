"""从实际LOGIN2和系统库发行字节生成CORE公开摘要，不碰私钥。"""
from pathlib import Path
import argparse
import hashlib
import json
import sys
ROOT=Path(__file__).resolve().parents[1]
if not (ROOT/'tools/scd_format.py').exists():ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools'))
import scd_format
import scl_format
def row(data):return ','.join('0x%02x'%x for x in hashlib.sha256(data).digest())
def put(path,text):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_text(encoding='ascii')!=text:path.write_text(text,encoding='ascii',newline='\n')
def main():
    p=argparse.ArgumentParser();p.add_argument('--login',type=Path,required=True);p.add_argument('--libraries',type=Path,required=True)
    p.add_argument('--unicode',type=Path,required=True);p.add_argument('--program-out',type=Path,required=True);p.add_argument('--resources-out',type=Path,required=True);a=p.parse_args()
    body=a.login.read_bytes();contract=scd_format.validate_scx(body)
    if a.login.name.upper()!='LOGIN2.SCX' or len(body)>262144:raise ValueError('need exact fixed LOGIN2 dynamic SCX')
    names={x['name'] for x in contract['dependencies']}
    if not {'SC32API.SCL','SCUI.SCL'}<=names or names-{'SC32API.SCL','SCUI.SCL','SCCLIB.SCL','SCLIB.SCL'}:raise ValueError('login dependencies differ from minimal contract')
    put(a.program_out,'#ifndef SANDCORE_LOGIN_PROGRAM_ISSUE_H\n#define SANDCORE_LOGIN_PROGRAM_ISSUE_H\nstatic const u32 login_issued_bytes=%du;\nstatic const u8 login_issued_sha256[32]={%s};\n#endif\n'%(len(body),row(body)))
    resources=[];tables={}
    for name in ('SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL','SCUI.SCL'):
        data=(a.libraries/name).read_bytes();info=scl_format.validate(data)
        if info['name']!=name:raise ValueError('library identity mismatch')
        tables[name]=info
        resources.append(('SYS/LIB/'+name,data))
    for item in [contract,*tables.values()]:
        for dependency in item['dependencies']:
            library=tables.get(dependency['name'])
            if not library or library['abi_major']!=dependency['abi_major'] or library['abi_minor']<dependency['min_minor'] or library['library_version']<dependency['min_version']:raise ValueError('issued library dependency mismatch '+dependency['name'])
        for imported in item['imports']:
            library=tables.get(imported['dependency']);export=next((x for x in library['exports'] if x['name']==imported['name']),None) if library else None
            if not export or export['kind']!=imported['kind'] or export.get('size',1)<imported.get('size',1):raise ValueError('issued library symbol missing '+imported['name'])
    data=a.unicode.read_bytes()
    if data[:8]!=b'SCUT15_1':raise ValueError('need actual UAX15_1.SCU asset')
    resources.append(('SYS/UI/UAX15_1.SCU',data))
    text='#ifndef SANDCORE_LOGIN_RESOURCE_ISSUE_H\n#define SANDCORE_LOGIN_RESOURCE_ISSUE_H\n'
    text+='typedef struct {const char *path;u32 bytes;u8 sha256[32];} login_resource_issue_t;\n#define LOGIN_RESOURCE_COUNT 6u\nstatic const login_resource_issue_t login_issued_resources[6]={\n'
    for path,data in resources:text+='{"%s",%du,{%s}},\n'%(path,len(data),row(data))
    text+='};\n#endif\n';put(a.resources_out,text)
    print(json.dumps(dict(login_bytes=len(body),login_sha256=hashlib.sha256(body).hexdigest(),resources=[dict(path=p,bytes=len(d),sha256=hashlib.sha256(d).hexdigest()) for p,d in resources])))
if __name__=='__main__':main()
