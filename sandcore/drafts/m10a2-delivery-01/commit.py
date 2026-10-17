"""交付检查后：保留本地历史，再让GitHub创建实际Verified的最终提交。"""
from pathlib import Path
import os,sys,json,base64,subprocess,hashlib
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];REPO=ROOT.parent
sys.path.insert(0,str(ROOT/'drafts/m10a2-session-return-01'))
from github_api import request,REPO as SLUG
EVIDENCE=ROOT/'evidence/m10a2-delivery-01'
def git(*args,**kw):return subprocess.check_output(['git',*args],cwd=REPO,**kw).decode().strip()
def save(name,v):(EVIDENCE/name).write_bytes((json.dumps(v,indent=2)+'\n').encode())
def main():
    audit=json.loads((EVIDENCE/'delivery.json').read_bytes())
    assert audit['status']=='FINAL_DELIVERY_FILES_AND_SOURCE_PROVENANCE_CHECKED'
    assert git('branch','--show-current')=='main'
    staged=git('diff','--cached','--name-only').splitlines()
    owned_dirs=('drafts/m10a2-session-return-01','drafts/m10a2-p6-followup-refresh-v10',
        'drafts/m10a2-p6-final-12-receipt','drafts/m10a2-p6-gui-13','drafts/m10a2-delivery-01',
        'evidence/m10a2-gui12','evidence/m10a2-session-return','evidence/m10a2-delivery-01')
    # 只加入本轮明确拥有的路径；旧代理目录和用户.zcodeignore保持未跟踪。
    tracked=sorted(set(git('diff','--name-only').splitlines()+staged))
    expected={'README.md','README_EN.md','m10a2handoff.md','sandcore/README.md',
        'sandcore/kernel/auth.c','sandcore/user/scshell/desktop/desktop_actions.inc'}
    allowed=lambda n:n in expected or n.startswith('sandcore/docs/') or any(n.startswith('sandcore/'+d+'/') for d in owned_dirs) or n=='sandcore/evidence/m10a2-fullscreen-entry-01/user-feedback.json'
    assert all(allowed(n) for n in tracked)
    files=tracked+['sandcore/docs/M10A2-ACCEPTANCE.md','sandcore/docs/M10A2-RELEASE.md',
        'sandcore/evidence/m10a2-fullscreen-entry-01/user-feedback.json']
    for folder in owned_dirs:
        files += [p.relative_to(REPO).as_posix() for p in (ROOT/folder).rglob('*')
            if p.is_file() and '__pycache__' not in p.parts and p.name not in ('relay.json',)]
    # commit.json会在实际签名之后才产生，不自引用。
    files=sorted(set(files));git('add','--',*files)
    # 原始Windows报告保持CRLF字节，cr-at-eol只解释行尾，不改报告或全局配置。
    subprocess.run(['git','-c','core.whitespace=cr-at-eol','diff','--cached','--check'],cwd=REPO,check=True,capture_output=True)
    changes=git('diff','--cached','--name-only','--diff-filter=ACMRT').splitlines()
    deleted=git('diff','--cached','--name-only','--diff-filter=D').splitlines()
    assert not deleted
    parent=git('rev-parse','HEAD');tree=git('write-tree')
    remote=request('repos/'+SLUG+'/branches/main')['commit']['sha']
    subprocess.run(['git','merge-base','--is-ancestor',remote,parent],cwd=REPO,check=True)
    prepared={'status':'CHECKED_INDEX_READY_FOR_SIGNED_COMMIT','parent':parent,'expected_tree':tree,
        'remote_before':remote,'files':changes,'not_staged_user_paths_preserved':True}
    # 此准备记录留build，避免在固定好tree之后又改变被提交文件。
    out=ROOT/'build/m10a2-delivery-01';(out/'commit-plan.json').write_bytes((json.dumps(prepared,indent=2)+'\n').encode())
    env=os.environ.copy();env.update(GIT_TERMINAL_PROMPT='0',GCM_INTERACTIVE='never',GIT_TRACE='0',GIT_CURL_VERBOSE='0')
    helper='!python -B '+str(Path(__file__).with_name('git_credential.py')).replace('\\','/')
    # 先非强制推送原有23个提交；不丢弃或改写旧开发历史。
    push=subprocess.run(['git','-c','http.proxy=http://127.0.0.1:7890','-c','credential.helper=',
        '-c','credential.helper='+helper,'push','origin','main'],cwd=REPO,env=env,capture_output=True)
    if push.returncode:raise RuntimeError('History push failed: '+push.stderr.decode(errors='replace')[-1600:])
    assert request('repos/'+SLUG+'/branches/main')['commit']['sha']==parent
    additions=[]
    for name in changes:
        body=subprocess.check_output(['git','show',':'+name],cwd=REPO)
        additions.append({'path':name,'contents':base64.b64encode(body).decode('ascii')})
    mutation='''mutation($input: CreateCommitOnBranchInput!) { createCommitOnBranch(input:$input) { commit { oid url signature { isValid state } } } }'''
    # 保留全部原始字节，仅拆小GraphQL请求，避免整批base64传输超时。
    groups=[];group=[];size=0
    for row in additions:
        length=len(row['contents'])
        if group and size+length>16000000:groups.append(group);group=[];size=0
        group.append(row);size+=length
    if group:groups.append(group)
    head=parent;verified=[]
    for number,group in enumerate(groups,1):
        result=request('graphql',{'query':mutation,'variables':{'input':{
            'branch':{'repositoryNameWithOwner':SLUG,'branchName':'main'},'expectedHeadOid':head,
            'message':{'headline':('Complete M10a2 delivery and local sign-in fixes' if number==1 else 'Preserve M10a2 raw execution evidence')+' ('+str(number)+'/'+str(len(groups))+')',
                'body':'Checked native SDK documentation and scoped GUI evidence. The original login resource accounting limitation remains disclosed.'},
            'fileChanges':{'additions':group,'deletions':[]}}}})
        if result.get('errors'):raise RuntimeError('GraphQL commit rejected: '+json.dumps(result['errors']))
        commit=result['data']['createCommitOnBranch']['commit'];oid=commit['oid']
        actual=request('repos/'+SLUG+'/git/commits/'+oid)
        assert [p['sha'] for p in actual['parents']]==[head]
        verify=actual['verification'];assert verify['verified'] is True and verify['reason']=='valid'
        assert commit['signature']['isValid'] and commit['signature']['state']=='VALID'
        head=oid;verified.append({'sha':oid,'files':len(group),'verified':True})
        (out/'signed-batch-progress.json').write_bytes((json.dumps(verified,indent=2)+'\n').encode())
        print('VERIFIED_BATCH '+str(number)+'/'+str(len(groups))+' '+oid,flush=True)
    assert actual['tree']['sha']==tree
    # 必须先取得同一个提交对象，再推进本地引用；不hard reset用户工作树。
    fetch=subprocess.run(['git','-c','http.proxy=http://127.0.0.1:7890','fetch','origin','main'],cwd=REPO,env=env,capture_output=True)
    if fetch.returncode:raise RuntimeError('Verified remote commit exists but fetch failed')
    assert git('rev-parse','refs/remotes/origin/main')==oid
    assert git('write-tree')==tree and not git('diff','--name-only')
    git('update-ref','refs/heads/main',oid,parent)
    assert git('rev-parse','HEAD^{tree}')==tree and not git('diff','--cached','--name-only')
    receipt={'status':'GITHUB_SIGNED_VERIFIED_COMMIT_ON_MAIN','sha':oid,'url':commit['url'],
        'parent':parent,'tree':tree,'github_verification':{'verified':verify['verified'],'reason':verify['reason']},
        'graphql_signature':commit['signature'],'prior_history_preserved':True,'force_push':False,
        'local_main_equals_remote':True,'committed_files':len(changes),'verified_batches':verified}
    save('commit.json',receipt);print(json.dumps(receipt))
if __name__=='__main__':main()
