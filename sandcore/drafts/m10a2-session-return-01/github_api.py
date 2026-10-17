"""只用已授权WSL gh账户的现有凭据，令牌仅在子进程管道/内存中流转。"""
import json,subprocess,urllib.request,urllib.error
REPO='mio-qwq/SandCore'
def request(path,data=None,method=None,headers=None):
    auth=subprocess.run(['wsl.exe','gh','auth','token','--hostname','github.com'],capture_output=True)
    if auth.returncode:raise RuntimeError('WSL gh credential unavailable; stderr suppressed')
    token=auth.stdout.decode().strip()
    if not token or '\n' in token:raise RuntimeError('Credential pipe is not a single token')
    h={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28','User-Agent':'SandCore-release'}
    if headers:h.update(headers)
    if isinstance(data,(dict,list)):
        data=json.dumps(data).encode();h['Content-Type']='application/json'
    req=urllib.request.Request(path if path.startswith('https://') else 'https://api.github.com/'+path,data=data,method=method,headers=h)
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({'https':'http://127.0.0.1:7890'}))
    try:
        with opener.open(req,timeout=60) as r:return json.load(r)
    except urllib.error.HTTPError as e:
        raise RuntimeError('GitHub HTTP '+str(e.code)+'; response body suppressed') from None
if __name__=='__main__':
    r=request('repos/'+REPO+'/branches/main')
    print(json.dumps({'branch':r['name'],'sha':r['commit']['sha']}))
