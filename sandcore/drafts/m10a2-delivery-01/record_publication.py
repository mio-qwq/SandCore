"""发布成功后落地实际URL/摘要；用第二次Verified提交保存凭证，不重打包。"""
from pathlib import Path
import sys,json,subprocess,base64,os
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];REPO=ROOT.parent;E=ROOT/'evidence/m10a2-delivery-01'
sys.path.insert(0,str(ROOT/'drafts/m10a2-session-return-01'))
from github_api import request,REPO as SLUG
def read(p):return p.read_text(encoding='utf-8-sig')
def write(p,s):p.write_bytes((s.rstrip()+'\n').encode('utf-8'))
def git(*args):return subprocess.check_output(['git',*args],cwd=REPO).decode().strip()
def main():
    c=json.loads((E/'commit.json').read_bytes());p=json.loads((E/'publish.json').read_bytes())
    assert p['status']=='RELEASE_PUBLISHED_ASSETS_HASH_VERIFIED'
    assert p['commit']==c['sha'] and c['github_verification']=={'verified':True,'reason':'valid'}
    url=p['url'];source=c['url'];sha=c['sha'];files=[]
    for filename,old,new in (
        ('README.md','文档/SDK/附件检查后按用户授权签名提交并发布；实际发布状态以发布凭证为准。','[M10a2已正式发布]('+url+')，运行包/独立SDK附件摘要已核对；[源提交]('+source+')经GitHub实际Verified验证。'),
        ('README_EN.md','Documentation, SDK and package checks precede the authorized signed commit and Release; actual publication is recorded separately.','[M10a2 is published]('+url+'); runtime and SDK asset digests were checked. The [source commit]('+source+') has an actual valid GitHub signature.'),
        ('sandcore/README.md','检查后按最新授权签名提交和Release。','[M10a2已正式发布]('+url+')，实际Verified签名及资产摘要见[发布凭证](docs/M10A2-PUBLISHED.md)。')):
        f=REPO/filename;s=read(f);assert old in s;s=s.replace(old,new,1)
        s=s.replace('检查后签名提交/Release。','已签名提交并正式发布；附件摘要已核对。').replace('signing and Release follow delivery checks.','published with verified source signatures and checked asset digests.')
        if filename=='README.md':
            s=s.replace('[M10a2运行包/SDK](sandcore/docs/M10A2-RELEASE.md) | 交付内容与启动方法；实际发布以GitHub结果为准','[M10a2运行包/SDK]('+url+') | 正式下载；[发布说明及校验凭证](sandcore/docs/M10A2-PUBLISHED.md)')
        if filename=='README_EN.md':
            s=s.replace('[M10a2 runtime and SDK](sandcore/docs/M10A2-RELEASE.md) | Package contents and launch instructions; publication is recorded separately','[M10a2 runtime and SDK]('+url+') | Published downloads; [release receipt](sandcore/docs/M10A2-PUBLISHED.md)')
        write(f,s);files.append(filename)
    status='''# M10a2 实际发布记录

2026-10-17。SandCore M10a2 — The Application Platform Era已发布。以下来自实际GitHub返回结果；发布前的staging/交付报告保持其原始时态。

- [GitHub Release]('''+url+''')，tag M10a2，draft=false，prerelease=false。
- [发行源提交]('''+source+''')：`'''+sha+'''`。最终完整树快照的GitHub签名验证verified=true、reason=valid，GraphQL签名VALID。主体使用正常Git通过代理提交/推送，传输提交自身未签名的事实记录在commit.json；最终快照签名覆盖完整树与父链。此前开发历史完整保留，无强推。
- 完整交付合同与GUI13原资源FAIL/+8页边界见[M10A2-ACCEPTANCE.md](M10A2-ACCEPTANCE.md)，未改称零差PASS。
- SDK是同一native11/HOST21/final12的499项载荷、8普通原生库和唯一SCSOUND宿主codec例外；运行包严格四文件，用户test盘不在附件中。

| 附件 | 字节 | SHA256 |
|---|---:|---|
'''
    for name,a in p['assets'].items():status+='| ['+name+']('+a['url']+') | '+str(a['bytes'])+' | `'+a['digest'].split(':',1)[1]+'` |\n'
    status+='\nGitHub上传后的资产digest/size已与本机逐项相等；原始记录见[evidence/m10a2-delivery-01](../evidence/m10a2-delivery-01/README.md)中的commit.json、publish.json、delivery.json及SHA256SUMS。未因发布新增编译/QEMU矩阵。完整M10及基础内核冻结未在本版宣称完成。\n\n修订：2026-10-17，落地实际签名与Release结果。\n'
    write(ROOT/'docs/M10A2-PUBLISHED.md',status);files.append('sandcore/docs/M10A2-PUBLISHED.md')
    old='当前工作为统一文档、同字节最终SDK restage、交付包核验，然后按用户授权签名提交/Release；不再扩展QEMU矩阵。当前正式发布结果以实际publish凭证为准，不预写成功。'
    new='最终SDK/四文件包检查已经通过，Verified源提交和正式Release均已完成；实际URL、资产摘要与签名结果见[M10A2-PUBLISHED.md](M10A2-PUBLISHED.md)。不再扩展QEMU矩阵。'
    for name in ('M10A2-LIVE-GUI.md','M10A2-CURRENT-GOAL.md','M10A2-IMPLEMENTATION.md','M10A2-P6-IMPLEMENTATION.md'):
        f=ROOT/'docs'/name;s=read(f);assert old in s;write(f,s.replace(old,new,1));files.append('sandcore/docs/'+name)
    f=REPO/'m10a2handoff.md';s=read(f);head,rest=s.split('\n',1)
    write(f,head+'\n\n## 最终发布已完成\n\n2026-10-17：'+url+' 已公开，发行源提交 '+sha+' 实际Verified。SDK/运行包/摘要已核对，完整凭证见sandcore/docs/M10A2-PUBLISHED.md及evidence/m10a2-delivery-01。下方发布前“剩余动作”已完成，历史记录保留；没有后台QEMU、构建或子代理。GUI13原FAIL/+8页限制没有重分类。\n'+rest);files.append('m10a2handoff.md')
    f=E/'README.md';write(f,read(f)+'\n\n实际Release已经公开：'+url+'。commit.json保存真实Verified源提交和各批次，publish.json保存公开状态、tag绑定和GitHub资产摘要；本目录的delivery.json保持发布前检查时态。');files.append(f.relative_to(REPO).as_posix())
    f=REPO/'releases/README.md'
    if f.is_file():
        s=read(f);head,rest=s.split('\n',1);write(f,head+'\n\n最新：[SandCore M10a2 — The Application Platform Era]('+url+')，提供四文件运行包、独立SDK及SHA256SUMS。[实际发布凭证](../sandcore/docs/M10A2-PUBLISHED.md)。以下旧版本原包保持。\n'+rest);files.append('releases/README.md')
    files+=['sandcore/evidence/m10a2-delivery-01/commit.json','sandcore/evidence/m10a2-delivery-01/publish.json',Path(__file__).relative_to(REPO).as_posix(),
        Path(__file__).with_name('resume_commit.py').relative_to(REPO).as_posix()]
    assert not git('diff','--cached','--name-only')
    git('add','--',*files);subprocess.run(['git','-c','core.whitespace=cr-at-eol','diff','--cached','--check'],cwd=REPO,check=True,capture_output=True)
    parent=git('rev-parse','HEAD');assert parent==sha and request('repos/'+SLUG+'/branches/main')['commit']['sha']==parent
    tree=git('write-tree');additions=[]
    for name in git('diff','--cached','--name-only').splitlines():
        additions.append({'path':name,'contents':base64.b64encode(subprocess.check_output(['git','show',':'+name],cwd=REPO)).decode()})
    result=request('graphql',{'query':'mutation($input: CreateCommitOnBranchInput!) { createCommitOnBranch(input:$input) { commit { oid url signature { isValid state } } } }',
        'variables':{'input':{'branch':{'repositoryNameWithOwner':SLUG,'branchName':'main'},'expectedHeadOid':parent,
        'message':{'headline':'Record verified M10a2 Release and matching delivery assets'},'fileChanges':{'additions':additions,'deletions':[]}}}})
    assert not result.get('errors'),result.get('errors')
    final=result['data']['createCommitOnBranch']['commit'];actual=request('repos/'+SLUG+'/git/commits/'+final['oid'])
    assert actual['tree']['sha']==tree and [r['sha'] for r in actual['parents']]==[parent]
    assert actual['verification']['verified'] and actual['verification']['reason']=='valid'
    assert final['signature']['isValid'] and final['signature']['state']=='VALID'
    git('-c','http.proxy=http://127.0.0.1:7890','fetch','origin','main')
    assert git('rev-parse','refs/remotes/origin/main')==final['oid'] and not git('diff','--name-only')
    git('update-ref','refs/heads/main',final['oid'],parent)
    assert not git('diff','--cached','--name-only')
    receipt={'status':'PUBLICATION_RECEIPTS_SIGNED_AND_PUSHED','sha':final['oid'],'url':final['url'],'verified':True,'release':url,
        'release_commit':parent,'local_main_equals_remote':True,'force_push':False}
    (ROOT/'build/m10a2-delivery-01/publication-commit.json').write_bytes((json.dumps(receipt,indent=2)+'\n').encode())
    print(json.dumps(receipt))
if __name__=='__main__':main()
