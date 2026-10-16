"""只生成Settings的可证局部修复/审阅；不改主树，不观察或操作当前VM。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
EXPECTED = {
    'user/settings2/settings_view.inc': '451467943eaf712b0bf23b5bd7e0a23eca87391fb6feaf2b10611a21f77c449b',
    'user/settings2/settings_model.inc': 'f23fdf29e74de721a7dba786c6d0bace8c3fbf8752d4c5310786bbb2a0658066',
    'user/settings2/settings_programs.inc': 'd177b98dce854dffc7b69b7638893005b5c76762d90855dbfac44ba4d5328d46',
    'user/settings2/install_view.inc': 'd935140ff2a70847d571eb599b5ddc6499367dd0412412db34cddd3ef7b04e34',
    'user/settings2/install_model.inc': 'd1a2672aafa3331c2cf98d7527ea4dcc3647557a5f308e7e7a0a4dca53349709',
}
REPLACEMENTS = {
    'user/settings2/settings_view.inc': [('''        columns[i].column=i;
        columns[i].field=i;''', '''        /* column是非零控件身份，field才是provider使用的零起始数据字段。 */
        columns[i].column=i+1;
        columns[i].field=i;''')],
    'user/settings2/settings_model.inc': [('''    if(q->operation==SC_UI_DATA_CELL){const char *z=q->column?settings.env[index].value:settings.env[index].key;''', '''    /* CELL由SCUI按列描述的field取值，不把可重排的column身份当数组下标。 */
    if(q->operation==SC_UI_DATA_CELL){const char *z=q->field?settings.env[index].value:settings.env[index].key;''')],
    'user/settings2/settings_programs.inc': [('''        if(q->column==0)z=program_string(row->label);
        else if(q->column==1)z=program_string(row->executable);
        else if(q->column==2)z=row->layer==SC_SHELL_LAYER_SYSTEM?"系统默认":row->layer==SC_SHELL_LAYER_ACCOUNT?"账户":"临时";''', '''        /* field保留模型字段编号；column是表头身份，不能借其数值解释行数据。 */
        if(q->field==0)z=program_string(row->label);
        else if(q->field==1)z=program_string(row->executable);
        else if(q->field==2)z=row->layer==SC_SHELL_LAYER_SYSTEM?"系统默认":row->layer==SC_SHELL_LAYER_ACCOUNT?"账户":"临时";''')],
    'user/settings2/install_view.inc': [('''        columns[i].column=i;
        columns[i].field=i;''', '''        /* 表头身份从1开始；实际来源/目标/字节/结果仍按原field取值。 */
        columns[i].column=i+1;
        columns[i].field=i;''')],
    'user/settings2/install_model.inc': [('''        if(q->column==2){r->value.type=SC_VALUE_U32;''', '''        /* CELL只按模型field解释内容，保持字节列的数值类型。 */
        if(q->field==2){r->value.type=SC_VALUE_U32;'''),
        ('''        else {const char *z=q->column==0?(row->kind==SC32_PATH_DIRECTORY?"<目录>":row->source):q->column==1?row->destination:install_state(row->state);''',
         '''        else {const char *z=q->field==0?(row->kind==SC32_PATH_DIRECTORY?"<目录>":row->source):q->field==1?row->destination:install_state(row->state);''')],
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    gui = json.loads((ROOT/'build/m10a2-p6-gui-run-02/p6-gui.json').read_text(encoding='utf-8'))
    pins, patches = {}, {'SETTINGS2':[], 'INSTALLER2':[]}
    for name, expected in EXPECTED.items():
        source = ROOT/name
        if sha(source) != expected:
            raise ValueError('当前Settings固定原像已改变：'+name)
        recorded = [v for k,v in gui['input_sha256'].items() if k.replace('\\','/').endswith('/'+name)]
        if not recorded or any(v!=expected for v in recorded):
            raise ValueError('Settings源不在真实GUI02输入中：'+name)
        body = source.read_bytes()
        newline = '\r\n' if b'\r\n' in body else '\n'
        before = body.decode('utf-8').replace('\r\n','\n')
        after = before
        for old,new in REPLACEMENTS[name]:
            if after.count(old)!=1:
                raise ValueError('固定局部原文不是唯一匹配：'+name)
            after=after.replace(old,new,1)
        target=HERE/'overlay'/name
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(after.replace('\n',newline).encode('utf-8'))
        pins[name]=dict(before_sha256=expected,result_sha256=sha(target),before_bytes=len(body),result_bytes=target.stat().st_size)
        label='INSTALLER2' if Path(name).name.startswith('install_') else 'SETTINGS2'
        patches[label].extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
    patch_path=HERE/'settings-column-field.patch'
    patch_path.write_text(''.join(patches['SETTINGS2']),encoding='utf-8',newline='\n')
    installer_patch=HERE/'installer-column-field.patch'
    installer_patch.write_text(''.join(patches['INSTALLER2']),encoding='utf-8',newline='\n')
    dependencies=('user/settings2/SETTINGS2.C','user/settings2/app_ui.inc','user/settings2/APP.H',
                  'user/scui/ui_provider.c','user/scui/ui_data.c','user/scui/ui_base.c','user/scui/ui_tree.c',
                  'user/scui/ui_layout.c','user/scui/ui_controls.c','user/scui/ui_menu.c',
                  'user/scshell/shell_client.c','kernel/session.c','user/SCUI.H')
    launch_rows=[]
    for case in gui['cases']:
        for match in re.finditer(r'^([0-9]+)\s+1000\s+1000\s+([0-9]+)\s+(SETTINGS2\.[A-Z]+)\s*$',case.get('output',''),re.M):
            launch_rows.append(dict(case=case['name'],pid=int(match[1]),uid=1000,gid=1000,state=int(match[2]),name=match[3],
                                    stdout_sha256=case.get('stdout_sha256')))
    selection=ROOT/'build/m10a2-p6-gui-run-02/p6-settings-selected-before-enter.png'
    facts=dict(status='SOURCE_PROVED_DEFECT_ACTUAL_LAUNCH_REPORTED_EXIT_STATUS_NOT_OBSERVED',modified=pins,
               patch_sha256=sha(patch_path),installer_patch_sha256=sha(installer_patch),
               refresh_labels=['SETTINGS2','INSTALLER2'],primary_refresh_labels=['SETTINGS2'],
               dependencies={name:sha(ROOT/name) for name in dependencies},
               actual_GUI02_source_pins_match=True,
               constructor_rejection=dict(stage='settings_create first settings_table(programs)',
                   supplied_first_column_id=0,SCUI_rejects_zero_column=True,raw_status=-301,
                   main_return_if_reached=301,needs_previous_calls_success=True),
               cell_mapping=dict(SCUI_DATA_CELL_uses='request.field',request_column_default=0,
                   fixed_column_ids=[1,2,3,4],unchanged_data_fields=[0,1,2,3]),
               menu_launch_confirmed_by_Root=True,actual_saved_process_rows=launch_rows,
               selected_menu_screenshot=dict(path=str(selection),sha256=sha(selection)),
               late_exit_code_observed=False,ordinary_Terminal_exit_code_observed_by_this_agent=False,
               installer_same_source_defect=True,installer_runtime_execution_claimed=False,
               sorting=dict(sortable_column_flags_enabled=False,provider_sort_enabled=False,
                            SCUI_sort_maps_column_ID_to_descriptor_field=True,new_sorting_feature_added=False),
               no_new_imports=True,manifests_not_modified=True,public_SCUI_contract_not_modified=True,
               original_main_unchanged=all(sha(ROOT/name)==expected for name,expected in EXPECTED.items()),
               main_entry=dict(path='user/settings2/SETTINGS2.C',before_sha256=sha(ROOT/'user/settings2/SETTINGS2.C'),
                               result_sha256=sha(ROOT/'user/settings2/SETTINGS2.C'),modified=False))
    (HERE/'PINS.json').write_text(json.dumps(facts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    ast.parse((HERE/'prepare.py').read_text(encoding='utf-8'))
    delivery={p.relative_to(HERE).as_posix():dict(sha256=sha(p),bytes=p.stat().st_size)
              for p in sorted(HERE.rglob('*')) if p.is_file() and p.name!='DELIVERY.json'}
    (HERE/'DELIVERY.json').write_text(json.dumps(dict(status='SOURCE_PATCH_ONLY_NOT_BUILT_NOT_VM_EXECUTED',files=delivery),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:facts[k] for k in ('status','modified','patch_sha256','constructor_rejection','original_main_unchanged')},ensure_ascii=False,indent=2))
    print('DELIVERY_SHA256',sha(HERE/'DELIVERY.json'))


if __name__=='__main__':
    main()
