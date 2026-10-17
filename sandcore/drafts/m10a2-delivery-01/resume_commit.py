"""只续传已固定index的未完成签名批次；每个中间tree/parent均精确核验。"""
from pathlib import Path
import os,sys,json,base64,subprocess,time
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];REPO=ROOT.parent;OUT=ROOT/'build/m10a2-delivery-01';E=ROOT/'evidence/m10a2-delivery-01'
sys.path.insert(0,str(ROOT/'drafts/m10a2-session-return-01'))
from github_api import request,REPO as SLUG
def git(*a,env=None):return subprocess.check_output(['git',*a],cwd=REPO,env=env).decode().strip()
def save(p,r):p.write_bytes((json.dumps(r,indent=2)+'\n').encode())
def main():
    plan=json.loads((OUT/'commit-plan.json').read_bytes());parent=plan['parent']
    assert git('rev-parse','HEAD')==parent and git('write-tree')==plan['expected_tree'] and not git('diff','--name-only')
    additions=[]
    for name in git('diff','--cached','--name-only').splitlines():
        body=subprocess.check_output(['git','show',':'+name],cwd=REPO)
        additions.append({'path':name,'contents':base64.b64encode(body).decode()})
    groups=[];group=[];size=0
    for row in additions:
        length=len(row['contents'])
        if group and size+length>16000000:groups.append(group);group=[];size=0
        group.append(row);size+=length
    if group:groups.append(group)
    index=OUT/'resume-index';assert not index.exists()
    env=os.environ.copy();env['GIT_INDEX_FILE']=str(index)
    git('read-tree',parent,env=env);trees=[]
    for group in groups:
        for row in group:
            entry=git('ls-files','-s','--',row['path']).split('\t')[0].split()
            git('update-index','--add','--cacheinfo',entry[0]+','+entry[1]+','+row['path'],env=env)
        trees.append(git('write-tree',env=env))
    assert trees[-1]==plan['expected_tree']
    done=json.loads((OUT/'signed-batch-progress.json').read_bytes());head=parent
    for number,row in enumerate(done):
        actual=request('repos/'+SLUG+'/git/commits/'+row['sha'])
        assert actual['tree']['sha']==trees[number] and actual['parents'][0]['sha']==head
        assert actual['verification']['verified'] and actual['verification']['reason']=='valid';head=row['sha']
    for number in range(len(done),len(groups)):
        group=groups[number]
        mutation={'query':'mutation($input: CreateCommitOnBranchInput!) { createCommitOnBranch(input:$input) { commit { oid url signature { isValid state } } } }',
            'variables':{'input':{'branch':{'repositoryNameWithOwner':SLUG,'branchName':'main'},'expectedHeadOid':head,
            'message':{'headline':'Preserve M10a2 raw execution evidence ('+str(number+1)+'/'+str(len(groups))+')'},
            'fileChanges':{'additions':group,'deletions':[]}}}}
        for attempt in range(3):
            remote=request('repos/'+SLUG+'/branches/main')['commit']['sha']
            if remote!=head:
                actual=request('repos/'+SLUG+'/git/commits/'+remote)
                assert actual['tree']['sha']==trees[number] and actual['parents'][0]['sha']==head,'非本批远端变化'
                oid=remote;break
            try:
                result=request('graphql',mutation)
                if result.get('errors'):raise RuntimeError(json.dumps(result['errors']))
                oid=result['data']['createCommitOnBranch']['commit']['oid'];break
            except RuntimeError as error:
                if 'HTTP 499' not in str(error) or attempt==2:raise
                print('RETRY_SAME_EXPECTED_PARENT batch='+str(number+1),flush=True)
                time.sleep(2)
        actual=request('repos/'+SLUG+'/git/commits/'+oid);v=actual['verification']
        assert actual['tree']['sha']==trees[number] and actual['parents'][0]['sha']==head
        assert v['verified'] and v['reason']=='valid'
        done.append({'sha':oid,'files':len(group),'verified':True});head=oid
        save(OUT/'signed-batch-progress.json',done);print('VERIFIED_BATCH '+str(number+1)+'/'+str(len(groups))+' '+oid,flush=True)
    git('-c','http.proxy=http://127.0.0.1:7890','fetch','origin','main')
    assert git('rev-parse','refs/remotes/origin/main')==head and git('write-tree')==plan['expected_tree']
    assert not git('diff','--name-only');git('update-ref','refs/heads/main',head,parent)
    assert not git('diff','--cached','--name-only')
    receipt={'status':'GITHUB_SIGNED_VERIFIED_COMMIT_ON_MAIN','sha':head,'url':'https://github.com/'+SLUG+'/commit/'+head,
        'parent':parent,'tree':plan['expected_tree'],'github_verification':{'verified':True,'reason':'valid'},
        'prior_history_preserved':True,'force_push':False,'local_main_equals_remote':True,
        'committed_files':len(additions),'verified_batches':done}
    save(E/'commit.json',receipt);print(json.dumps(receipt))
if __name__=='__main__':main()
