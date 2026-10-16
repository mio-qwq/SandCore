"""只读规划统一安装；install仅供Root的QemuSession.data_prepare回调调用。"""
from pathlib import Path
from contextlib import ExitStack
import argparse
import hashlib
import inspect
import json
import os
import re
import struct
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import stage_m10a2_sdk as sdk
from mkfs_m9 import Record,fold,defaults,read_image
from mkfs_m10 import mapped,release_views,check_main
from mkcore import inspect_elf
from upgrade_m10a2_defaults import upgrade_records

NATIVE_LIBRARIES=('SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL','SCUI.SCL','SCSHELL.SCL','SCGFX.SCL','SCTERM.SCL')
APPLICATIONS=('FILES2','STUDIO2','DEBUG2','SETTINGS2','NOTES2','CANVAS2','LENS2','MONITOR2','CALC2','TERMINAL2','PALETTE2','SOUND2','SESSION2','INSTALLER2')
EDITABLE={'SYS/MENU.CFG','SYS/USER.CFG','SYS/ENV.CFG','SYS/DISPLAY.CFG','SYS/THEME.CFG','SYS/WALL.CFG'}
def digest(body):return hashlib.sha256(body).hexdigest()
def sha(path):return sdk.sha(path)
def checked(path,expected=None,size=None):
    path=Path(path).resolve(strict=True)
    if not path.is_relative_to(ROOT):raise ValueError('输入必须位于仓库：'+str(path))
    body=path.read_bytes()
    if (expected is not None and digest(body)!=expected) or (size is not None and len(body)!=size):raise ValueError('输入字节变化：'+str(path))
    return body
def target(name):
    name=name.lstrip('/')
    if not name or len(name.encode('utf-8'))>63 or any(p in ('','..','.') for p in name.split('/')) or '\\' in name:raise ValueError('非法客体目标：'+name)
    return name

