"""只生成本次单路径消费者修复草稿，不改主树、不编译、不运行VM。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RELATIVE = 'user/notes2/NOTES2.C'
EXPECTED = 'b9e03693a4be610f22f8c49418fb5f25c9fec5bf120e63913bf90334512ef8e0'

PARSER = '''/* MODERN Shell传的是原始参数尾部，不是已拆好的argv。这里只消费一条
 * document；不把多参数拼成路径，也不随意删除路径内部的引号。 */
static ScStatus n_start_path(char *argument,ScU32 capacity,ScU32 *bytes)
{
    if(*bytes>=capacity || strlen(argument)!=*bytes)return SC32_E_INVALID;
    ScU32 i=0,used=0,quoted=0,closed=0;
    while(i<*bytes && (argument[i]==' ' || argument[i]=='\\t'))i++;
    if(i==*bytes){argument[0]=0;*bytes=0;return 0;}
    quoted=argument[i]=='"';if(quoted)i++;
    while(i<*bytes){unsigned char c=(unsigned char)argument[i++];
        if(quoted && c=='"'){closed=1;break;}
        if(!quoted && (c==' ' || c=='\\t'))break;
        if((!quoted && c=='"') || c<32 || c==127)return SC32_E_INVALID;
        /* Shell只对双引号和反斜杠加转义；其它反斜杠序列不是本合同。
         * 原地压缩始终写在读指针之前，不覆盖尚未检查的尾部参数。 */
        if(quoted && c=='\\\\'){
            if(i==*bytes || (argument[i]!='"' && argument[i]!='\\\\'))return SC32_E_INVALID;
            c=(unsigned char)argument[i++];
        }
        argument[used++]=(char)c;
    }
    if(quoted && !closed)return SC32_E_INVALID;
    while(i<*bytes && (argument[i]==' ' || argument[i]=='\\t'))i++;
    if(i!=*bytes || !used)return SC32_E_INVALID;
    argument[used]=0;*bytes=used;return 0;
}

'''

OLD = '''    if(status>=0){
        char argument[1024];ScU32 bytes=0;
        status=ScProcessArguments(argument,sizeof(argument),&bytes);
        if(status>=0)status=n_load(bytes?argument:"/HOME/notes.txt",bytes?0:1);
        if(status<0){n_error(status,bytes?argument:"/HOME/notes.txt");status=0;}
    }'''

NEW = '''    if(status>=0){
        char argument[1024];ScU32 bytes=0,decoded=0;argument[0]=0;
        status=ScProcessArguments(argument,sizeof(argument),&bytes);
        if(status>=0){status=n_start_path(argument,sizeof(argument),&bytes);if(status>=0)decoded=1;}
        if(status>=0)status=n_load(bytes?argument:"/HOME/notes.txt",bytes?0:1);
        if(status<0){n_error(status,decoded?(bytes?argument:"/HOME/notes.txt"):"启动参数必须是单一路径");status=0;}
    }'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    source = ROOT/RELATIVE
    if sha(source) != EXPECTED:
        raise ValueError('Notes2固定原像已改变')
    original = source.read_bytes()
    newline = '\r\n' if b'\r\n' in original else '\n'
    before = original.decode('utf-8').replace('\r\n', '\n')
    if before.count(OLD) != 1 or before.count('int main(void)\n') != 1:
        raise ValueError('入口原文不是唯一匹配')
    after = before.replace('int main(void)\n', PARSER+'int main(void)\n', 1).replace(OLD, NEW, 1)
    target = HERE/'overlay'/RELATIVE
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(after.replace('\n', newline).encode('utf-8'))
    patch = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                       fromfile='a/'+RELATIVE, tofile='b/'+RELATIVE))
    (HERE/'notes-single-path.patch').write_text(patch, encoding='utf-8', newline='\n')
    mains = {'FILES2': 'user/files2/FILES2.C', 'NOTES2': RELATIVE,
             'LENS2': 'user/imageapps/LENS2.C', 'SOUND2': 'user/sound2/SOUND2.C',
             'CANVAS2': 'user/imageapps/CANVAS2.C'}
    consumers = {'FILES2': 'user/files2/files2_view.inc', 'NOTES2': RELATIVE,
                 'LENS2': 'user/imageapps/lifecycle.c', 'SOUND2': 'user/sound2/SOUND2.C',
                 'CANVAS2': 'user/imageapps/lifecycle.c'}
    decisions = {'FILES2':'UNCHANGED_ALREADY_DECODES_SINGLE_START_PATH',
                 'NOTES2':'PATCH_RAW_SINGLE_DOCUMENT_CONSUMER',
                 'LENS2':'UNCHANGED_ALREADY_HANDLES_OUTER_PAIR_AND_OPTIONAL_OPEN',
                 'SOUND2':'UNCHANGED_ALREADY_STRICT_SINGLE_DOCUMENT_PARSER',
                 'CANVAS2':'UNCHANGED_ALREADY_HANDLES_OUTER_PAIR_AND_OPTIONAL_OPEN'}
    records = {}
    for label, relative in mains.items():
        actual = ROOT/relative
        consumer = ROOT/consumers[label]
        changed = label == 'NOTES2'
        records[label] = dict(entry=relative, before_sha256=sha(actual),
            result_sha256=sha(target) if changed else sha(actual), modified=changed,
            consumer=consumers[label], consumer_before_sha256=sha(consumer),
            consumer_result_sha256=sha(target) if changed else sha(consumer), decision=decisions[label])
    gui = json.loads((ROOT/'build/m10a2-p6-gui-run-02/p6-gui.json').read_text(encoding='utf-8'))
    for row in records.values():
        for key, relative in (('before_sha256',row['entry']), ('consumer_before_sha256',row['consumer'])):
            found = [v for k,v in gui['input_sha256'].items() if k.replace('\\','/').endswith('/'+relative)]
            if not found or any(v != row[key] for v in found):
                raise ValueError('实际入口与GUI02输入摘要不同：'+relative)
    # strlen本已由真正Notes源码使用；私有解析器没有增加动态导入或公共ABI。
    for suffix in ('JSON', 'NATIVE.JSON'):
        manifest = json.loads((ROOT/'user/notes2'/('NOTES2.'+suffix)).read_text(encoding='utf-8'))
        if 'strlen' not in {row['name'] for row in manifest['imports']}:
            raise ValueError('Notes2清单没有已有strlen')
    screenshot = ROOT/'build/m10a2-p6-gui-run-02/p6-notes-associated-status.png'
    record = dict(status='SOURCE_PATCH_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED', applications=records,
                  changed_paths=[RELATIVE], refresh_labels=['NOTES2'], patch_sha256=sha(HERE/'notes-single-path.patch'),
                  no_new_imports=True, manifests_not_modified=True, no_new_shared_argument_API=True,
                  SCLIB_header=dict(path='user/sclib/SCLIB.H',sha256=sha(ROOT/'user/sclib/SCLIB.H'),
                                    public_argument_parser_declared=False),
                  actual_GUI02_source_pins_match=True,
                  native_report=gui['native_report'], native_report_sha256=gui['native_report_sha256'],
                  screenshot=dict(path=str(screenshot),sha256=sha(screenshot),visually_inspected=True),
                  original_main_unchanged=sha(source)==EXPECTED,
                  scope='Only Notes2 raw single-path startup consumer; Shell/SCAPI/ABI and other four consumers unchanged')
    (HERE/'PINS.json').write_text(json.dumps(record, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    ast.parse((HERE/'prepare.py').read_text(encoding='utf-8'))
    delivery = {p.relative_to(HERE).as_posix():dict(sha256=sha(p),bytes=p.stat().st_size)
                for p in sorted(HERE.rglob('*')) if p.is_file() and p.name!='DELIVERY.json'}
    (HERE/'DELIVERY.json').write_text(json.dumps(dict(status=record['status'],files=delivery),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(record, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
