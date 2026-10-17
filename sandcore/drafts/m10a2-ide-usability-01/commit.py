"""整批提交已审阅索引；由GitHub签署，保留本地代理文件和全部旧提交。"""
from pathlib import Path
import base64,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];REPO=ROOT.parent
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/'drafts/m10a2-session-return-01'))
from github_api import request,REPO as SLUG
OUT=ROOT/'build/m10a2-product-delivery-01'
def git(*a):return subprocess.check_output(['git',*a],cwd=REPO)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    assert git('branch','--show-current').decode().strip()=='main'
    parent=git('rev-parse','HEAD').decode().strip()
    assert request('repos/'+SLUG+'/branches/main')['commit']['sha']==parent
    tree=git('write-tree').decode().strip()
    local=('AGENTS.md','M10a2RULE.MD','m10a2handoff.md','HANDOFF.md','M8_GOAL.md','AUDIT-NOTES.md','DEVELOPMENT-REFLECTION.md','USERSPACE-PRODUCT-PROMPT.md')
    preserved={n:sha(REPO/n) for n in local}
    tracked=set(git('ls-files','-z').decode().split('\0'))
    assert not tracked.intersection(local)
    additions=[];deletions=[]
    for line in git('diff','--cached','--name-status','--no-renames').decode().splitlines():
        status,name=line.split('\t')
        if status=='D':deletions.append(dict(path=name))
        else:
            assert status in ('A','M')
            body=git('show',':'+name)
            additions.append(dict(path=name,contents=base64.b64encode(body).decode('ascii')))
    assert additions and len(deletions)==6
    payload=dict(query='mutation($input: CreateCommitOnBranchInput!) { createCommitOnBranch(input:$input) { commit { oid url signature { isValid state } } } }',variables=dict(input=dict(
        branch=dict(repositoryNameWithOwner=SLUG,branchName='main'),expectedHeadOid=parent,
        message=dict(headline='Fix Studio workflows, legacy source support and file navigation',body='Restore SCAPI.H source compatibility; make default HelloWorld creation, build and run usable. Fix shared directory selection, scroll bounds and modern editor routing. Keep agent instructions local without deleting their files.'),
        fileChanges=dict(additions=additions,deletions=deletions))))
    size=len(json.dumps(payload).encode())
    assert size<16000000,'本批过大，停止，不自行拆分提交'
    (OUT/'commit-plan.json').write_text(json.dumps(dict(parent=parent,tree=tree,files=len(additions)+len(deletions),request_bytes=size,local_agent_files=preserved),indent=2)+'\n',encoding='utf-8')
    result=request('graphql',payload)
    assert not result.get('errors'),json.dumps(result.get('errors'))
    c=result['data']['createCommitOnBranch']['commit'];oid=c['oid']
    actual=request('repos/'+SLUG+'/git/commits/'+oid)
    assert actual['verification']['verified'] and actual['verification']['reason']=='valid'
    assert actual['tree']['sha']==tree and [r['sha'] for r in actual['parents']]==[parent]
    subprocess.run(['git','-c','http.proxy=http://127.0.0.1:7890','fetch','origin','main'],cwd=REPO,check=True)
    assert git('rev-parse',oid+'^{tree}').decode().strip()==tree
    subprocess.run(['git','update-ref','refs/heads/main',oid,parent],cwd=REPO,check=True)
    assert all(sha(REPO/n)==h for n,h in preserved.items())
    record=dict(status='GITHUB_SIGNED_VERIFIED_COMMIT_ON_MAIN',sha=oid,url=c['url'],tree=tree,parent=parent,
        files=len(additions)+len(deletions),request_bytes=size,verification=actual['verification']['reason'],
        one_batch=True,local_agent_files_preserved=True,prior_history_preserved=True,force_push=False)
    (OUT/'commit.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(record))
if __name__=='__main__':main()
