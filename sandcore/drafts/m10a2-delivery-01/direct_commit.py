"""遵照用户最新指令，正常Git整批提交/代理推送，再签署最终完整树快照。"""
from pathlib import Path
import os,sys,json,subprocess,base64,hashlib
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];REPO=ROOT.parent;E=ROOT/'evidence/m10a2-delivery-01';OUT=ROOT/'build/m10a2-delivery-01'
sys.path.insert(0,str(ROOT/'drafts/m10a2-session-return-01'))
from github_api import request,REPO as SLUG
def git(*a,env=None):return subprocess.check_output(['git',*a],cwd=REPO,env=env).decode().strip()
def save(p,v):p.write_bytes((json.dumps(v,indent=2)+'\n').encode())
def main():
    plan=json.loads((OUT/'commit-plan.json').read_bytes())
    assert git('rev-parse','HEAD')==plan['parent'] and git('write-tree')==plan['expected_tree']
    assert not git('diff','--name-only')
    # 前两次已实际签署的提交保留，不改写它们；取消其余分批请求。
    progress=json.loads((OUT/'signed-batch-progress.json').read_bytes());remote=request('repos/'+SLUG+'/branches/main')['commit']['sha']
    assert remote==progress[-1]['sha']
    git('-c','http.proxy=http://127.0.0.1:7890','fetch','origin','main')
    subprocess.run(['git','merge-base','--is-ancestor',plan['parent'],remote],cwd=REPO,check=True)
    git('update-ref','refs/heads/main',remote,plan['parent'])
    for name in ('direct_commit.py','record_publication.py','resume_commit.py'):
        git('add','--',str(Path(__file__).with_name(name)))
    subprocess.run(['git','-c','core.whitespace=cr-at-eol','diff','--cached','--check'],cwd=REPO,check=True,capture_output=True)
    tree=git('write-tree')
    # 本机没有用户GPG私钥。传输提交保持真实未签名状态；随后由GitHub对
    # 完整最终树和父链做一次Verified签名，不声称使用了用户私钥。
    git('-c','commit.gpgsign=false','commit','-m','Complete M10a2 native SDK delivery and preserve full GUI evidence')
    transfer=git('rev-parse','HEAD');assert git('rev-parse','HEAD^{tree}')==tree
    env=os.environ.copy();env.update(GIT_TERMINAL_PROMPT='0',GCM_INTERACTIVE='never',GIT_TRACE='0',GIT_CURL_VERBOSE='0')
    helper='!python -B '+str(Path(__file__).with_name('git_credential.py')).replace('\\','/')
    push=subprocess.run(['git','-c','http.proxy=http://127.0.0.1:7890','-c','credential.helper=',
        '-c','credential.helper='+helper,'push','origin','main'],cwd=REPO,env=env,capture_output=True)
    if push.returncode:raise RuntimeError(push.stderr.decode(errors='replace')[-1500:])
    assert request('repos/'+SLUG+'/branches/main')['commit']['sha']==transfer
    audit=json.loads((E/'delivery.json').read_bytes())
    name='sandcore/evidence/m10a2-delivery-01/source-snapshot.json'
    snapshot={'scope':'Entire source tree and parent chain, GitHub signature protects this complete snapshot',
        'source_tree_sha':tree,'transport_commit':transfer,'transport_commit_locally_signed':False,
        'core_sha256':audit['core_binding']['core_sha256'],'assets':audit['assets'],
        'prior_history_preserved':True,'additional_split_uploads_cancelled_at_user_request':True}
    body=(json.dumps(snapshot,indent=2)+'\n').encode();(REPO/name).write_bytes(body);git('add','--',name);wanted=git('write-tree')
    result=request('graphql',{'query':'mutation($input: CreateCommitOnBranchInput!) { createCommitOnBranch(input:$input) { commit { oid url signature { isValid state } } } }',
        'variables':{'input':{'branch':{'repositoryNameWithOwner':SLUG,'branchName':'main'},'expectedHeadOid':transfer,
        'message':{'headline':'Sign the complete checked M10a2 source and delivery snapshot'},
        'fileChanges':{'additions':[{'path':name,'contents':base64.b64encode(body).decode()}],'deletions':[]}}}})
    assert not result.get('errors'),result.get('errors')
    c=result['data']['createCommitOnBranch']['commit'];actual=request('repos/'+SLUG+'/git/commits/'+c['oid'])
    assert actual['tree']['sha']==wanted and actual['parents'][0]['sha']==transfer
    assert actual['verification']['verified'] and actual['verification']['reason']=='valid'
    assert c['signature']['isValid'] and c['signature']['state']=='VALID'
    git('-c','http.proxy=http://127.0.0.1:7890','fetch','origin','main')
    assert git('rev-parse','refs/remotes/origin/main')==c['oid'] and not git('diff','--name-only')
    git('update-ref','refs/heads/main',c['oid'],transfer);assert not git('diff','--cached','--name-only')
    receipt={'status':'GITHUB_SIGNED_VERIFIED_COMMIT_ON_MAIN','sha':c['oid'],'url':c['url'],
        'parent':transfer,'tree':wanted,'github_verification':{'verified':True,'reason':'valid'},
        'graphql_signature':c['signature'],'prior_history_preserved':True,'force_push':False,
        'local_main_equals_remote':True,'ordinary_git_transport_commit':transfer,'ordinary_git_transport_signed':False,
        'entire_final_tree_and_parent_chain_github_signed':True,'earlier_verified_batches':progress,
        'further_split_uploads_cancelled':True}
    save(E/'commit.json',receipt);print(json.dumps(receipt))
if __name__=='__main__':main()
