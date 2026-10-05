/* =====================================================================
 * mio：SC DEBUG，真实三环图形调试器。目标由 DBGEXEC 在首指令之前
 * 暂停；每个操作检查返回值与 STATUS，再读取内核保存的 CPU 现场。
 * 寄存器面板不以本调试器的寄存器冒充目标。断点由内核写 0xCC，
 * 单步由 CPU TF 异常返回；反汇编只是只读辅助，未知字节明确显示 db。
 *
 * F8 单步、F5 继续、F4 暂停、B 指定断点地址、U 撤断点、M 内存页，
 * F2 重启目标。内核按真实 owner 校验，退出调试器也结束专属目标。
 * 全屏目标盖住本窗口时，点击底部任务条 SC 按钮切回再暂停。
 * ===================================================================== */
#include "SCAPI.H"
#include "UI.inc"
static char target_command[128],target_path[64],status[40],symbol_map[16384],function_label[36];
static u32 context[22],memory_address,break_address,last_event;
static u8 memory_bytes[128];
static int target_pid=-1,paused,have_context,memory_mode,have_break;
static u32 bytes32(u8 *p)
{ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static int hex_value(const char *text,u32 *value)
{
    if(text[0]=='0'&&(text[1]=='x'||text[1]=='X')) text+=2;
    u32 v=0;
    int n=0;
    while(*text) {
        int c=*text++,d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if(d<0||n>=8) return 0;
        v=(v<<4)|(u32)d;
        n++;
    }
    *value=v;
    return n>0;
}
static void result(int code,const char *text)
{
    if(!code) copy(status,text,sizeof(status));
    else {copy(status,"DEBUG error ",sizeof(status));
        char n[12];
        decimal(n,code);
        append(status,n,sizeof(status));}
}
static int modrm_size(u8 *p,int max)
{
    if(!max) return 0;
    int m=p[0],mode=m>>6,rm=m&7,n=1;
    if(mode!=3&&rm==4) {
        if(n>=max) return 0;
        int sib=p[n++];
        if(mode==0&&(sib&7)==5) n+=4;
    }
    if(mode==0&&rm==5) n+=4;
    if(mode==1) n++;
    if(mode==2) n+=4;
    return n<=max?n:0;
}
static int disassemble(u8 *p,int max,u32 address,char *text)
{
    const char *regs[]={"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
    int op=p[0],n=1;
    char value[9];
    copy(text,"db",32);
    if(op==0x66&&max>1) {int k=disassemble(p+1,max-1,address+1,text);
        return k+1;}
    if(op>=0x50&&op<=0x5F) {copy(text,op<0x58?"push ":"pop ",32);
        append(text,regs[op&7],32);}
    else if(op>=0xB8&&op<=0xBF&&max>=5) {
        n=5;
        copy(text,"mov ",32);
        append(text,regs[op-0xB8],32);
        append(text,",",32);
        ui_hex(value,bytes32(p+1));
        append(text,value,32);
    } else if(op==0xE8||op==0xE9) {
        if(max<5) return 1;
        n=5;
        copy(text,op==0xE8?"call ":"jmp ",32);
        ui_hex(value,address+5+bytes32(p+1));
        append(text,value,32);
    } else if(op==0xEB&&max>=2) {n=2;
    copy(text,"jmp ",32);
    ui_hex(value,address+2+(int)(signed char)p[1]);
    append(text,value,32);}
    else if(op==0xCD&&max>=2) {n=2;
        copy(text,"int ",32);
        ui_hex(value,p[1]);
        append(text,value+6,32);}
    else if(op==0xCC) copy(text,"int3",32);
    else if(op==0xC3) copy(text,"ret",32);
    else if(op==0xC9) copy(text,"leave",32);
    else if(op==0x90) copy(text,"nop",32);
    else if(op==0x99) copy(text,"cdq",32);
    else if(op==0x05||op==0x3D||op==0x68) {n=5;
        copy(text,op==0x05?"add eax,imm":op==0x3D?"cmp eax,imm":"push imm",32);}
    else if(op==0x0F&&max>=2) {
        if(p[1]>=0x80&&p[1]<=0x8F) {n=6;
            copy(text,"jcc rel32",32);}
        else if(p[1]==0x0B) {n=2;
            copy(text,"ud2",32);}
        else {
            int m=modrm_size(p+2,max-2);
            if(!m) return 1;
            n=2+m;
            copy(text,p[1]==0xAF?"imul":p[1]==0xBE||p[1]==0xBF?"movsx":p[1]==0xB6||p[1]==0xB7?"movzx":"setcc",32);
        }
    } else if(op==0x89||op==0x8B||op==0x8D||op==0x88||op==0x8A||op==0x81||op==0x83||op==0x69
        ||op==0x85||op==0x31||op==0x01||op==0x29||op==0x21||op==0x09||op==0x39||op==0xF7||op==0xFF||op==0xD3) {
        int m=modrm_size(p+1,max-1);
        if(!m) return 1;
        n=1+m;
        if(op==0x81||op==0x69) n+=4;
        if(op==0x83) n++;
        copy(text,op==0x89||op==0x8B||op==0x88||op==0x8A?"mov reg/mem":op==0x8D?"lea":op==0x85?"test":op==0x31?"xor":op==0xFF?"call/jmp":op==0xF7?"unary/div":op==0xD3?"shift cl":"arith reg/mem",32);
    }
    if(n>max) {copy(text,"db",32);
        n=1;}return n;
}
static void symbol_at(u32 eip)
{
    char *p=symbol_map;
    u32 nearest=0;
    function_label[0]=0;
    while(*p&&*p!='\n') p++;
    if(*p) p++;
    while(*p) {
        char number[9];
        int i=0;
        while(*p&&*p!=' '&&i<8) number[i++]=*p++;
        number[i]=0;
        u32 address=0;
        int valid=hex_value(number,&address);
        if(*p==' ') p++;
        char *name=p;
        while(*p&&*p!='\n') p++;
        int size=(int)(p-name);
        if(*p) p++;
        if(valid&&address<=eip&&address>=nearest) {
            nearest=address;
            if(size>35) size=35;
            for(int j=0;j<size;j++) function_label[j]=name[j];
            function_label[size]=0;
        }
    }
}
static void refresh(void)
{
    if(target_pid<0) return;
    int state=sc_status(target_pid);
    paused=state==0x40000001;
    if(state!=0x40000000&&state!=0x40000001) {
        char value[12];
        decimal(value,state);
        copy(status,"Target exited ",sizeof(status));
        append(status,value,sizeof(status));
        target_pid=-1;
        return;
    }
    if(paused&&!sc_debug(target_pid,0,0,context,sizeof(context))) {
        have_context=1;
        if(last_event!=context[21]) {last_event=context[21];
            if(!memory_mode) memory_address=context[14];
            symbol_at(context[14]);}
    }
}
static void draw(void)
{
    ui_header("SC DEBUG",target_path);
    ui_panel(8,43,176,104);
    ui_panel(191,43,119,104);
    ui_text(15,49,memory_mode?"MEMORY":"INSTRUCTIONS",PAL_UI_CYAN+7);
    if(target_pid>=0) {
        if(!sc_debug(target_pid,3,memory_address,memory_bytes,sizeof(memory_bytes))) {
            if(memory_mode) {
                for(int row=0;row<7;row++) {
                    char text[24],hex[9];
                    ui_hex(text,memory_address+row*4);
                    append(text," ",sizeof(text));
                    for(int i=0;i<4;i++) {ui_hex(hex,memory_bytes[row*4+i]);
                        append(text,hex+6,sizeof(text));
                        append(text," ",sizeof(text));}
                    ui_text(15,61+row*10,text,PAL_UI_TEXT);
                }
            } else {
                int at=0;
                for(int row=0;row<5&&at<128;row++) {
                    char mnemonic[32],hex[9];
                    int bytes=disassemble(memory_bytes+at,128-at,memory_address+(u32)at,mnemonic);
                    ui_hex(hex,memory_address+(u32)at);
                    ui_text(15,61+row*16,hex,have_context&&memory_address+(u32)at==context[14]?PAL_UI_GOLD+7:PAL_UI_MUTED);
                    char short_op[12];
                    copy(short_op,mnemonic,sizeof(short_op));
                    ui_text(87,61+row*16,short_op,PAL_UI_TEXT);
                    at+=bytes;
                }
            }
        } else ui_text(15,65,"Unmapped memory",PAL_UI_ALERT);
    }
    char label[21];
    copy(label,function_label,sizeof(label));
    ui_text(15,137,label,PAL_UI_MUTED);
    const char *names[]={"EAX","ECX","EDX","EBX","ESP","EBP"};
    int indices[]={11,10,9,8,17,6};
    for(int i=0;i<6;i++) {char hex[9];
        ui_text(198,50+i*12,names[i],PAL_UI_MUTED);
        ui_hex(hex,have_context?context[indices[i]]:0);
        ui_text(231,50+i*12,hex,PAL_UI_TEXT);}
    ui_text(198,127,paused?"PAUSED":target_pid>=0?"RUNNING":"EXITED",paused?PAL_UI_GOLD+7:PAL_UI_CYAN+7);
    ui_footer(status[0]?status:"F8 step F5 run F4 pause B break M mem");
}
static void start(void)
{
    if(target_pid>=0) sc_debug(target_pid,6,0,0,0);
    target_pid=sc_dbgexec(target_command);
    have_context=have_break=0;
    last_event=0;
    memory_address=0x400000;
    copy(status,target_pid>=0?"Paused before first instruction":"Cannot load target",sizeof(status));
    refresh();
    char map[64];
    copy(map,target_path,64);
    if(length(map)>59) copy(map,"HOME/S3C",64);
    append(map,".map",64);
    int n=sc_read(map,symbol_map,sizeof(symbol_map)-1);
    if(n<0) symbol_map[0]=0;
    else symbol_map[n]=0;
}
static void address_dialog(int breakpoint)
{
    char text[64];
    ui_hex(text,breakpoint&&have_context?context[14]:memory_address);
    draw();
    if(!ui_edit_path(text,sizeof(text),breakpoint?"Breakpoint address (hex)":"Memory address (hex)")) return;
    u32 address;
    if(!hex_value(text,&address)) {copy(status,"Invalid hexadecimal address",sizeof(status));
        return;}
    if(breakpoint) {int r=sc_debug(target_pid,4,address,0,0);
        result(r,"Breakpoint inserted");
        if(!r) {have_break=1;
            break_address=address;}}
    else {memory_address=address;
        memory_mode=1;
        copy(status,"Memory view",sizeof(status));}
}
int main(void)
{
    if(ui_open("SC DEBUG / mio")<0) return 1;
    sc_args(target_command,sizeof(target_command));
    if(!target_command[0]) copy(target_command,"apps/hello.scx",sizeof(target_command));
    copy(target_path,target_command,64);
    for(int i=0;target_path[i];i++) if(target_path[i]==' ') {target_path[i]=0;
        break;}
    start();
    draw();
    ui_present();
    for(;;) {
        refresh();
        int key=sc_key();
        if(key==27) return 0;
        if(key==2) start();
        else if(key==4&&target_pid>=0) result(sc_debug(target_pid,7,0,0,0),"Target paused");
        else if(key==5&&target_pid>=0) result(sc_debug(target_pid,2,0,0,0),"Continuing; taskbar switches back");
        else if(key==0x88&&target_pid>=0) result(sc_debug(target_pid,1,0,0,0),"One CPU instruction");
        else if((key=='b'||key=='B')&&target_pid>=0) address_dialog(1);
        else if((key=='u'||key=='U')&&have_break) {result(sc_debug(target_pid,5,break_address,0,0),"Breakpoint removed");
            have_break=0;}
        else if(key=='m'||key=='M') address_dialog(0);
        else if(key=='i'||key=='I') {memory_mode=0;
            if(have_context) memory_address=context[14];
            status[0]=0;}
        else if(key==0x80&&memory_address>=16) memory_address-=16;
        else if(key==0x81) memory_address+=16;
        if(ui_frame_due()||key>=0) {refresh();
            draw();
            ui_present();}
    }
}
