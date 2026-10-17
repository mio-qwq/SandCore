"""仅在实际Verified提交后上传已检查资产，核摘要再公开Release。"""
from pathlib import Path
import json,sys,hashlib,urllib.parse
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'drafts/m10a2-session-return-01'))
from github_api import request,REPO
OUT=ROOT/'build/m10a2-delivery-01';E=ROOT/'evidence/m10a2-delivery-01'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,r):p.write_bytes((json.dumps(r,indent=2)+'\n').encode())
def main():
    commit=json.loads((E/'commit.json').read_bytes());audit=json.loads((E/'delivery.json').read_bytes())
    assert commit['status']=='GITHUB_SIGNED_VERIFIED_COMMIT_ON_MAIN'
    oid=commit['sha'];v=request('repos/'+REPO+'/git/commits/'+oid)['verification']
    assert v['verified'] and v['reason']=='valid'
    files=dict(audit['assets']);files['SHA256SUMS']={'sha256':audit['sums_sha256'],'bytes':(OUT/'SHA256SUMS').stat().st_size}
    for name,row in files.items():assert sha(OUT/name)==row['sha256'] and (OUT/name).stat().st_size==row['bytes']
    body='''SandCore M10a2 — The Application Platform Era

Ring3 SCL动态链接、SCCLIB/SCLIB/SC32API/SCUI、统一桌面与主要组件，以及真实系统内SCCC/SCLD、多源SCW和第三方动态库开发流程。

- `SandCore-M10a2.zip`：解压仅4文件，安装Windows QEMU和Python后运行run.bat；默认SDL原生鼠标/物理DPI/最近邻，优先WHPX。
- `SandCore-M10a2-SDK.zip`：9库、公共头、原生工具、多源例子、同批规范和固定源码/完整许可。8库来自实际客体编译；SCSOUND是明确的宿主codec例外。
- `SHA256SUMS`：附件摘要。源提交经GitHub实际Verified验证。

本批修复高DPI入口/全屏鼠标/快拖取消、终端与安装器显示；退出桌面保留会话并回登录，注销关闭会话，默认未设密码mio可在固定本机登录界面空输入登录。root、普通su/网络没有这个例外。

**准确限制：** GUI12和native11分别有零差资源证据；GUI13登录功能实测有效，但首次保留LOCKED登录桌面+8页，原资源FAIL/未归因8及驱动退出1保留。源码生命周期与实际编译的32660B状态匹配，未直接捕获这8页地址归属，不能称该轮整体PASS。当前最大1920×1080，16:10全屏有比例黑边；SCCLIB是C32兼容范围，非完整ISO C。HTTPS/TLS、GPU、SMP、IPv6及完整系统双语未实现或未承诺。完整M10与基础内核冻结未在本版宣称完成。

用户test盘另行迁移并保留原对象；本包使用独立干净基盘，不包含用户test盘。
'''
    base='https://github.com/'+REPO+'/blob/'+oid+'/sandcore/'
    body+='\n[完整交付检查]('+base+'docs/M10A2-ACCEPTANCE.md) · [SDK规范]('+base+'docs/M10A2-SDK.md) · [GUI12证据]('+base+'evidence/m10a2-gui12/README.md) · [登录回归及资源限制]('+base+'evidence/m10a2-session-return/README.md)\n'
    body+='\n![实际重新登录桌面](https://raw.githubusercontent.com/'+REPO+'/'+oid+'/sandcore/evidence/m10a2-session-return/screens/relogin-result.png)\n'
    # 路径由本地实际截图确认，正文不依赖自动生成的功能推测。
    screenshot=ROOT/'evidence/m10a2-session-return/screens/relogin-result.png'
    if not screenshot.exists():
        actual=list((ROOT/'evidence/m10a2-session-return').rglob('relogin-result.png'));assert len(actual)==1
        body=body.replace('/screens/relogin-result.png','/'+actual[0].relative_to(ROOT/'evidence/m10a2-session-return').as_posix())
    try:release=request('repos/'+REPO+'/releases/tags/M10a2')
    except RuntimeError as error:
        if str(error)!='GitHub HTTP 404; response body suppressed':raise
        release=request('repos/'+REPO+'/releases',{'tag_name':'M10a2','target_commitish':oid,
            'name':'SandCore M10a2 — The Application Platform Era','body':body,'draft':True,'prerelease':False},method='POST')
    assert release['tag_name']=='M10a2' and release['target_commitish']==oid
    assert release['draft'],'不覆盖既有公开Release'
    assets={a['name']:a for a in request('repos/'+REPO+'/releases/'+str(release['id'])+'/assets')}
    for name,row in files.items():
        asset=assets.get(name)
        if asset is None:
            url=release['upload_url'].split('{')[0]+'?name='+urllib.parse.quote(name)
            asset=request(url,(OUT/name).read_bytes(),method='POST',headers={'Content-Type':'application/zip' if name.endswith('.zip') else 'text/plain'})
        assert asset['state']=='uploaded' and asset['size']==row['bytes']
        assert asset.get('digest')=='sha256:'+row['sha256'],'GitHub资产摘要不符或缺失，保持draft'
        assets[name]=asset
        save(OUT/'release-upload-progress.json',{'release_id':release['id'],'uploaded':list(assets)})
    assert set(assets)==set(files)
    release=request('repos/'+REPO+'/releases/'+str(release['id']),{'draft':False,'body':body},method='PATCH')
    observed=request('repos/'+REPO+'/releases/tags/M10a2')
    assert not observed['draft'] and not observed['prerelease']
    tag=request('repos/'+REPO+'/git/ref/tags/M10a2')['object']
    assert tag['type']=='commit' and tag['sha']==oid
    final={a['name']:a for a in observed['assets']};assert set(final)==set(files)
    for name,row in files.items():assert final[name]['digest']=='sha256:'+row['sha256'] and final[name]['size']==row['bytes']
    receipt={'status':'RELEASE_PUBLISHED_ASSETS_HASH_VERIFIED','tag':'M10a2','commit':oid,'commit_verified':True,
        'release_id':observed['id'],'url':observed['html_url'],'draft':False,'prerelease':False,
        'published_at':observed['published_at'],'assets':{n:{'url':a['browser_download_url'],'bytes':a['size'],'digest':a['digest']} for n,a in final.items()}}
    save(E/'publish.json',receipt);print(json.dumps(receipt))
if __name__=='__main__':main()
