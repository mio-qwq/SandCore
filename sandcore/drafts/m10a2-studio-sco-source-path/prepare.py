"""只在独占目录生成Studio的SCO源坐标修复，不改主树/编译器/旧草稿。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RELATIVE = 'user/studio2/studio_build.c'
EXPECTED = '0a2c2d65ae2634a7d3a04868d4e9df72282f73c200ee1df1ffcfa40b7ee2d789'

OLD = '''        char name[68];
        U take=stringbytes-offset;
        if(take>67u)take=67u;
        n=0;
        s=ScPathRead(in->object,strings+offset,name,take,&n);
        if(s<0||!memchr(name,0,n))return SC32_E_INVALID;'''

NEW = '''        /* SCCC把旧RESOLVE的根内名原样写入SCO，不能把它再当cwd相对名。
         * 先补显式根，再沿原path正规化；后面的工程/namespace/revision检查不变。
         * name+1仍有67B，读取上限同时给根斜杠和字符串终止留足空间。 */
        char name[68];name[0]='/';
        U take=stringbytes-offset;
        if(take>67u)take=67u;
        n=0;
        s=ScPathRead(in->object,strings+offset,name+1,take,&n);
        if(s<0||!memchr(name+1,0,n)||!name[1])return SC32_E_INVALID;'''

OLD_COMMENT = '''    /* 编译器本来最多64份source；这里只读取它真正记录的依赖身份，
     * 项目外非系统头必须显式加为header后重新构建，不能悄悄漏跟踪。 */'''
NEW_COMMENT = '''    /* 按实际source-file表枚举依赖身份；项目外非系统头必须显式
     * 加为header后重新构建，不能悄悄漏跟踪。 */'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    source = ROOT/RELATIVE
    if sha(source) != EXPECTED:
        raise ValueError('固定Studio原像已经改变')
    raw = source.read_bytes()
    newline = '\r\n' if b'\r\n' in raw else '\n'
    before = raw.decode('utf-8').replace('\r\n', '\n')
    if before.count(OLD) != 1 or before.count(OLD_COMMENT) != 1:
        raise ValueError('固定局部原文不是唯一匹配')
    after = before.replace(OLD, NEW, 1).replace(OLD_COMMENT, NEW_COMMENT, 1)
    target = HERE/'overlay'/RELATIVE
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(after.replace('\n', newline).encode('utf-8'))
    patch = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                       fromfile='a/'+RELATIVE, tofile='b/'+RELATIVE))
    (HERE/'studio-sco-source-root.patch').write_text(patch, encoding='utf-8', newline='\n')
    # 读真实既有SCO的source-file表，不把map展示文本或已删除的失败对象当成新运行证据。
    sco = ROOT/'build/m10a2-p6-target-refresh-03/objects/STUDIO2.SCO'
    body = sco.read_bytes()
    if body[:8] != b'SCO1MIO\0' or struct.unpack_from('<I',body,8)[0] != 1:
        raise ValueError('不是实际SCO v1')
    files,count = struct.unpack_from('<2I',body,52)
    strings,size = struct.unpack_from('<2I',body,68)
    if files>len(body) or count>(len(body)-files)//16 or strings>len(body) or size>len(body)-strings:
        raise ValueError('真实SCO表越界')
    names = []
    for i in range(count):
        offset,source_bytes,fnv,reserved = struct.unpack_from('<4I',body,files+i*16)
        if reserved or offset>=size or body.find(b'\0',strings+offset,strings+size)<0:
            raise ValueError('真实SCO源路径损坏')
        path = body[strings+offset:body.index(0,strings+offset)].decode('utf-8')
        if not path or path.startswith('/') or len(path.encode('utf-8'))>63:
            raise ValueError('当前producer的实际根内路径实证不匹配')
        names.append(dict(path=path,source_bytes=source_bytes,source_FNV1a32=fnv))
    gui = json.loads((ROOT/'build/m10a2-p6-gui-run-02/p6-gui.json').read_text(encoding='utf-8'))
    source_pins = [v for k,v in gui['input_sha256'].items() if k.replace('\\','/').endswith('/'+RELATIVE)]
    if not source_pins or any(v!=EXPECTED for v in source_pins):
        raise ValueError('原像不属于实际GUI02')
    dependencies = ('user/s3c.c','user/s3c_pp.inc','user/sccc_include.inc','user/s3c_sco.inc',
                    'user/SCOFMT.H','user/studio2/studio_io.c','docs/SC32API.md','docs/SCO-AND-LINKER.md')
    dependencies = {name:sha(ROOT/name) for name in dependencies}
    screenshot = ROOT/'build/m10a2-p6-gui-run-02/p6-studio-scw-build-progress.png'
    record = dict(status='SOURCE_PATCH_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED',
        modified={RELATIVE:dict(before_sha256=EXPECTED,result_sha256=sha(target),before_bytes=len(raw),result_bytes=target.stat().st_size)},
        patch_sha256=sha(HERE/'studio-sco-source-root.patch'), refresh_labels=['STUDIO2'],
        dependencies=dependencies, actual_GUI02_source_pin_match=True,
        actual_existing_sco=dict(path=str(sco),sha256=sha(sco),source_count=count,source_files=names,
            all_rootless=True,all_source_names_at_most_63_bytes=True),
        failed_RDSTATS_sco_recovered=False,
        screenshot=dict(path=str(screenshot),sha256=sha(screenshot),visually_inspected=True,
                        observed='Actual SCCC OK SCO / Compilation complete, then Studio project-external-source refusal -108; no link success'),
        validation_preserved=['path UTF8 and forbidden bytes','full canonical member comparison','normalized SYS/INC namespace only',
                              'system dependency ScPathQuery revisions','inputs_current revisions and alias checks'],
        no_new_imports=True, manifests_not_modified=True, compiler_and_SCO_format_not_modified=True,
        original_main_unchanged=sha(source)==EXPECTED)
    (HERE/'PINS.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    ast.parse((HERE/'prepare.py').read_text(encoding='utf-8'))
    # 明确只替换源名输入适配和失效的64项注释，严格验证主体逐字节复原。
    if after.replace(NEW,OLD,1).replace(NEW_COMMENT,OLD_COMMENT,1)!=before:
        raise ValueError('局部变换外出现意外修改')
    delivery = {p.relative_to(HERE).as_posix():dict(sha256=sha(p),bytes=p.stat().st_size)
                for p in sorted(HERE.rglob('*')) if p.is_file() and p.name!='DELIVERY.json'}
    (HERE/'DELIVERY.json').write_text(json.dumps(dict(status=record['status'],files=delivery),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:record[k] for k in ('status','modified','patch_sha256','refresh_labels','original_main_unchanged')},ensure_ascii=False,indent=2))
    print('DELIVERY_SHA256',sha(HERE/'DELIVERY.json'))


if __name__=='__main__':
    main()
