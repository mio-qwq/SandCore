"""只读主树，输出局部补丁/前像摘要；不构建、不应用、不启动客体。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('anchor not unique: ' + old[:90])
    return text.replace(old, new, 1)


def main():
    paths = ('user/scshell/SCSHELLPRIVATE.H', 'user/scshell/shell_menu.c',
             'user/scshell/SCSHELL.json', 'docs/SCSHELL-DESIGN.md')
    old_bytes = {p: (ROOT / p).read_bytes() for p in paths}
    old = {p: b.decode('utf-8').replace('\r\n', '\n') for p, b in old_bytes.items()}
    pin_file = HERE / 'PINS.json'
    if pin_file.exists():
        pins = json.loads(pin_file.read_text(encoding='utf-8'))
        for p, b in old_bytes.items():
            if sha(b) != pins['modified'][p]['before_sha256']:
                raise ValueError('frozen before-image differs: ' + p)
    new = dict(old)
    p = paths[0]
    new[p] = once(new[p], 'ScShellDialog *dialog;ScUiDialog *prompt;',
                  'ScShellDialog *dialog;ScUiNode prompt,prompt_name,prompt_status;\n'
                  '    ScU32 prompt_listener,prompt_intent;')

    p = paths[1]
    new[p] = once(new[p], 'if(m->prompt)ScUiDialogRelease(m->prompt);',
                  'if(m->prompt_listener)ScUiDisconnect(m->desc.ui,m->prompt_listener);\n'
                  '        if(m->prompt)ScUiNodeDestroy(m->desc.ui,m->prompt);')
    start = new[p].index('static ScStatus sh_menu_name_prompt(')
    end = new[p].index('static ScStatus sh_menu_name_commit(', start)
    new[p] = new[p][:start] + '#include "shell_name_prompt.inc"\n' + new[p][end:]
    start = new[p].index('static ScStatus sh_menu_name_commit(')
    end = new[p].index('ScStatus ScShellMenuExecute(', start)
    commit = new[p][start:end]
    commit = once(commit, 'ScConfig *values=0;\n    ScValue v;',
                  'ScString *input=0;\n    ScView v;')
    commit = once(commit, 'ScU32 i;', 'ScU32 i,kind=m->prompt_kind;')
    commit = once(commit,
        's=ScUiDialogCopyValues(m->prompt,&values);\n'
        '    if(s<0)goto done;\n'
        '    memset(&v,0,sizeof(v));\n'
        '    v.size=sizeof(v);\n'
        '    v.version=1;\n'
        '    s=ScConfigGet(values,"operation","name",&v);',
        's=ScUiNodeCopyText(m->desc.ui,m->prompt_name,&input);\n'
        '    if(s<0)goto done;\n'
        '    memset(&v,0,sizeof(v));\n'
        '    v.size=sizeof(v);\n'
        '    v.version=1;\n'
        '    s=ScStringView(input,&v);')
    commit = once(commit, 'if(v.type!=SC_VALUE_UTF8||!v.bytes||v.bytes>63||',
                  'if(!v.bytes||v.bytes>63||')
    commit = commit.replace('m->prompt_kind', 'kind')
    commit = once(commit, 'ScU32 i,kind=kind;', 'ScU32 i,kind=m->prompt_kind;')
    commit = once(commit, '    if(kind==0x7300) {\n        ScShellConflictDecision decision;',
        '    /* 同owner只准一个modal child；校验成功才让出名称窗，再建原进度窗。 */\n'
        '    s=sh_menu_name_close(m);\n'
        '    if(s<0)goto done;\n'
        '    if(kind==0x7300) {\n        ScShellConflictDecision decision;')
    commit = once(commit, '    done:free(name);',
        '    if(s<0&&!m->base.closing&&(!m->job||(kind==0x7300&&m->job->conflict_valid))) {\n'
        '        /* Begin/Resolve没有成功，保留原输入供修正；真正成功仍只由原任务ACK决定。 */\n'
        '        sh_menu_name_open(m,kind,name);\n'
        '    }\n'
        '    done:free(name);')
    commit = once(commit, 'ScConfigRelease(values);', 'ScStringRelease(input);')
    new[p] = new[p][:start] + commit + new[p][end:]
    start = new[p].index('    if(m->prompt) {', new[p].index('ScStatus ScShellMenuStep('))
    end = new[p].index('    if(s>=0&&m->dialog)', start)
    new[p] = new[p][:start] + (
        '    if(m->prompt&&m->prompt_intent) {\n'
        '        ScU32 intent=m->prompt_intent;\n'
        '        m->prompt_intent=0;\n'
        '        if(intent==SC_UI_DIALOG_CANCEL)s=sh_menu_name_close(m);\n'
        '        else {\n'
        '            s=sh_menu_name_commit(m);\n'
        '            if(s<0&&m->prompt)sh_menu_name_error(m,s);\n'
        '        }\n'
        '    }\n') + new[p][end:]

    p = paths[2]
    new[p] = once(new[p], '"name": "ScUiDialogCopyValues"', '"name": "ScUiEditSelectAll"')
    new[p] = once(new[p], '"name": "ScUiPropertySheet"', '"name": "ScUiMetricsQuery"')
    manifest = json.loads(new[p])
    imports = [v['name'] for v in manifest['imports']]
    assert len(imports) == len(set(imports))
    assert len(manifest['exports']) == len(json.loads(old[p])['exports'])

    p = paths[3]
    new[p] += ('\n2026-10-16名称提示修订：ScShellMenu的新建目录/重命名/冲突改名'
        '使用公开SCUI模态Window和标准Label/Textbox/Button，320×170逻辑客户区、'
        '中文标题及新建/重命名/取消动作，无PropertySheet页签或Apply。焦点全选，'
        'Enter确认、Esc/关闭取消；非法输入保留原文。校验后先销提示窗口让出同owner'
        '模态槽，再进入原受管文件事务，只有原持久ACK可确认文件结果。'
        'Properties及其完整元数据/权限页不变，公开ABI/导出不变。'
        '本修复为隔离源码草稿，尚未构建或客体验证。\n')

    fragment_path = 'user/scshell/shell_name_prompt.inc'
    fragment = (HERE / 'shell_name_prompt.inc').read_text(encoding='utf-8')
    new[fragment_path] = fragment
    patches = []
    pin_data = {'status': 'SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED', 'modified': {}, 'added': {}}
    for p, text in new.items():
        data = text.encode('utf-8')
        out = HERE / 'overlay' / p
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        if p in old:
            pin_data['modified'][p] = {'before_sha256': sha(old_bytes[p]),
                'after_sha256': sha(data), 'before_bytes': len(old_bytes[p]), 'after_bytes': len(data)}
            patches.extend(difflib.unified_diff(old[p].splitlines(True), text.splitlines(True),
                fromfile='a/' + p, tofile='b/' + p, n=3))
        else:
            pin_data['added'][p] = {'after_sha256': sha(data), 'after_bytes': len(data)}
            patches.extend(difflib.unified_diff([], text.splitlines(True), fromfile='/dev/null',
                tofile='b/' + p, n=3))
    patch = ''.join(patches).encode('utf-8')
    (HERE / 'files-name-prompt.patch').write_bytes(patch)
    pin_data['patch_sha256'] = sha(patch)
    pin_file.write_text(json.dumps(pin_data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    ast.parse(Path(__file__).read_text(encoding='utf-8'))
    print(json.dumps({'modified': len(paths), 'added': 1, 'exports': len(manifest['exports']),
        'imports': len(imports), 'patch_sha256': sha(patch), 'status': pin_data['status']}))


if __name__ == '__main__':
    main()
