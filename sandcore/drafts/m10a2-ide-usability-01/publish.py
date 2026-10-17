"""在Verified提交上发布独立补丁附件，保留原M10a2发行。"""
from pathlib import Path
import hashlib,json,sys,urllib.parse
ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/'drafts/m10a2-session-return-01'))
from github_api import request,REPO
OUT=ROOT/'build/m10a2-product-delivery-01'
E=ROOT/'evidence/m10a2-product-fix'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    c=json.loads((OUT/'commit.json').read_bytes());audit=json.loads((E/'delivery.json').read_bytes())
    assert c['status']=='GITHUB_SIGNED_VERIFIED_COMMIT_ON_MAIN'
    assert request('repos/'+REPO+'/git/commits/'+c['sha'])['verification']['verified']
    tag='M10a2.1';files=dict(audit['assets']);files['SHA256SUMS']=dict(bytes=(OUT/'SHA256SUMS').stat().st_size,sha256=sha(OUT/'SHA256SUMS'))
    for name,row in files.items():assert sha(OUT/name)==row['sha256']
    releases=request('repos/'+REPO+'/releases?per_page=100')
    assert not any(r['tag_name']==tag for r in releases),'补丁已存在，停止以免重复或覆盖'
    body='''SandCore M10a2 — The Application Platform Era：Studio 与 Files 产品修正。

- 恢复 SCAPI.H、SCIO.H、SCMEM.H 源码包含支持，SCAPI.H 原字节和旧 ABI 不变。
- Studio 开始页、新建 C/H 的明确类型和后缀、项目名称/位置/类型向导；直接打开 C 即可构建。普通程序自动生成链接清单，保留多文件、显式 SCW/SCL、调试及发布功能。
- 默认 HelloWorld 项目已实际 F7 构建、Ctrl+F5 运行并输出 Hello, world!；真实编译错误显示文件、行号和原因。输出自动跟随末行。
- 修正公共文件夹双击/Enter 导航、列表滚动边界、默认类型显示，以及 Files 右键编辑调用旧程序的问题。

运行包解压仅 run.py、run.bat、sandcore.img、sanddata.img 四文件；SDK 独立提供9库、公共头、系统内编译工具、例子、规范及源码/许可证归档。Windows QEMU 与 Python 为宿主依赖，启动器继续优先 WHPX。

本补丁绑定 HOST26/native15/final15，具体操作证据见仓库 evidence/m10a2-product-fix。未重复原版验收矩阵；原 M10a2 和历史证据保留。这里的干净运行盘不是用户个人 test 盘，不会包含用户源码和账户数据。首次登录界面的已披露资源问题没有借本补丁改称通过。
'''
    release=request('repos/'+REPO+'/releases',dict(tag_name=tag,target_commitish=c['sha'],name='SandCore M10a2.1 — Studio and Files fixes',body=body,draft=True,prerelease=False),method='POST')
    (OUT/'release-draft.json').write_text(json.dumps(dict(id=release['id'],tag=tag),indent=2)+'\n',encoding='utf-8')
    upload=release['upload_url'].split('{')[0]
    for name,row in files.items():
        a=request(upload+'?name='+urllib.parse.quote(name),data=(OUT/name).read_bytes(),method='POST',headers={'Content-Type':'application/octet-stream'})
        assert a['size']==row['bytes'] and a['digest']=='sha256:'+row['sha256']
    request('repos/'+REPO+'/releases/'+str(release['id']),dict(draft=False),method='PATCH')
    observed=request('repos/'+REPO+'/releases/tags/'+tag)
    assert not observed['draft'] and not observed['prerelease']
    actual={a['name']:a for a in observed['assets']};assert set(actual)==set(files)
    for name,row in files.items():assert actual[name]['size']==row['bytes'] and actual[name]['digest']=='sha256:'+row['sha256']
    record=dict(status='RELEASE_PUBLISHED_ASSETS_HASH_VERIFIED',url=observed['html_url'],tag=tag,sha=c['sha'],commit_verified=True,
        assets={n:dict(bytes=a['size'],digest=a['digest'],url=a['browser_download_url']) for n,a in actual.items()})
    (OUT/'publish.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(json.dumps(record))
if __name__=='__main__':main()
