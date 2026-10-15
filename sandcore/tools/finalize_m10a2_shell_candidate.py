#!/usr/bin/env python3
"""Complete the reviewed P5 candidate wiring, without touching active sources."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    args=parser.parse_args()
    stage=args.stage.resolve()
    if not stage.is_relative_to((ROOT/'build').resolve()):parser.error('stage must be inside build/')
    report=stage/'integration-review.json'
    record=json.loads(report.read_text(encoding='utf-8'))
    if (stage/'finalization-review.json').exists():parser.error('candidate already finalized')
    for name,expected in record['candidate_sha256'].items():
        if digest(stage/name)!=expected:raise ValueError('Candidate changed before review: '+name)
    changes={}
    def write(name,text):
        path=stage/name
        before=digest(path) if path.exists() else None
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(text.encode('utf-8'))
        changes[name]=dict(before=before,after=digest(path))
    def patch(name,old,new):
        path=stage/name;text=path.read_bytes().decode('utf-8')
        if text.count(old)!=1:raise ValueError('Non-unique candidate anchor: '+name)
        write(name,text.replace(old,new,1))
    host=(stage/'user/SC32HOST.H').read_text(encoding='utf-8')
    exports=sorted(set(re.findall(r'\bScStatus\s+(Sc\w+)\s*\(',host)))
    if len(exports)!=11:raise ValueError('Host contract must have eleven reviewed entry points')
    name='user/sc32/SC32API.json';manifest=json.loads((stage/name).read_text(encoding='utf-8'))
    names={x['name'] for x in manifest['exports']}
    if len(names)!=142 or names.intersection(exports):raise ValueError('Unexpected pre-host SC32 manifest')
    manifest['exports'].extend(dict(name=symbol,kind='function') for symbol in exports)
    manifest['abi_major'],manifest['abi_minor'],manifest['library_version']=1,3,4
    write(name,json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    patch('kernel/sc32_core.c','#include "fs_lease.h"',
          '#include "fs_lease.h"\n#include "../user/SC32HOST.H"')
    patch('kernel/sc32_core.c',
          '    if(fs_lease_available())result.capabilities_hi|=SC32_CAP_LEASES_HI;',
          '    if(fs_lease_available())result.capabilities_hi|=SC32_CAP_LEASES_HI;\n'
          '    /* 能力位表示已接入的角色服务合同，不表示当前用户桌面已经ACTIVE。 */\n'
          '    result.capabilities_hi|=SC32_CAP_HOST_HI;')
    for name in ['user/scui/SCUI.json','user/shellworker/SCFILEWORK.json','user/scshell/desktop/SCSHELL.json']:
        manifest=json.loads((stage/name).read_text(encoding='utf-8'))
        found=False
        for dep in manifest['dependencies']:
            if dep['name']=='SC32API.SCL':
                dep['abi_major']=1;dep['min_minor']=3;dep['min_version']=4;found=True
                dep.pop('abi_minor',None)
            if dep['name']=='SCUI.SCL':
                dep['abi_major']=1;dep['min_minor']=1;dep['min_version']=2
                dep.pop('abi_minor',None)
        if not found:raise ValueError('Missing SC32 dependency: '+name)
        write(name,json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    text=(stage/'m10a2.mk').read_bytes().decode('utf-8')
    if 'm10a2-shell-artifacts:' in text:raise ValueError('Duplicate shell targets')
    text+='''
# Phase5必须统一生成库、worker和桌面，再生成同字节发行摘要，最后链接CORE。
# 这些明确目标不触发封存游戏/影片或全应用构建，也不修改用户原数据盘。
M10A2_SHELL_DIR ?= build/m10a2-shell-bootstrap
.PHONY: m10a2-shell-bootstrap m10a2-shell-artifacts m10a2-shell-install
m10a2-shell-bootstrap:
	$(MAKE) BUILD=$(M10A2_SHELL_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_SHELL_DIR)/sandcore.img $(M10A2_SHELL_DIR)/fs/SYS/CORE/CORE.SKM m10a2-shell-artifacts m10a2-ui-resources m10a2-shell-install
$(BUILD)/scshell-library.o: user/scshell/SCSHELL.C user/SCSHELL.H $(wildcard user/scshell/*.c user/scshell/*.H) user/SC32HOST.H user/SC32LEASE.H user/SCUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCSHELL.SCL: $(BUILD)/scshell-library.o user/scshell/SCSHELL.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/scshell/SCSHELL.json
$(BUILD)/scfilework.o: user/shellworker/SCFILEWORK.C $(wildcard user/shellworker/*.c user/shellworker/*.H) user/scshell/SCSHELLWIRE.H user/SCSHELL.H user/SC32LEASE.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCFILEWORK.SCX: $(BUILD)/scfilework.o user/shellworker/SCFILEWORK.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/shellworker/SCFILEWORK.json
$(BUILD)/scshell-desktop.o: user/scshell/desktop/SCSHELL.C $(wildcard user/scshell/desktop/*.inc user/scshell/desktop/*.H) user/SCSHELL.H user/SC32HOST.H user/SCUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCSHELL.SCX: $(BUILD)/scshell-desktop.o user/scshell/desktop/SCSHELL.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/scshell/desktop/SCSHELL.json
m10a2-shell-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/SC32API.SCL $(BUILD)/SCCLIB.SCL $(BUILD)/SCLIB.SCL $(BUILD)/SCUI.SCL $(BUILD)/SCSHELL.SCL $(BUILD)/SCFILEWORK.SCX $(BUILD)/SCSHELL.SCX
M10A2_SHELL_LIB_NAMES := SCLDR SC32API SCCLIB SCLIB SCUI SCSHELL
$(addprefix $(BUILD)/fs/SYS/LIB/,$(addsuffix .SCL,$(M10A2_SHELL_LIB_NAMES))): $(BUILD)/fs/SYS/LIB/%.SCL: $(BUILD)/%.SCL
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/SHELL/SCSHELL.SCX: $(SHELL_HOST_IMAGE)
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/SHELL/SCFILEWORK.SCX: $(BUILD)/SCFILEWORK.SCX
	mkdir -p $(dir $@)
	cp $< $@
m10a2-shell-install: $(addprefix $(BUILD)/fs/SYS/LIB/,$(addsuffix .SCL,$(M10A2_SHELL_LIB_NAMES))) $(BUILD)/fs/SYS/SHELL/SCSHELL.SCX $(BUILD)/fs/SYS/SHELL/SCFILEWORK.SCX
'''
    write('m10a2.mk',text)
    active={name:digest(ROOT/name) for name in record['baseline_sha256']
            if digest(ROOT/name)!=record['baseline_sha256'][name]}
    if active:raise ValueError('Active staging baseline changed: '+repr(active))
    final=dict(status='COMPLETE_WIRING_CANDIDATE_NOT_BUILT_NOT_RUN',
        source_review_sha256=digest(report),changes=changes,active_sources_modified=False,
        public_sc32_exports=153,public_scui_exports=162,
        remaining='Compile real aggregate sources, validate undefined imports, guest generation, trusted image identity and behavior.')
    (stage/'finalization-review.json').write_text(json.dumps(final,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=final['status'],changed=len(changes),stage=str(stage)),ensure_ascii=False))

if __name__=='__main__':main()
