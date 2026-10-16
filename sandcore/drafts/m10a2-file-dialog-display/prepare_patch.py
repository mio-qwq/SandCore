"""只从固定原像生成三文件最小补丁；不应用主树，不编译或运行客体。"""
from pathlib import Path
import difflib,hashlib,json,re
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
PINS={
 'user/scui/ui_dialog.c':'3edbc873669548cefa6495e191c2b3a88544d9d994e7134d7002da9b2607ac4d',
 'user/scui/SCUI.json':'d5f76e378f1e1de18499604185dab1cbb00310b9de410721bda50a880c88f553',
 'user/scui/SCUI.NATIVE.JSON':'f7ba0ca60219804635c91ae538059d706c61d1d3c07e444554eb35d042e59fbc'}
def sha(body):return hashlib.sha256(body).hexdigest()
def one(text,old,new):
    if text.count(old)!=1:raise ValueError('固定锚点不唯一或丢失：'+old[:80])
    return text.replace(old,new)
def replay(original,patch):
    # 沿正式pinned工具的保留原行尾规则在内存核对，绝不调用写主树入口。
    lines=patch.splitlines();source=original.decode('utf-8').splitlines(keepends=True);out=[];cursor=0;index=2
    ending='\r\n' if b'\r\n' in original else '\n'
    while index<len(lines):
        match=re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@.*',lines[index])
        if not match:raise ValueError('补丁hunk损坏')
        start=int(match[1])-1;old_count=int(match[2] or 1);new_count=int(match[4] or 1)
        if start<cursor:raise ValueError('补丁顺序损坏')
        out.extend(source[cursor:start]);cursor=start;index+=1;consumed=produced=0
        while index<len(lines) and not lines[index].startswith('@@ '):
            line=lines[index];index+=1
            if not line or line[0] not in ' +-':raise ValueError('补丁行损坏')
            if line[0] in ' -':
                if source[cursor].rstrip('\r\n')!=line[1:]:raise ValueError('原像hunk不符')
                if line[0]==' ':out.append(source[cursor]);produced+=1
                cursor+=1;consumed+=1
            else:out.append(line[1:]+ending);produced+=1
        if consumed!=old_count or produced!=new_count:raise ValueError('补丁长度不符')
    out.extend(source[cursor:]);return ''.join(out).encode('utf-8')
def main():
    before={};after={};patches=[];records={}
    for name,wanted in PINS.items():
        raw=(ROOT/name).read_bytes()
        if sha(raw)!=wanted:raise ValueError('main已变，禁止重定基或模糊应用：'+name)
        before[name]=raw;text=raw.decode('utf-8')
        if name.endswith('ui_dialog.c'):
            text=one(text,'#include "SCUIPRIVATE.H"','#include "SCUIPRIVATE.H"\n#include "../SC32PATH.H"')
            text=one(text,'else if(request->field==2){reply->value.type=SC_VALUE_U32;\n        reply->value.lo=row->kind;\n    }',
                'else if(request->field==2){\n        /* 类型列只翻译呈现；目录kind仍供原排序、导航和权限检查。 */\n        const char *kind=row->kind==SC32_PATH_DIRECTORY?"文件夹":"文件";\n        reply->value.type=SC_VALUE_UTF8;\n        reply->value.data=kind;\n        reply->value.bytes=(ScU32)strlen(kind);\n    }')
            text=one(text,'columns[2].format=SC_UI_FORMAT_U32;','columns[2].format=SC_UI_FORMAT_TEXT;')
            text=one(text,'s=ScPathMetadata(full,&metadata);','/* 旧Metadata把缺失折成粗错误；保存新名字必须用精确Query。 */\n        s=ScPathQuery(full,&metadata);')
        else:
            text=one(text,'"name": "ScPathMetadata"','"name": "ScPathQuery"')
            old=json.loads(raw);new=json.loads(text)
            old_imports=old.pop('imports');new_imports=new.pop('imports')
            if old!=new or len(old_imports)!=len(new_imports):raise ValueError('清单出现越界变化')
            expected=[dict(v,name='ScPathQuery') if v['name']=='ScPathMetadata' else v for v in old_imports]
            if new_imports!=expected:raise ValueError('清单不止替换唯一导入')
        after[name]=text.encode('utf-8');old=raw.decode('utf-8').replace('\r\n','\n');new=text.replace('\r\n','\n')
        patch=''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/sandcore/'+name,tofile='b/sandcore/'+name))
        if replay(raw,patch)!=after[name]:raise ValueError('正式行尾语义下补丁结果不符')
        patches.append(patch);records[name]=dict(before_bytes=len(raw),after_bytes=len(after[name]),before_sha256=wanted,after_sha256=sha(after[name]))
    # 冻结源仍须相等；只生成小补丁/凭证，不创建main候选文件或修改旧草稿。
    if any((ROOT/name).read_bytes()!=raw for name,raw in before.items()):raise ValueError('生成期间源发生变化')
    (HERE/'file-dialog.patch').write_text(''.join(patches),encoding='utf-8',newline='\n')
    (HERE/'pins.json').write_text(json.dumps(PINS,indent=2)+'\n',encoding='utf-8')
    (HERE/'result-pins.json').write_text(json.dumps({n:sha(b) for n,b in after.items()},indent=2)+'\n',encoding='utf-8')
    result=dict(schema=1,status='FIXED_SOURCE_PATCH_PREPARED_NOT_APPLIED_NOT_COMPILED',targets=records,memory_only_pinned_patch_replay=True,source_unchanged_during_preparation=True,main_modified=False,build_executed=False,qemu_started=False)
    (HERE/'PREPARATION.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PINNED_FILE_DIALOG_PATCH_READY targets=3 no_main_write=true no_build=true')
if __name__=='__main__':main()