class Candidate:
    def __init__(self,native_report,host_report,sdk_directory,core_build,source_image):
        # 原generation记录含Windows绝对artifact/stdout名；不在WSL偷偷改写来源路径。
        if os.name!='nt':raise ValueError('请用Windows Python读取真实Windows generation来源')
        self.native_path=Path(native_report).resolve(strict=True)
        self.host_path=Path(host_report).resolve(strict=True)
        self.sdk=Path(sdk_directory).resolve(strict=True);self.core=Path(core_build).resolve(strict=True)
        self.source=Path(source_image).resolve(strict=True)
        self.native=json.loads(checked(self.native_path))
        n=self.native
        if n.get('status')!='DECLARED_P6_NATIVE_GENERATION_PASS' or not n.get('current_inputs_unchanged') or not n.get('source_disk_unchanged') or n.get('changed_input_output_paths')!=[]:raise ValueError('完整真实native generation尚未通过')
        if set(n['native_libraries'])!=set(NATIVE_LIBRARIES):raise ValueError('需要全部八个普通native库')
        self.source_sha=n['source_data_sha256']
        if sha(self.source)!=self.source_sha:raise ValueError('原只读盘与generation来源不符')
        self.input_pins=sdk.Inputs()
        _,self.recorded,self.outputs=sdk.verified_build(self.host_path,self.input_pins)
        for name,value in n['source_input_sha256'].items():
            if self.recorded.get(name)!=value:raise ValueError('当前HOST/archive输入与native源不同：'+name)
            checked(ROOT/name,value)
        self.payload={};self.origins={};self.target_index={};self.protected_resource_skips=[]
        manifest=json.loads(checked(self.sdk/'MANIFEST.json'))
        if manifest.get('profile')!='native-preview' or not manifest.get('inputs_unchanged') or manifest.get('SDK_runtime_verified') is not False:raise ValueError('需要完整native-preview staging，不能用HOST preview')
        if manifest.get('build_report_sha256')!=sha(self.host_path):raise ValueError('SDK不是指定HOST/archive证明')
        expected=set(NATIVE_LIBRARIES)|{'SCSOUND.SCL'}
        if set(manifest['libraries'])!=expected:raise ValueError('SDK九库集合不符')
        if manifest['libraries']['SCSOUND.SCL']['origin'].get('source')!='USER_APPROVED_HOST_CODEC_EXCEPTION_NOT_GUEST_NATIVE':raise ValueError('SCSOUND例外来源错误')
        for name,row in manifest['files'].items():
            body=checked(self.sdk/name,row['sha256'],row['bytes'])
            if name.startswith('SYS/'):self.add(name,body,'SDK_STAGING')
        for name in NATIVE_LIBRARIES:
            row=n['native_libraries'][name]
            body=checked(row['path'],row['sha256'],row['bytes'])
            if self.payload['SYS/LIB/'+name]!=body:raise ValueError('SDK与将执行的native库不同：'+name)
            self.sidecars(row,'SYS/LIB/'+name)
        codecs=[(path,row) for path,row in self.outputs.items() if path.name=='SCSOUND.SCL']
        if len(codecs)!=1:raise ValueError('HOST证明须有唯一SCSOUND例外输出')
        codec_path,codec_row=codecs[0]
        if self.payload['SYS/LIB/SCSOUND.SCL']!=checked(codec_path,codec_row['sha256'],codec_row['bytes']):raise ValueError('SDK codec不是精确HOST例外输出')
        for name in APPLICATIONS:self.artifact(name+'.SCX','APPS/'+name+'.SCX')
        for name,destination in [('LOGIN2.SCX','SYS/CORE/LOGIN2.SCX'),('SCSHELL.SCX','SYS/SHELL/SCSHELL.SCX'),
                                 ('SCFILEWORK.SCX','SYS/SHELL/SCFILEWORK.SCX'),('SOUNDWORK.SCX','APPS/SOUNDWORK.SCX')]:
            self.artifact(name,destination)
        self.artifact('SCLD.SCX','SYS/BIN/SCLD.SCX')
        compiler=n['native_artifacts']['SCCC1.SCX']
        body=checked(compiler['path'],compiler['sha256'],compiler['bytes'])
        if self.payload['SYS/BIN/SCCC.SCX']!=body:raise ValueError('SDK compiler不是本轮实际生成并执行过的工具')
        # 旧容器bootstrap只确实产生map，不编造.sdbg。
        self.add('SYS/BIN/SCCC.SCX.map',checked(str(compiler['path'])+'.map'),'ACTUAL_NATIVE_COMPILER_MAP')
        for name in ('fs/bin/sh.scx','fs/bin/nano.scx','fs/SYS/CORE/IMAGE.SCX'):
            suffix=tuple(p.casefold() for p in Path(name).parts)
            matches=[(path,row) for path,row in self.outputs.items() if tuple(p.casefold() for p in path.parts[-len(suffix):])==suffix]
            if len(matches)!=1:raise ValueError('HOST报告缺唯一必要既有运行服务：'+name)
            path,row=matches[0]
            self.add(name[3:],checked(path,row['sha256'],row['bytes']),'EXACT_EXISTING_HOST_RUNTIME')
        for name,row in n['preflight'].get('bootstrap',{}).items():
            if row.get('origin')!='RESOURCE':continue
            key=fold(name)
            if key in EDITABLE or key.startswith(('HOME/','DESK/','USERS/','SYS/THEMES/')):
                self.protected_resource_skips.append(name);continue
            self.add(name,checked(row['path'],row['sha256'],row['bytes']),'PINNED_RESOURCE')
        # SCDB在生成时固定/SYS/SRC；复用准确源正文，不改副文件的路径身份。
        for obj in n['objects'].values():
            for row in obj['sources']:
                guest=row['guest']
                if guest.startswith('SYS/SRC/'):
                    source='user/'+guest[len('SYS/SRC/'):]
                    if n['source_input_sha256'].get(source)!=row['sha256']:raise ValueError('调试源不在native源闭包')
                    self.add(guest,checked(ROOT/source,row['sha256'],row['bytes']),'EXACT_SCDB_SOURCE_ALIAS')
                elif guest.startswith('SYS/INC/'):
                    key=self.target_index.get(fold(guest))
                    if key is None or digest(self.payload[key])!=row['sha256']:raise ValueError('SCDB公共头与SDK不同：'+guest)
        core=checked(self.core/'fs/SYS/CORE/CORE.SKM');check_main(core)
        self.add('SYS/CORE/CORE.SKM',core,'NATIVE_BOUND_CORE')
        self.core_binding=self.audit_core(core)
        if self.input_pins.unchanged():raise ValueError('读取安装计划期间HOST输入改变')

    def add(self,name,body,origin):
        name=target(name)
        if fold(name).startswith('SYS/M10BASE/'):raise ValueError('历史归档只能由本轮保留原件逻辑建立')
        prior=self.target_index.get(fold(name))
        if prior is not None:
            if self.payload[prior]!=body:raise ValueError('安装payload同名不同字节：'+name)
            return
        self.payload[name]=body;self.origins[name]=origin;self.target_index[fold(name)]=name

    def sidecars(self,row,destination):
        self.add(destination+'.map',checked(str(row['path'])+'.map',row['map_sha256']),'ACTUAL_NATIVE_MAP')
        self.add(destination+'.sdbg',checked(str(row['path'])+'.sdbg',row['debug']['sha256'],row['debug']['bytes']),'ACTUAL_NATIVE_SCDB')

    def artifact(self,name,destination):
        row=self.native['native_artifacts'][name]
        if not row.get('generated_by_actual_scld') or not row.get('guest_sccc_compiled'):raise ValueError('不是SCO/native-SCLD应用：'+name)
        body=checked(row['path'],row['sha256'],row['bytes'])
        if destination in self.payload and self.payload[destination]!=body:raise ValueError('SDK工具与实际native应用不同')
        self.add(destination,body,'ACTUAL_NATIVE_APPLICATION')
        self.sidecars(row,destination)

    def audit_core(self,core):
        payload=checked(self.core/'core.bin');symbols=inspect_elf(self.core/'core.elf')
        if core[128:]!=payload or len(payload)!=symbols['__core_image_end']-symbols['__core_base']:raise ValueError('CORE/ELF/flat载荷不符')
        base=symbols['__core_base']
        def read(address,count):
            at=address-base
            if at<0 or count>len(payload)-at:raise ValueError('CORE发行表超出真实载荷')
            return payload[at:at+count]
        for symbol,name in [('login_issued_sha256','SYS/CORE/LOGIN2.SCX'),('shell_host_issued_sha256','SYS/SHELL/SCSHELL.SCX')]:
            if read(symbols[symbol],32).hex()!=digest(self.payload[name]):raise ValueError('CORE仍绑定另一份发行程序：'+symbol)
        wanted={'SYS/LIB/'+name for name in ('SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL','SCUI.SCL')}|{'SYS/UI/UAX15_1.SCU'}
        found=set()
        for i in range(6):
            row=read(symbols['login_issued_resources']+i*40,40);pointer,size=struct.unpack_from('<2I',row)
            name=bytearray()
            for offset in range(64):
                byte=read(pointer+offset,1)[0]
                if byte==0:break
                name.append(byte)
            else:raise ValueError('发行路径未终止')
            name=name.decode('ascii')
            if name not in wanted or name in found or size!=len(self.payload[name]) or row[8:].hex()!=digest(self.payload[name]):raise ValueError('CORE中实际login资源表不匹配')
            found.add(name)
        if found!=wanted:raise ValueError('CORE资源发行集合不同')
        return dict(core_sha256=digest(core),payload_sha256=digest(payload),elf_sha256=sha(self.core/'core.elf'),
                    actual_compiled_program_hashes=True,actual_compiled_login_resources=sorted(found))

    def plan(self,image):
        extra=dict(self.payload);origins=dict(self.origins);metadata={};preserved_metadata=[]
        with ExitStack() as stack:
            records=read_image(mapped(stack,Path(image)),False);stack.callback(lambda:release_views(records))
            occupied=set(records)|{fold(name) for name in extra};archive_lookup={};replacements=[];archive_counter=0;archive_added=[]
            for key,row in sorted(records.items()):
                if re.fullmatch(r'SYS/M10BASE/[0-9A-F]{8}\.OLD',key) and row.payload is not None:
                    archive_lookup.setdefault((digest(row.payload),row.uid,row.gid,row.mode),row.name)
            def archive_record(prior):
                nonlocal archive_counter
                body=bytes(prior.payload);identity=(digest(body),prior.uid,prior.gid,prior.mode)
                archived=archive_lookup.get(identity);reused=archived is not None
                if not reused:
                    while True:
                        archive_counter+=1
                        if archive_counter>0xffffffff:raise ValueError('归档名字耗尽')
                        archived='SYS/M10BASE/%08X.OLD'%archive_counter
                        if fold(archived) not in occupied:break
                    occupied.add(fold(archived));archive_lookup[identity]=archived
                    extra[archived]=body;metadata[fold(archived)]=(prior.uid,prior.gid,prior.mode)
                    origins[archived]='PRESERVED_ORIGINAL_FILE';archive_added.append(dict(path=archived,bytes=len(body)))
                # 与正式mkfs_m10 INDEX相同七字段；generation记录原对象，
                # 不把准备阶段虚拟代数声称已安装对象的真实generation。
                replacements.append(dict(path=prior.name,archive=archived,sha256=identity[0],
                                         uid=prior.uid,gid=prior.gid,mode=prior.mode,generation=prior.generation))
                return reused
            for name,body in extra.items():
                old=records.get(fold(name))
                if old is not None and old.payload is None:raise ValueError('明确文件不能覆盖旧目录：'+name)
                attrs=(old.uid,old.gid,old.mode) if old else ((-1,-1,23) if fold(name).startswith('SYS/') else defaults(name))
                metadata[fold(name)]=attrs
                if old:preserved_metadata.append(name)
            # 正式发布器同样保留被替换对象；包括CORE/SH/nano/IMAGE和
            # 恰有变化的旧源码。只复制改变对象正文，不复制任何整盘。
            for name,body in list(extra.items()):
                prior=records.get(fold(name))
                if prior is not None and prior.payload!=body:archive_record(prior)
            shadow=dict(records)
            for name,body in extra.items():
                attrs=metadata[fold(name)]
                shadow[fold(name)]=Record(name,body,*attrs,1)
            counter=max((r.generation for r in records.values()),default=0)
            def generation():
                nonlocal counter
                counter+=1;return counter
            defaults_report=upgrade_records(shadow,generation,archive_record)
            for row in defaults_report['upgraded']:
                name=row['path'];record=shadow[fold(name)]
                extra[name]=record.payload;metadata[fold(name)]=(record.uid,record.gid,record.mode)
                origins[name]='EXACT_DEFAULT_UPGRADE'
                row['planning_generation_not_installed']=row.pop('new_generation')
            if replacements:
                prior_index=records.get('SYS/M10BASE/INDEX.JSON')
                if prior_index:
                    if prior_index.payload is None:raise ValueError('旧迁移INDEX是目录，拒绝覆盖')
                    archive_record(prior_index)
                index='SYS/M10BASE/INDEX.JSON'
                extra[index]=(json.dumps(replacements,ensure_ascii=False,indent=2)+'\n').encode('utf-8')
                metadata[index]=(-1,-1,prior_index.mode if prior_index else 23)
                origins[index]='MIGRATION_INDEX_WITH_PRIOR_INDEX_ARCHIVED'
            return extra,metadata,dict(defaults=defaults_report,old_metadata_preserved=preserved_metadata,
                replacements=replacements,archive_new_files=archive_added,archive_new_bytes=sum(row['bytes'] for row in archive_added),
                archive_reused_mappings=len(replacements)-len(archive_added),
                metadata={name:list(value) for name,value in sorted(metadata.items())},
                payload={name:dict(bytes=len(body),sha256=digest(body),origin=origins[name]) for name,body in extra.items()})

    def install(self,image):
        """只由已获授权的Root回调执行；只接受QemuSession创建的精确副本。"""
        image=Path(image).resolve(strict=True)
        if image==self.source or os.path.samefile(image,self.source) or not image.is_relative_to(ROOT/'build') or image.name!='sanddata.img':raise ValueError('拒绝修改原盘/硬链接/非本会话副本')
        if sha(image)!=self.source_sha:raise ValueError('会话副本不是固定只读来源')
        from verify_m10a2_scl import prepare
        if 'metadata' not in inspect.signature(prepare).parameters:raise RuntimeError('Root须在generation结束后先接prepare可选metadata参数；不回落旧UID覆盖')
        extra,metadata,plan=self.plan(image)
        result=prepare(image,self.core,extra,runtime_fixtures=False,metadata=metadata)
        # 旧helper内回读验证仍复用；这里再对调用者原计划核一遍，避免
        # 只加了metadata参数却漏用override时误把自洽结果当作正确身份。
        with ExitStack() as stack:
            records=read_image(mapped(stack,image),False);stack.callback(lambda:release_views(records))
            for name,body in extra.items():
                row=records[fold(name)]
                if row.payload is None or (row.uid,row.gid,row.mode)!=metadata[fold(name)] or digest(row.payload)!=digest(body):raise ValueError('安装回读与精确payload/metadata计划不符：'+name)
        if sha(self.source)!=self.source_sha:raise ValueError('原盘意外改变')
        # 保留旧helper的来源标签供审阅，不能让其固定HOST字样误标本轮native。
        legacy={key:result.pop(key) for key in ('host_bootstrap_libraries','guest_sccc_compiled') if key in result}
        return dict(result,installation='EXACT_P6_NATIVE_BUNDLE_GUEST_EXECUTION_PENDING',ordinary_native_libraries=8,
            host_codec_exception='SCSOUND.SCL',legacy_helper_origin_labels=legacy,metadata_plan=plan,
            core_binding=self.core_binding,native_report_sha256=sha(self.native_path),host_report_sha256=sha(self.host_path),
            source_disk_sha256=self.source_sha,source_disk_unchanged=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('native-report','host-report','sdk-directory','core-build','source-image'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();candidate=Candidate(**vars(args))
    _,_,plan=candidate.plan(candidate.source)
    print(json.dumps(dict(status='READ_ONLY_INSTALL_PLAN_NOT_EXECUTED',core_binding=candidate.core_binding,**plan),ensure_ascii=False,indent=2))
if __name__=='__main__':main()
