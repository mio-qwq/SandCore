#!/usr/bin/env python3
"""mio：Settings实际G2生成、鼠标草稿/坏值/文本编辑/冷启动验证。

测试盘仅在开机前准备，运行后所有保存都由普通三环程序执行。
读取应用变量/页表只断言结果；不调用客体函数或改写内存。
"""
import hashlib,json,struct,time,zipfile
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-settings';OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT;q=t.q

def wait(test,label,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        assert 3 not in compiler.task_states(),label+' / 三环异常'
        time.sleep(.12)
    q.shot('timeout');raise AssertionError(label)

def idle():
    wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1 and 2 not in compiler.task_states()[1:],'实际退出和回收')

def user_bytes(w,address,count):
    pd=t.word(q.symbols()['tasks']+w['owner']*168);result=b''
    while count:
        n=min(count,4096-(address&4095));result+=q.memory(t.physical(pd,address),n);count-=n;address+=n
    return result

def main():
    inputs={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
            ('build/sandcore.img','build/kernel.elf','build/kernel.sym','user/SCAPI.H','user/settings.c','user/SETTINGS.inc','user/NUI.inc','user/configprobe.c')}
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/SRC/cfgprobe.c',(ROOT/'user/configprobe.c').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        compiler.disk_put(disk,'SYS/USER.CFG',b'SUSER1MIO\nusername=mio\nhome=/HOME\n')
        compiler.disk_put(disk,'SYS/ENV.CFG',b'SENV1MIO\nPATH=/BIN:/APPS\nKEEP=old\n')
        compiler.disk_put(disk,'HOME/UTF8.CFG','#沙核\nflag=yes\n'.encode())
        compiler.disk_put(disk,'HOME/TOOBIG.CFG',b'x'*8192)
        compiler.disk_put(disk,'HOME/BINARY.CFG',b'abc\0tail')
    proc=t.launch('std',128,'settings',prepare);disk=OUT/'sanddata-std-128-settings.img'
    checks=[]
    try:
        t.open_shell(True)
        probe=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/cfgprobe.c','HOME/CFGP.SCX',180);idle()
        ps=theme.native_symbols(compiler.await_file(disk,'HOME/CFGP.SCX.map'))
        baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/CFGP.SCX\n');w=wait(theme.window,'真实配置API探针')
        wait(lambda:theme.user_word(w,ps['config_probe'])==1,'全部纯检查行为')
        values=struct.unpack('<8I',user_bytes(w,ps['config_probe'],32));assert values[1]==0,values
        q.shot('01-pure-config-check');q.key('esc');idle()
        wait(lambda:t.word(q.symbols()['pf_used'])==baseline[0]+t.word(q.symbols()['desktop_pages'])-baseline[1],'API探针严格回收')
        settings=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/settings.c','HOME/SET.SCX',240);idle()
        symbols=theme.native_symbols(compiler.await_file(disk,'HOME/SET.SCX.map'))
        (OUT/'settings-native.scx').write_bytes(settings);(OUT/'settings-native.map').write_bytes(compiler.await_file(disk,'HOME/SET.SCX.map'))
        (OUT/'configprobe-native.scx').write_bytes(probe)
        baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        original_shell=t.windows()[0];shell_pages=(original_shell['cw']*original_shell['ch']*4+4095)//4096
        q.text('run HOME/SET.SCX\n');w=wait(theme.window,'原生六页Settings')
        # 窗口先出现，随后才分配私有8MB帧并建立逻辑尺寸；不能把
        # 尚未初始化的UI_W=0当两列布局，点错位置后归为鼠标失效。
        wait(lambda:theme.user_word(w,symbols['ui_frames'])>=2,'初次实际布局与提交')
        def number(name):return theme.user_word(w,symbols[name])
        def string(name,capacity=256):return user_bytes(w,symbols[name],capacity).split(b'\0')[0].decode()
        def commit():
            frames=number('ui_frames');wait(lambda:number('ui_frames')>frames and t.word(q.symbols()['dirty'])==0,'实际界面提交')
        def click(x,y):
            current=[win for win in t.windows() if win['handle']==w['handle']][0]
            scale=number('ui_scale');title=current['h']-current['ch']-1
            t.point(current['x']+1+x*scale//100,current['y']+title+y*scale//100);t.click()
        def tab(index):
            cols=6 if number('UI_W')>=760 else 3 if number('UI_W')>=480 else 2
            step=26 if number('ui_compact') else 36;top=34 if number('ui_compact') else 70
            width=(number('UI_W')-32-(cols-1)*8)//cols
            click(20+(index%cols)*(width+8),top+(index//cols)*step+8)
            wait(lambda:number('tab')==index,'鼠标页签'+str(index));commit()
        def edit(x,y,text):
            click(x,y);wait(lambda:number('ui_modal')==1,'文本框实际输入焦点')
            q.text(text+'\n');time.sleep(.3)
        tab(3);top=number('settings_body_top')
        original=compiler.file_content(disk,'SYS/USER.CFG')
        edit(48,top+70,'bad/name');wait(lambda:number('user_dirty')==1,'用户名草稿')
        click(48,top+236);time.sleep(.4)
        assert compiler.file_content(disk,'SYS/USER.CFG')==original and number('user_dirty')==1
        edit(48,top+70,'tester');click(48,top+236)
        wait(lambda:number('user_dirty')==0,'图形用户名保存');assert b'username=tester\n' in compiler.file_content(disk,'SYS/USER.CFG')
        q.shot('02-user-saved')
        tab(4);top=number('settings_body_top');original_env=compiler.file_content(disk,'SYS/ENV.CFG')
        click(80,top+8);q.key('ret');q.text('/BIN::/APPS\n');time.sleep(.3)
        click(256,top+8);time.sleep(.3)
        assert compiler.file_content(disk,'SYS/ENV.CFG')==original_env and number('env_dirty')==1
        click(80,top+8);q.key('ret');q.text('/APPS:/BIN\n');click(256,top+8)
        wait(lambda:number('env_dirty')==0,'PATH有效保存')
        click(32,top+8);q.text('FAVORITE\nsea\n');wait(lambda:number('env_count')==3,'添加环境变量')
        click(256,top+8);wait(lambda:number('env_dirty')==0,'保留未知键完整保存')
        env=compiler.file_content(disk,'SYS/ENV.CFG');assert b'KEEP=old\n' in env and b'FAVORITE=sea\n' in env and b'PATH=/APPS:/BIN\n' in env
        q.shot('03-environment-saved')
        click(32,top+8);q.text('DRAFT_ONLY\nvalue\n');wait(lambda:number('env_count')==4 and number('env_dirty')==1,'另一页未保存草稿')
        tab(5);top=number('settings_body_top');click(40,top+8)
        wait(lambda:number('cfg_editing')==1,'通用配置编辑器')
        saved=compiler.file_content(disk,'SYS/USER.CFG');q.key('f9');q.text('broken=1\n');q.key('f2');time.sleep(.5)
        assert compiler.file_content(disk,'SYS/USER.CFG')==saved and number('cfg_dirty')==1
        q.shot('04-invalid-config-preserved')
        q.key('esc');q.key('esc');assert number('cfg_editing')==1,'取消放弃应保留文本'
        q.key('esc');q.key('y');wait(lambda:number('cfg_editing')==0,'显式确认放弃')
        click(40,top+8);wait(lambda:number('cfg_editing')==1,'重新开用户文本')
        q.key('f9');q.text('# kept\n');q.key('f2');wait(lambda:number('cfg_dirty')==0,'通用USER保存有效注释')
        assert number('env_count')==4 and number('env_dirty')==1,'提交USER不能丢ENV草稿'
        q.key('esc');wait(lambda:number('cfg_editing')==0,'保存后返回')
        tab(4);click(320,number('settings_body_top')+8);q.key('y')
        wait(lambda:number('env_count')==3 and number('env_dirty')==0,'显式重载才放弃另一页草稿')
        tab(5)
        # 文件按钮按toolbar的实际宽度推算，逻辑坐标随模式/缩放映射。
        def file_button(index):
            names=['USER.CFG','ENV.CFG','DISPLAY.CFG','THEME.CFG','MENU.CFG','WALL.CFG','Open file','New file'];x=16;y=number('settings_body_top');step=26 if number('ui_compact') else 36
            for i,name in enumerate(names):
                width=len(name)*8+20
                if x+width>number('UI_W')-16:x=16;y+=step
                if i==index:
                    click(x+8,y+8)
                    if index==6:wait(lambda:number('ui_modal')==2,'打开路径框实际就绪')
                    return
                x+=width+8
        file_button(6);q.text('HOME/UTF8.CFG\n');wait(lambda:number('cfg_editing')==1,'打开用户UTF-8配置')
        q.key('f8');q.key('right');q.key('right');q.key('backspace');q.key('f2')
        wait(lambda:compiler.file_content(disk,'HOME/UTF8.CFG')=='#核\nflag=yes\n'.encode() and number('cfg_dirty')==0,'汉字标量退格真实写盘')
        q.shot('05-unicode-config-editor');q.key('esc');wait(lambda:number('cfg_editing')==0,'保存后返回')
        file_button(6);q.text('HOME/TOOBIG.CFG\n');time.sleep(.4);assert number('cfg_editing')==0 and '8191' in string('status',96)
        file_button(6);q.text('HOME/BINARY.CFG\n');time.sleep(.4);assert number('cfg_editing')==0 and 'preserved' in string('status',96)
        checks.append('原生Settings用户坏值拒绝/环境全部键保留/PATH坏值拒绝/通用保存取消/跨页草稿保留/汉字标量退格/过长和二进制拒绝')
        def toolbar(index,names,top):
            x=16;y=top;step=26 if number('ui_compact') else 36
            for i,name in enumerate(names):
                width=len(name)*8+20
                if x+width>number('UI_W')-16:x=16;y+=step
                if i==index:click(x+8,y+8);return
                x+=width+8
        tab(1);top=number('settings_body_top')
        desktop_labels=['White Aurora','Classic','Custom theme','Reload theme','Wallpaper file','Reload desktop','VGA dawn','VGA night','VGA warm']
        # Desktop 在工具条前另有一行 THEME 标题。自动点击必须与真实
        # 布局共用这 24 个逻辑像素的偏移，否则点到标题不能代表按钮失败。
        toolbar(1,desktop_labels,top+24);wait(lambda:number('ui_classic')==1,'Classic实际主题提交');commit();q.shot('07-classic-settings')
        toolbar(0,desktop_labels,top+24);wait(lambda:number('ui_classic')==0,'Aurora实际主题提交')
        toolbar(4,desktop_labels,top+24);wait(lambda:number('ui_modal')==2,'壁纸路径框实际就绪');q.text('SYS/WALL/TIDAL.SCB\n')
        wait(lambda:b'wallpaper=SYS/WALL/TIDAL.SCB' in compiler.file_content(disk,'SYS/THEME.CFG'),'SCB壁纸路径真实写回')
        commit();q.shot('08-custom-wallpaper')
        tab(2);top=number('settings_body_top');old_count=number('count')
        programs=['Add','Edit','Remove','Shortcut','Reload']
        toolbar(0,programs,top);wait(lambda:number('ui_modal')==1,'新菜单输入框实际就绪');q.text('Test hello\napps/hello.scx\n@auto\n')
        wait(lambda:number('count')==old_count+1,'真实注册菜单');assert b'Test hello|apps/hello.scx|@auto\n' in compiler.file_content(disk,'SYS/MENU.CFG')
        toolbar(1,programs,top)
        wait(lambda:number('ui_modal')==1,'菜单编辑框首帧真正提交后再键入')
        q.text('Changed hello\n');q.key('ret');q.key('ret')
        wait(lambda:b'Changed hello|apps/hello.scx|@auto\n' in compiler.file_content(disk,'SYS/MENU.CFG'),'菜单编辑保存')
        toolbar(3,programs,top)
        wait(lambda:number('ui_modal')==2,'快捷方式路径框实际就绪')
        q.text('DESK/VERIFY.LNK\n')
        wait(lambda:compiler.file_content(disk,'DESK/VERIFY.LNK')==b'Changed hello\napps/hello.scx\n@auto\n','真实彩色快捷方式文本')
        commit();q.shot('09-menu-shortcut')
        toolbar(2,programs,top);q.key('y');wait(lambda:number('count')==old_count,'明确移除菜单项')
        checks.append('鼠标六页、Aurora/Classic切换、SCB壁纸保存并实际呈现、菜单新增/编辑/移除和彩色快捷方式创建')
        tab(0);top=number('settings_body_top');cols=3 if number('UI_W')>=600 else 2;ww=(number('UI_W')-48-(cols-1)*8)//cols
        click(32+ww+8,top+32);click(216,top+144);click(32,top+192)
        wait(lambda:t.word(q.symbols()['gfx_width'])==640 and number('ui_scale')==200,'真实640/200%预览和布局')
        click(number('UI_W')-56,42);wait(lambda:number('tab')==1,'矮窗口鼠标翻页')
        click(120,number('UI_H')-42);wait(lambda:number('settings_scroll')>0,'实际裁剪视口滚动')
        commit();q.shot('10-small-window-200');q.key('esc')
        wait(lambda:t.word(q.symbols()['gfx_width'])==1024 and number('ui_scale')==100,'预览Esc回退')
        checks.append('640×480/200%真实模式预览、矮窗口翻页/裁剪滚动和Esc回退，不降色深/不改固定显示配置')
        # 保存的设置由真实冷启动读回；本轮不改默认数据盘。
        q.key('esc');idle();remaining=t.windows()[0];remaining_shell_pages=(remaining['cw']*remaining['ch']*4+4095)//4096
        wait(lambda:t.word(q.symbols()['pf_used'])==baseline[0]+t.word(q.symbols()['desktop_pages'])-baseline[1]+remaining_shell_pages-shell_pages,'Settings严格回收，扣除实际预览重排的常驻Shell画布')
        q.hmp('quit');proc.wait(timeout=10)
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    proc=t.launch('std',128,'settings',reuse=True)
    try:
        t.open_shell(True);q.text('run HOME/SET.SCX\n');w=wait(theme.window,'同盘冷启动Settings')
        wait(lambda:user_bytes(w,symbols['user_name'],32).split(b'\0')[0]==b'tester','实际内核用户名重读')
        wait(lambda:theme.user_word(w,symbols['env_count'])==3,'实际环境文件重读')
        wait(lambda:theme.user_word(w,symbols['ui_frames'])>=2,'冷启动首次布局');tab(3);q.shot('06-coldboot-settings');q.key('esc');idle()
        checks.append('同盘冷启动用户tester、PATH=/APPS:/BIN与KEEP/FAVORITE完整持久化，严格资源回收')
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    def vga_prepare(disk):prepare(disk);compiler.disk_put(disk,'HOME/SET.SCX',settings)
    proc=t.launch('cirrus',128,'settings-vga',vga_prepare)
    try:
        t.open_shell(False);baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/SET.SCX\n');w=wait(theme.window,'原生Settings兼容VGA')
        wait(lambda:number('ui_frames')>=2,'VGA实际布局')
        assert number('ui_scale')==50 and t.word(q.symbols()['gfx_width'])==320
        for index in range(6):tab(index)
        tab(3);q.shot('11-vga-settings-user');q.key('esc');idle()
        wait(lambda:t.word(q.symbols()['pf_used'])==baseline[0]+t.word(q.symbols()['desktop_pages'])-baseline[1],'VGA设置完整回收')
        checks.append('同一G2 Settings在cirrus VGA实际运行，六页鼠标导航、50%有效布局比例与退出回收保留')
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    assert inputs=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs},'验收期间输入发生变化'
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,checks=checks,
                native_sha256=hashlib.sha256(settings).hexdigest(),probe=list(values),
                limitation='已测std/128MB/1024/100%、640/200预览、cirrus VGA；五档×三档、压缩壁纸和完整M8仍待矩阵')
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)

if __name__=='__main__':main()
