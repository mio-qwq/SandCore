"""登录界面不是普通UID1000，独立验证LOCKED范围，不削弱普通GUI绑定。"""
import hashlib,json,re,time
from coherent_runtime import CoherentRuntime
class LoginRuntime(CoherentRuntime):
    def login_scope(self,ident):
        self.state['session']=None
        listed=self.h.run('login-scope-'+ident,'sessionctl list')
        rows=self.session_parser(listed['output'].encode())
        active=[r for r in rows if r['active']]
        if len(active)!=1 or active[0]['type']!='LOCKED' or active[0]['uid']!=-2:
            raise ValueError('登录输入只允许实际可见的LOCKED会话')
        processes=self.h.run('login-process-'+ident,'ps')
        found=re.findall(r'^([0-9]+)\s+-1\s+-1\s+1\s+LOGIN2\.SCX\s*$',processes['output'],re.M)
        if len(found)!=1:raise ValueError('缺少唯一实际LOGIN2进程，不允许登录输入')
        return dict(session=active[0],login_pid=int(found[0]),process_case=processes['name'],
            session_case=listed['name'],realm_provenance='fixed issuer semantics in actual CORE source; not read from ps',
            channel='ACTUAL_HMP_PS2_FIXED_LOGIN_SCREEN_NOT_ORDINARY_SESSION')
    def request(self,request):
        op=request.get('op') if isinstance(request,dict) else None
        if op not in ('login_keys','login_type','login_click'):return super().request(request)
        ident=request.get('id')
        if not isinstance(ident,str) or not ident:raise ValueError('每个登录输入必须有唯一id')
        fingerprint=hashlib.sha256(json.dumps(request,sort_keys=True,ensure_ascii=False).encode()).hexdigest()
        prior=self.state['actions'].get(ident)
        if prior:
            if prior['request_sha256']!=fingerprint:raise ValueError('相同输入id内容改变')
            return dict(prior,replayed=False)
        record=dict(id=ident,op=op,request_sha256=fingerprint,status='ATTEMPTING',seconds=time.monotonic(),qemu_pid=self.h.vm.process.pid)
        self.state['actions'][ident]=record;self._persist()
        try:
            scope=self.login_scope(ident)
            if op=='login_type':
                # 本次只输入公开用户名，空密码由真实Tab/Enter提交，日志不接秘密。
                if request.get('text') not in ('mio','root'):raise ValueError('本轮登录文本仅允许两个非秘密用户名')
                self.flow.type_ascii(request['text']);result=dict(text=request['text'])
            elif op=='login_keys':
                keys=request.get('keys')
                if not isinstance(keys,list) or not keys or any(not isinstance(k,str) or not re.fullmatch(r'[a-z0-9_-]+',k) for k in keys):raise ValueError('需要实际HMP键名数组')
                self.flow.keys(*keys);result=dict(keys=keys)
            else:
                screen=self._screen_ref(request['screenshot']);point=self._point(request['point'],screen)
                self.flow.inputs.click(*point,which='left');result=dict(point=point,screenshot=request['screenshot'])
            result.update(scope=scope,effect='UNVERIFIED_REQUIRES_ACTUAL_SCREEN_AND_SESSION_EVIDENCE')
            record.update(status='EXECUTED_EFFECT_REQUIRES_EVIDENCE',result=result)
        except BaseException as e:
            record.update(status='UNVERIFIED_OR_INPUT_UNCERTAIN',error=dict(type=type(e).__name__,message=str(e)))
        self._persist();return record
