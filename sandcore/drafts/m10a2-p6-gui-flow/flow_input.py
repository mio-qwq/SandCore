"""仅薄封装既有ScuiInput；导入不会启动VM或连接客体。"""

_PUNCTUATION={
    ' ':'spc','/':'slash','.':'dot','-':'minus','_':'shift-minus',
    '(':'shift-9',')':'shift-0','+':'shift-equal','*':'shift-8',
    '=':'equal','|':'shift-backslash',"'":'apostrophe','"':'shift-apostrophe',
    '\\':'backslash',':':'shift-semicolon',';':'semicolon',
    ',':'comma','<':'shift-comma','>':'shift-dot','!':'shift-1',
    '?':'shift-slash','&':'shift-7','%':'shift-5','$':'shift-4',
    '[':'bracket_left',']':'bracket_right','{':'shift-bracket_left','}':'shift-bracket_right',
    '\n':'ret','\t':'tab',
}

def ascii_keys(text):
    """先校验整段，避免不支持字符导致输入半个路径；只用于非秘密字段。"""
    result=[]
    for value in text:
        if 'a'<=value<='z' or '0'<=value<='9':result.append(value)
        elif 'A'<=value<='Z':result.append('shift-'+value.lower())
        elif value in _PUNCTUATION:result.append(_PUNCTUATION[value])
        else:raise ValueError('仅支持非秘密US-ASCII输入，不能用此方法输入密码或Unicode')
    return result

class FlowInput:
    def __init__(self,inputs,harness):
        self.inputs,self.harness=inputs,harness

    def keys(self,*values):
        for value in values:self.inputs.key(value)

    def type_ascii(self,text):
        for value in ascii_keys(text):self.inputs.key(value)

    def replace(self,point,text):
        keys=ascii_keys(text)
        self.inputs.click(*point)
        self.inputs.key('ctrl-a')
        for value in keys:self.inputs.key(value)

    def drag(self,start,end):
        # 真正DOWN/MOVE/UP；先越过5px阈值。跨外国窗口时desktop本身
        # 决定转FILE transfer，不注入Drop/command，也不伪造私有状态。
        self.inputs.move(*start)
        self.inputs.button('left',True)
        try:
            self.inputs.move(start[0]+16,start[1])
            self.inputs.move(*end)
        finally:
            self.inputs.button('left',False)

    def screen(self,label):
        return self.harness.screen(label)

def client_point(window,x,y):
    """window取当前只读ScWindowInfo值，x/y为当前物理客户区点，非DIP。"""
    width=window['width'];height=window['height']
    cw=window['client_width'];ch=window['client_height']
    if not (0<=x<cw and 0<=y<ch):raise ValueError('点必须位于当前客户区')
    border=(width-cw)//2;title=height-ch-border
    return window['x']+border+x,window['y']+title+y
