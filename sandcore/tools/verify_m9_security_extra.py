#!/usr/bin/env python3
"""补齐真实账户CLI、SKM边界/回收和不可恢复的零环异常证据。"""
import argparse
import json
from pathlib import Path
import re
import secrets
import struct
import threading

from scserial import QemuSession
from verify_m9 import Guest, png_from_ppm
from verify_m9_session_lifecycle import read_u32


def skm(code,bss=0,fixes=()):
    return b'SKM1MIO\0'+struct.pack('<6I',0,len(code),bss,len(fixes),1,0x004f494d)+code+b''.join(struct.pack('<I',x) for x in fixes)


def run(boot,data,out,symbols):
    report=dict(status='RUNNING',cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True,audio=False) as vm:
        g=Guest(vm)
        def check(name,okay,reference=None,**details):
            report['cases'].append(dict(name=name,reference=reference,status='PASS' if okay else 'FAIL',**details))
            if not okay:
                raise AssertionError(name)
        def case(name,script,expected=b'',status=0,reference=None):
            code,output,_,_,_=g.run(script)
            check(name,code==status and (expected is None or output==expected),reference,exit_code=code,stdout_hex=output.hex())
            return output
        def password(command,prompt,password,second=None):
            marker,start=g.dialog(command,prompt)
            at=len(g.console);g.client.input(password.encode()+b'\n')
            if second is not None:
                g.wait(rb'Again: ',at);g.client.input(second.encode()+b'\n')
            code,output=g.finish_dialog(marker,start)
            check(command.split()[0]+'-secret-not-echoed',password.encode() not in output,command.split()[0])
            return code,output
        try:
            g.connect()
            # 根据公开格式独立手算，两个独立入口实际输出同样的完整字节。
            raw=b'\0A\xff\n';g.put_bytes(raw,'/TMP/HEXQA','hex')
            line=b'00000000  00 41 ff 0a '+b'   '*4+b' '+b'   '*8+b' |.A..|\n00000004\n'
            for command in ('hd','hexdump'):
                case(command+'-binary-exact',command+' -C /TMP/HEXQA',line,reference=command)
                case(command+'-missing-file',command+' /TMP/NOHEX',None,1,command)
            case('create-account','adduser cliver 1130 1131 /HOME/CLIVER',b'Account created; set its password with passwd cliver\n',reference='adduser')
            first='p'+secrets.token_hex(12);second='q'+secrets.token_hex(12)
            code,output=password('passwd cliver',rb'New password: ',first,first)
            check('SYSTEM-passwd-success',code==0 and b'Password updated' in output,'passwd')
            marker,start=g.dialog('su cliver',rb'Password: ')
            at=len(g.console);g.client.input(first.encode()+b'\n')
            ready=g.wait(rb'cliver /[^\r\n]* \$ ',at)
            check('su-valid-kernel-ticket-creates-user-shell',first.encode() not in bytes(g.console[start:ready.end()]),'su')
            at=len(g.console)
            g.client.input(b'id -u; id -g; printf USEROK > /HOME/CLIVER/OWN; exit\n')
            code,output=g.finish_dialog(marker,start)
            check('su-child-UID-GID-and-parent-return',code==0 and re.search(rb'\r?\n1130\r?\n1131(?:\r?\n|$)',output) is not None,'su',stdout_hex=output.hex())
            case('su-home-owned-write','cat /HOME/CLIVER/OWN',b'USEROK',reference='su')
            code,output=password('passwd cliver',rb'New password: ',second,second)
            check('passwd-replacement-success',code==0 and b'Password updated' in output,'passwd')
            # root/SYSTEM有普通账户管理权，内核允许其票据切换；密码
            # 正误必须在普通UID里验证，不能把管理员绕过当作改密失效。
            marker,start=g.dialog('su cliver',rb'Password: ')
            at=len(g.console);g.client.input(second.encode()+b'\n');g.wait(rb'cliver /[^\r\n]* \$ ',at)
            for label,secret,success in [('old-password',first,False),('new-password',second,True)]:
                token=('__LOGIN_'+secrets.token_hex(12)).encode();at=len(g.console)
                g.client.input(b'login cliver; echo '+token+b':$?\n');g.wait(rb'Password: ',at)
                password_at=len(g.console);g.client.input(secret.encode()+b'\n')
                if success:
                    g.wait(rb'cliver /[^\r\n]* \$ ',password_at);g.client.input(b'id -u; exit\n')
                done=g.wait(rb'\r?\n'+token+rb':([0-9]+)\r?\n',at)
                output=bytes(g.console[at:done.start()]);g.wait(rb'cliver /[^\r\n]* \$ ',done.end())
                check(label+'-ordinary-login',int(done[1])==(0 if success else 1) and secret.encode() not in output
                      and (not success or re.search(rb'\r?\n1130(?:\r?\n|$)',output) is not None),'login',stdout_hex=output.hex())
            g.client.input(b'exit\n');code,_=g.finish_dialog(marker,start)
            check('nested-login-parent-SYSTEM-return',code==0,'login')
            code,output=password('login SYSTEM',rb'Password: ',second)
            check('SYSTEM-ordinary-login-impossible',code==1,'login')
            case('parent-remains-external-SYSTEM','id -u',b'-1\n',reference='id')
            case('id-group-selection','id -G',b'-1\n',reference='id')
            case('id-current-user-name','id -un',b'SYSTEM\n',reference='id')
            for option in ('-u -g','-gn','-z','other'):
                case('id-reject-'+option,'id '+option,b'',2,'id')
            case('delete-account','deluser cliver',reference='deluser')
            # 三个真实native编译的模块分别漏释放、初始化失败、一次性注册
            # 回调。页计数和旧回调指针都必须恢复，不能只看工具非零退出。
            sources={
                'LEAK': '#include "SCKERNEL.H"\nint skm_main(sc_kernel_api *a){void *p=a->allocate(8193);return p?0:9;}\n',
                'FAIL': '#include "SCKERNEL.H"\nstatic void paint(void){}\nint skm_main(sc_kernel_api *a){void *p=a->allocate(8193);a->scene(paint);return p?7:9;}\n',
                'SCENE': '#include "SCKERNEL.H"\nstatic void paint(void){}\nint skm_main(sc_kernel_api *a){void *p=a->allocate(8193);a->scene(paint);return p?0:9;}\n'}
            for name,source in sources.items():
                g.put_bytes(source.encode(),'/TMP/'+name+'.C',name)
                case('native-SKM-'+name,'s3c /TMP/'+name+'.C /SYS/MOD/'+name+'.SKM --skm',None)
            case('warm-shell','true')
            for name,resident in [('LEAK',False),('LEAK',False),('FAIL',True),('SCENE',False)]:
                before=read_u32(vm,symbols['pf_used']);callback=read_u32(vm,symbols['scene_callback']);cursor=read_u32(vm,symbols['cursor'])
                case('SKM-'+name+('-resident' if resident else '-once'),'skmrun /SYS/MOD/'+name+'.SKM'+(' --resident' if resident else ''),None,0 if name=='LEAK' else 7 if name=='FAIL' else 1)
                after=read_u32(vm,symbols['pf_used'])
                check('SKM-'+name+'-page-and-callback-reclaim',before==after and callback==read_u32(vm,symbols['scene_callback']) and cursor==read_u32(vm,symbols['cursor']),before_pages=before,after_pages=after)
            invalid=[('truncated',b'SKM1MIO'),('wrong-ABI',skm(b'\x31\xc0\xc3')[:24]+struct.pack('<I',2)+struct.pack('<I',0x004f494d)+b'\x31\xc0\xc3'),
                     ('zero-payload',skm(b'')),('outside-reloc',skm(b'\0'*4,fixes=(4,))),('duplicate-reloc',skm(b'\0'*4,fixes=(0,0))),
                     ('overlap-reloc',skm(b'\0'*8,fixes=(0,2))),('reloc-value-outside-image',skm(struct.pack('<I',4),fixes=(0,))),
                     ('huge-BSS',skm(b'\x31\xc0\xc3',bss=131073))]
            for name,body in invalid:
                g.put_bytes(body,'/SYS/MOD/BAD.SKM','bad-'+name)
                before=read_u32(vm,symbols['pf_used'])
                case('SKM-reject-'+name,'skmrun /SYS/MOD/BAD.SKM',None,1)
                check('SKM-invalid-no-page-leak-'+name,before==read_u32(vm,symbols['pf_used']))
            g.put_bytes(skm(b'\x31\xc0\xc3'),'/SYS/MOD/INSQA.SKM','valid-resident')
            case('insmod-native-resident','insmod /SYS/MOD/INSQA.SKM',reference='insmod')
            case('lsmod-resident-name','lsmod | grep INSQA.SKM',b'SYS/MOD/INSQA.SKM\n',reference='lsmod')
            case('insmod-case-alias-no-hot-reload','insmod /sys/mod/insqa.skm',None,1,'insmod')
            # 最后才触发真实UD2；不尝试把CPL0 panic当可恢复用户异常。
            g.put_bytes(skm(b'\x0f\x0b\xc3'),'/SYS/MOD/FATALQA.SKM','fatal')
            at=len(g.debug);vm.pipes['debug'].write(b'hello\n');g.wait(rb'SCDBG1 hello ',at,debug=True)
            # 真正CPL0异常会停住管理调用本身，ACK必须允许仍待发送。
            # 独立请求线程让主线程继续使用COM1，不阻塞在COM2回复上。
            errors=[]
            def fatal_input():
                try:
                    g.client.input(b'skmrun /SYS/MOD/FATALQA.SKM\n')
                except BaseException as error:
                    errors.append(repr(error))
            at=len(g.debug);sender=threading.Thread(target=fatal_input,name='fatal-pending-input');sender.start()
            stopped=g.wait(rb'STOP vector=00000006 eip=([0-9a-f]+)\r\n',at,debug=True)
            check('real-CPL0-UD2-stop',True,eip=stopped[1].decode())
            at=len(g.debug);vm.pipes['debug'].write(b'cont\n');g.wait(rb'ERR fatal exception cannot resume\r\n',at,debug=True)
            check('fatal-CPL0-cannot-resume',True)
            picture=out/'fatal-debug.ppm';vm.hmp('screendump "'+picture.as_posix()+'"');report['screenshots'].append(png_from_ppm(picture))
            g.stop.set();g.client.fail(RuntimeError('EXPECTED_FATAL_STOP'));sender.join(5)
            check('fatal-pending-request-cancelled',not sender.is_alive(),host_request_errors=errors)
            report['status']='SECURITY_EXTRA_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error));raise
        finally:
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            g.close()
    return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--boot',required=True,type=Path);p.add_argument('--data',required=True,action='append',type=Path)
    p.add_argument('--symbols',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    # cursor在其它模块也可能重名；module.o符号地址从同批objdump解析。
    symbols={m[2]:int(m[1],16) for m in re.finditer(r'^([0-9a-f]+)\s+[bB]\s+(pf_used|scene_callback|cursor)$',a.symbols.read_text(encoding='ascii'),re.M)}
    a.out.mkdir(parents=True,exist_ok=False);matrix=dict(status='RUNNING',disks=[])
    try:
        for i,data in enumerate(a.data,1):
            matrix['disks'].append(run(a.boot,data,a.out/f'disk-{i}',symbols));print(f'disk-{i}: SECURITY_EXTRA_CASES_PASS',flush=True)
        matrix['status']='SECURITY_EXTRA_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error));raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
