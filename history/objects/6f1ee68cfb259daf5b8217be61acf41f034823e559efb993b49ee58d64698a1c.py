from pathlib import Path
import re
paths=['kernel/display.c','kernel/desktop.c','kernel/wm_native.inc','user/NUI.inc','user/IMAGE.inc','user/settings.c','user/lens.c','user/canvas.c','user/monitor.c']
for name in paths:
    p=Path(name);s=p.read_text(encoding='utf-8')
    if name.startswith('user/'):
        s=re.sub(r'\bstatic (?!inline)([\w *]+?\([^;{}]*\)\s*\{)',r'static inline \1',s)
    out=[];quote=None;comment=None;paren=0;i=0
    while i<len(s):
        c=s[i];next=s[i+1:i+2]
        if comment:
            out.append(c)
            if comment=='line' and c=='\n':comment=None
            if comment=='block' and c=='*' and next=='/':out.append('/');i+=1;comment=None
        elif quote:
            out.append(c)
            if c=='\\' and next:out.append(next);i+=1
            elif c==quote:quote=None
        elif c=='/' and next in ('/','*'):
            out.extend([c,next]);i+=1;comment='line' if next=='/' else 'block'
        elif c in ('"',"'"):quote=c;out.append(c)
        else:
            if c=='(':paren+=1
            elif c==')':paren-=1
            if c=='}' and out and out[-1]!='\n':out.append('\n')
            out.append(c)
            if c=='{':out.append('\n')
            if c=='}':
                j=i+1
                while j<len(s) and s[j] in ' \t':j+=1
                if j<len(s) and s[j] not in ';,)\n':out.append('\n');i=j-1
            if c==';' and paren==0:
                j=i+1
                while j<len(s) and s[j] in ' \t':j+=1
                if j<len(s) and s[j]!='\n':out.append('\n');i=j-1
        i+=1
    s=''.join(out)
    # 仅缩进新增语句行；原注释/空行保留，避免翻译或重排用户文档。
    lines=[];level=0;block=False
    for line in s.splitlines():
        t=line.strip()
        if not t:lines.append('');continue
        if block or t.startswith(('/*','*','//','#')):
            lines.append(line)
            if '/*' in t and '*/' not in t:block=True
            if '*/' in t:block=False
            continue
        code=re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'','',t)
        leading=1 if code.startswith('}') else 0
        lines.append('    '*max(0,level-leading)+t)
        level+=code.count('{')-code.count('}')
    p.write_text('\n'.join(lines)+'\n',encoding='utf-8')
