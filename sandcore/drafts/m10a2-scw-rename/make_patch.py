"""只读主源，准备SCW工作区局部补丁和两份小文本的固定移动计划。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(body):
    return hashlib.sha256(body).hexdigest()


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('anchor not unique: ' + before[:100])
    return text.replace(before, after, 1)


def main():
    paths = (
        'user/studio2/STUDIO2.C', 'user/studio2/studio_io.c',
        'user/studio2/studio_actions.c', 'user/studio2/studio_ui.c',
        'tools/build_m10a2_p6.py', 'tools/m10a2_sdk_final.py',
        'examples/m10a2-sdk/researchdesk/BUILD.SH',
        'examples/m10a2-sdk/researchdesk/README.md',
        'docs/SDK-RESEARCHDESK.md', 'docs/M10A2-SDK.md',
        'docs/M10A2-LIVE-GUI.md', 'docs/M10A2-CURRENT-GOAL.md',
        'docs/STUDIO2-DEBUG2-DESIGN.md',
    )
    bodies = {p: (ROOT / p).read_bytes() for p in paths}
    before = {p: b.decode('utf-8').replace('\r\n', '\n') for p, b in bodies.items()}
    pin_path = HERE / 'PINS.json'
    previous = json.loads(pin_path.read_text(encoding='utf-8')) if pin_path.exists() else None
    if previous:
        for p, body in bodies.items():
            if sha(body) != previous['modified'][p]['before_sha256']:
                raise ValueError('frozen before-image changed: ' + p)
    after = dict(before)

    p = paths[0]
    after[p] = once(after[p],
        '        const char *ext=strrchr(args,\'.\');\n'
        '        io_open(args,ext && (!strcmp(ext,".SCP")||!strcmp(ext,".scp"))?S_IO_PROJECT_OPEN:S_IO_OPEN,0);',
        '        U first=0;\n'
        '        while(first<n&&(args[first]==\' \'||args[first]==\'\\t\'))first++;\n'
        '        /* MODERN Shell为单个document加外引号；只剥成对引号，路径本身仍完整验证。 */\n'
        '        if(first<n&&args[first]==\'"\'&&n>first+1&&args[n-1]==\'"\'){first++;args[--n]=0;}\n'
        '        if(first<n){\n'
        '            s=strchr(args+first,\'"\')?SC32_E_INVALID:io_open(args+first,workspace_suffix(args+first)?S_IO_PROJECT_OPEN:S_IO_OPEN,0);\n'
        '            if(s<0)error(s,"无法打开指定文件");\n'
        '        }')

    p = paths[1]
    workspace_helpers = '''static U workspace_suffix(const char *name)
{
    const char *base=strrchr(name,'/');
    const char *suffix=strrchr(base?base+1:name,'.');
    return suffix&&same_name(suffix,".SCW");
}
static ScStatus workspace_save_name(const char *name,char out[68],U *implicit)
{
    ScStatus s=path(name,out);
    if(s<0)return s;
    const char *base=strrchr(out,'/');
    const char *suffix=strrchr(base?base+1:out,'.');
    *implicit=0;
    if(suffix)return workspace_suffix(out)?0:SC32_E_INVALID;
    U n=(U)strlen(out);
    if(n>60u)return SC32_E_RANGE;
    memcpy(out+n,".SCW",5);
    *implicit=1;
    return 0;
}
'''
    after[p] = once(after[p], 'static void pin_control(', workspace_helpers + 'static void pin_control(')
    start = after[p].index('static ScStatus io_open(')
    end = after[p].index('static ScStatus io_write_start(', start)
    block = after[p][start:end]
    block = once(block, '    if(s<0)return s;\n    if(kind==S_IO_OPEN)',
        '    if(s<0)return s;\n'
        '    if(kind==S_IO_PROJECT_OPEN&&!workspace_suffix(absolute))return SC32_E_INVALID;\n'
        '    if(kind==S_IO_OPEN)')
    after[p] = after[p][:start] + block + after[p][end:]
    after[p] = once(after[p],
        'static ScStatus io_write_start(ScString *snapshot,const char *name,U expected,U kind,U owner,U revision)',
        'static ScStatus io_write_start(ScString *snapshot,const char *name,U expected,U kind,U owner,U revision,U absent)')
    after[p] = once(after[p], '    U exists=s==0;\n    if(exists&&info.kind',
        '    U exists=s==0;\n'
        '    /* 自动扩展名没有对最终目标取得覆盖确认；这里只允许ABSENT，发布时继续原ABSENT条件。 */\n'
        '    if(absent&&exists)return SC32_E_EXISTS;\n'
        '    if(exists&&info.kind')
    after[p] = once(after[p], 'S_IO_SAVE,d->id,info.revision);', 'S_IO_SAVE,d->id,info.revision,0);')
    start = after[p].index('static ScStatus io_project_save(')
    end = after[p].index('static ScStatus io_candidate(', start)
    after[p] = after[p][:start] + '''static ScStatus io_project_save(const char *name)
{
    char absolute[68];
    U implicit=0;
    ScStatus s=workspace_save_name(name,absolute,&implicit);
    if(s<0)return s;
    ScString *snapshot=0;
    s=project_encode(&snapshot);
    if(s>=0)s=io_write_start(snapshot,absolute,!strcmp(absolute,st.project_path)?st.project_disk_revision:0,S_IO_PROJECT_SAVE,0,st.project_revision,implicit);
    ScStringRelease(snapshot);
    return s;
}
''' + after[p][end:]

    p = paths[2]
    after[p] = once(after[p], '    ScUiFileDialogDesc d;\n    rec(&d,sizeof(d));',
        '    ScUiFileDialogDesc d;\n    ScUiMenuItem filter;\n    rec(&d,sizeof(d));')
    after[p] = once(after[p], '?"打开工程":kind==S_DIALOG_PROJECT_SAVE?"保存工程"',
        '?"打开工作区（.SCW）":kind==S_DIALOG_PROJECT_SAVE?"保存工作区（默认 .SCW）"')
    after[p] = once(after[p], '    if(kind==S_DIALOG_ADD)d.flags|=SC_UI_FILE_MULTI;',
        '    if(kind==S_DIALOG_ADD)d.flags|=SC_UI_FILE_MULTI;\n'
        '    if(kind==S_DIALOG_PROJECT_OPEN||kind==S_DIALOG_PROJECT_SAVE){\n'
        '        rec(&filter,sizeof(filter));\n'
        '        filter.item=1;filter.command=1;\n'
        '        filter.label=text("SCW 工作区（*.SCW）");\n'
        '        /* 公共glob按字节匹配；列齐3个ASCII字母的大小写，不假定FS返回大写。 */\n'
        '        filter.help=text("*.SCW;*.scw;*.Scw;*.sCW;*.SCw;*.ScW;*.sCw;*.scW");\n'
        '        d.filters=&filter;d.filter_count=1;d.selected_filter=1;\n'
        '    }')
    after[p] = once(after[p], 'd.title=text("工程配置");', 'd.title=text("工作区配置");')
    after[p] = once(after[p], 'error(s,"操作未完成；原文档与工程保留");',
        'error(s,kind==S_DIALOG_PROJECT_SAVE?"工作区未保存；请使用 .SCW。自动补全只创建新目标；覆盖时显式选择完整文件名。":"操作未完成；原文档与工程保留");')
    p = paths[3]
    for a, b in (("新建工程", "新建工作区"), ("打开工程…", "打开工作区…"), ("保存工程", "保存工作区")):
        after[p] = once(after[p], '"' + a + '"', '"' + b + '"')

    for p in paths[4:6]:
        after[p] = once(after[p], "'.scp'", "'.scw'")
    p = paths[5]
    after[p] = once(after[p], "'SCTERM.md','SDK-RESEARCHDESK.md')", "'SCTERM.md','SDK-RESEARCHDESK.md','SCW-WORKSPACE.md')")
    p = paths[6]
    after[p] = once(after[p], '# Build as an ordinary signed-in user. No /SYS output is written.',
        '# ResearchDesk SCW workspaces: RDSTATS.SCW (plugin), RDESK.SCW (application).\n'
        '# Build as an ordinary signed-in user. No /SYS output is written.')
    for p in paths[7:9]:
        after[p] = after[p].replace('RDSTATS.SCP', 'RDSTATS.SCW').replace('RDESK.SCP', 'RDESK.SCW')
        after[p] = once(after[p], 'Studio2 可分别打开 RDSTATS.SCW、RDESK.SCW，',
            'SCW（SandCore Workspace）是 Studio 2 的工程工作区。通过“工程 → 打开工作区…”分别打开 RDSTATS.SCW、RDESK.SCW，')
        after[p] += '\n修订记录：2026-10-16，SCW工作区路径、公开Studio入口和ResearchDesk构建合同。\n'
    p = paths[9]
    after[p] = once(after[p], '、SCP、manifest', '、SCW工作区、manifest')
    after[p] = once(after[p], '详细命令与普通用户结束条件见 SDK-RESEARCHDESK 和示例 README。',
        'RDSTATS.SCW和RDESK.SCW分别描述插件与四TU应用，SCW即SandCore Workspace（工程工作区）。'
        '详细合同见SCW-WORKSPACE；命令与普通用户结束条件见 SDK-RESEARCHDESK 和示例 README。')
    after[p] += '\n2026-10-16：公开SCW工作区合同、示例路径和SDK归档接线。\n'
    p = paths[10]
    after[p] = after[p].replace('RDSTATS.SCP', 'RDSTATS.SCW').replace('RDESK.SCP', 'RDESK.SCW')
    after[p] = once(after[p], '工程打开没有CtrlShiftO，', '工作区入口为“工程 → 打开工作区…”，没有CtrlShiftO，')
    after[p] = once(after[p],
        '最新界面要求：SCW作为正式工作区格式，入口、样例、筛选与文档自然使用该名称，不写后缀更名过程。',
        'SCW（SandCore Workspace）为工程工作区格式，入口、样例、筛选与文档使用 .SCW。')
    p = paths[11]
    after[p] += ('\n2026-10-16 SCW工作区：Studio 2 的工作区入口及SDK示例使用.SCW' +
        '（SandCore Workspace）。普通用户先打开/SYS/SDK/EXAMPLES/RDESK/RDSTATS.SCW构建独立SCL，'
        '再打开RDESK.SCW构建四TU应用；F5构建、F6运行、F7调试。此段为当前入口合同，实际执行证据另列。\n')
    p = paths[12]
    after[p] = once(after[p], '工程descriptor采用版本化、严格解析的普通用户配置文件，',
        '工程工作区使用.SCW（SandCore Workspace），其descriptor采用版本化、严格解析的普通用户配置文件，')
    after[p] += '\n修订：2026-10-16，SCW工作区入口、schema1与标准文件对话框合同。\n'
    after['docs/SCW-WORKSPACE.md'] = (HERE / 'SCW-WORKSPACE.md').read_text(encoding='utf-8')

    renames = []
    for stem in ('RDESK', 'RDSTATS'):
        src = 'examples/m10a2-sdk/researchdesk/' + stem + '.SCP'
        dst = 'examples/m10a2-sdk/researchdesk/' + stem + '.SCW'
        body = (ROOT / src).read_bytes()
        if (ROOT / dst).exists():
            raise ValueError('destination already exists: ' + dst)
        if previous and sha(body) != previous['renames'][src]['before_sha256']:
            raise ValueError('workspace before-image changed: ' + src)
        out = HERE / 'overlay' / dst
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(body)
        renames.append(dict(source=src, target=dst, before_sha256=sha(body), after_sha256=sha(body),
                            bytes=len(body), operation='EXACT_SMALL_FILE_MOVE_AT_ROOT_INTEGRATION'))

    patch = []
    pin = dict(status='SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED', modified={}, added={}, renames={})
    for p, content in after.items():
        out = HERE / 'overlay' / p
        out.parent.mkdir(parents=True, exist_ok=True)
        data = content.encode('utf-8')
        out.write_bytes(data)
        if p in before:
            pin['modified'][p] = dict(before_sha256=sha(bodies[p]), after_sha256=sha(data),
                                      before_bytes=len(bodies[p]), after_bytes=len(data))
            patch.extend(difflib.unified_diff(before[p].splitlines(True), content.splitlines(True),
                                              fromfile='a/'+p, tofile='b/'+p, n=3))
        else:
            pin['added'][p] = dict(after_sha256=sha(data), bytes=len(data))
            patch.extend(difflib.unified_diff([], content.splitlines(True), fromfile='/dev/null',
                                              tofile='b/'+p, n=3))
    for item in renames:
        pin['renames'][item['source']] = item
    patch_bytes = ''.join(patch).encode('utf-8')
    (HERE / 'scw-workspace.patch').write_bytes(patch_bytes)
    pin['patch_sha256'] = sha(patch_bytes)
    pin_path.write_text(json.dumps(pin, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (HERE / 'RENAMES.json').write_text(json.dumps(renames, indent=2) + '\n', encoding='utf-8')
    ast.parse(Path(__file__).read_text(encoding='utf-8'))
    print(json.dumps(dict(modified=len(pin['modified']), added=len(pin['added']),
                          small_moves=len(renames), patch_sha256=pin['patch_sha256'], status=pin['status'])))


if __name__ == '__main__':
    main()
