/* =====================================================================
 * mio：SC DEBUG，真实三环图形调试器。目标由 DBGEXEC 在首指令之前
 * 暂停；每个操作检查返回值与 STATUS，再读取内核保存的 CPU 现场。
 * 寄存器面板不以本调试器的寄存器冒充目标。断点由内核写 0xCC，
 * 单步由 CPU TF 异常返回；反汇编只是只读辅助，未知字节明确显示 db。
 *
 * F8 单步、F5 继续、F4 暂停、B 指定断点地址、U 撤断点、M 内存页，
 * F2 重启目标。内核按真实 owner 校验，退出调试器也结束专属目标。
 * 全屏目标盖住本窗口时，点击底部任务条 SC 按钮切回再暂停。
 *
 * M8草稿合同见docs/DEBUGGER.md：共享NUI保留，私有布局按窗口
 * 分为双面板或Code/Memory/Regs；旧接口和真实调试所有权不改。
 * 状态按PIT节流，事件即时刷新，避免静止窗口反复读同一现场。
 * 当前仅存build/m8-next，正式预装未替换，宿主检查不算原生验收。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
static char target_command[128],target_path[64],status[64],symbol_map[16384],function_label[36];
static u32 context[22],memory_address,break_address,last_event;
static u8 memory_bytes[128];
static int target_pid=-1,paused,have_context,memory_mode,have_break;
static int debug_menu,menu_x,menu_y,debug_y,debug_rows,debug_bottom,small_window;
static int register_mode,register_first,wide_layout,menu_columns,menu_step,menu_button,menu_width,menu_height;
static int memory_valid,refresh_tick=-1;
static u32 target_generation;
static volatile int debug_operations,debug_refreshes;
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
    /* 66改变操作数宽度；这里并未实现对应16位寄存器/立即数。
     * 跳过前缀再按32位解码会连下一条字节也吞掉，所以明确db。
     * 辅助视图只解释有完整长度合同的子集，CPU单步不依赖它。 */
    if(op==0x66)return 1;
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
        else if(p[1]==0xAF||p[1]==0xBE||p[1]==0xBF||p[1]==0xB6||p[1]==0xB7||(p[1]>=0x90&&p[1]<=0x9F)) {
            int m=modrm_size(p+2,max-2);
            if(!m) return 1;
            n=2+m;
            copy(text,p[1]==0xAF?"imul":p[1]==0xBE||p[1]==0xBF?"movsx":p[1]==0xB6||p[1]==0xB7?"movzx":"setcc",32);
        }else return 1;
    } else if(op==0x89||op==0x8B||op==0x8D||op==0x88||op==0x8A||op==0x81||op==0x83||op==0x69
        ||op==0x85||op==0x31||op==0x01||op==0x29||op==0x21||op==0x09||op==0x39||op==0xF7||op==0xFF||op==0xD3) {
        int m=modrm_size(p+1,max-1);
        if(!m) return 1;
        n=1+m;
        if(op==0x81||op==0x69) n+=4;
        if(op==0x83) n++;
        copy(text,op==0x89||op==0x8B||op==0x88||op==0x8A?"mov reg/mem":op==0x8D?"lea":op==0x85?"test":op==0x31?"xor":op==0xFF?"call/jmp":op==0xF7?"unary/div":op==0xD3?"shift cl":"arith reg/mem",32);
        /* F7/FF共享首操作码，但ModR/M中的reg域决定操作及长度。
         * F7 /0是带四字节立即数的TEST，不能按IDIV的两字节推进；
         * FF /0、/1、/6分别是INC、DEC、PUSH，也不能都标CALL。
         * 未实现的远调用/跳转和保留组项只显示一个db字节，实际
         * TF单步始终由CPU完成，辅助子集绝不冒充完整指令解码器。 */
        if(op==0xF7){
            int group=(p[1]>>3)&7;
            if(group==1){copy(text,"db",32);return 1;}
            if(group==0)n+=4;
            const char *names[]={"test imm32","db","not","neg","mul","imul","div","idiv"};
            copy(text,names[group],32);
        }else if(op==0xFF){
            int group=(p[1]>>3)&7;
            if(group==3||group==5||group==7){copy(text,"db",32);return 1;}
            const char *names[]={"inc","dec","call","db","jmp","db","push","db"};
            copy(text,names[group],32);
        }
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
static void refresh(int force)
{
    if(target_pid<0)return;
    int tick=sc_tick(),interval=paused?100:5;
    if(!force&&refresh_tick>=0&&tick-refresh_tick<interval)return;
    refresh_tick=tick;debug_refreshes++;
    u32 cpu[SC_CPU_WORDS];
    if(sc_cpu(cpu)||cpu[16+target_pid*6+2]!=target_generation){
        target_pid=-1;paused=memory_valid=0;copy(status,"Target identity lost / restart",sizeof(status));ui_followup=1;return;
    }
    int state=sc_status(target_pid);
    /* 已有两个公开调用间可能调度/复用，STATUS后再次核对代数；
     * 调试owner仍由内核检查，此处只防误报另一个任务为原目标。 */
    if(sc_cpu(cpu)||cpu[16+target_pid*6+2]!=target_generation){
        target_pid=-1;paused=memory_valid=0;copy(status,"Target identity lost / restart",sizeof(status));ui_followup=1;return;
    }
    int previous=paused;paused=state==0x40000001;
    if(previous!=paused)ui_followup=1;
    if(state!=0x40000000&&state!=0x40000001){
        char value[12];decimal(value,state);copy(status,"Target exited ",sizeof(status));append(status,value,sizeof(status));
        target_pid=-1;memory_valid=0;ui_followup=1;return;
    }
    if(paused){
        if(!sc_debug(target_pid,0,0,context,sizeof(context))){
            have_context=1;
            if(last_event!=context[21]){
                last_event=context[21];if(!memory_mode)memory_address=context[14];
                symbol_at(context[14]);ui_followup=1;
            }
        }else{have_context=0;copy(status,"Cannot read target context",sizeof(status));ui_followup=1;}
    }
    memory_valid=!sc_debug(target_pid,3,memory_address,memory_bytes,sizeof(memory_bytes));
}
static void menu_geometry(void)
{
    menu_step=ui_compact?26:36;menu_columns=9*menu_step+16>UI_H-16?3:1;
    menu_button=menu_columns==3?76:164;
    menu_width=menu_columns*(menu_button+8)+8;menu_height=((9+menu_columns-1)/menu_columns)*menu_step+16;
    menu_x=ui_clamp(menu_x,8,UI_W-menu_width-8);menu_y=ui_clamp(menu_y,8,UI_H-menu_height-8);
}
static void register_panel(int x,int y,int width,int rows)
{
    const char *names[]={"EAX","ECX","EDX","EBX","ESP","EBP","ESI","EDI","EIP","FLAGS"};
    const int indices[]={11,10,9,8,17,6,5,4,14,16};
    if(!have_context){ui_text(x,y,"No paused context",PAL_UI_MUTED);return;}
    if(register_first>10-rows)register_first=10-rows;
    if(register_first<0)register_first=0;
    for(int row=0;row<rows&&register_first+row<10;row++){
        int i=register_first+row;char value[9];ui_hex(value,context[indices[i]]);
        ui_text(x,y+row*20,names[i],PAL_UI_MUTED);ui_text(x+56,y+row*20,value,PAL_UI_TEXT);
    }
    /* 一行高的紧凑面板仍有两个小箭头，全部十个寄存器可逐个看。
     * 控件不依赖新增字形，不能用固定十行破坏640/200%布局。 */
    int xx=x+width-12,hh=rows*20,button=hh<32?8:16;
    for(int r=0;r<4;r++){ui_span(xx+4-r,y+2+r,1+r*2,PAL_UI_MUTED);ui_span(xx+1+r,y+hh-button+2+r,7-r*2,PAL_UI_MUTED);}
    if((ui_pressed&1)&&ui_hit(xx,y,12,button))ui_action=20;
    if((ui_pressed&1)&&ui_hit(xx,y+hh-button,12,button))ui_action=21;
}
static void data_panel(int x,int y,int width,int rows)
{
    ui_clip_set(x,y,width-16,rows*20);
    if(!memory_valid)ui_text(x,y,target_pid<0?"Target ended":"Unmapped memory",PAL_UI_ALERT);
    int at=0;
    for(int row=0;memory_valid&&row<rows&&at<128;row++){
        char text[32],value[9];int bytes=4;
        if(memory_mode){
            if(at+4>128)break;
            ui_hex(text,memory_address+(u32)at);append(text," ",sizeof(text));
            for(int i=0;i<4;i++){ui_hex(value,memory_bytes[at+i]);append(text,value+6,sizeof(text));append(text," ",sizeof(text));}
            ui_text(x,y+row*20,text,PAL_UI_TEXT);
        }else{
            bytes=disassemble(memory_bytes+at,128-at,memory_address+(u32)at,text);
            int selected=have_context&&memory_address+(u32)at==context[14];
            if(selected)ui_rect_rgb(x-2,y+row*20-1,width-14,20,ui_role(SC_THEME_SELECT));
            ui_hex(value,memory_address+(u32)at);ui_selected_text(x,y+row*20,value,selected);ui_selected_text(x+80,y+row*20,text,selected);
        }
        at+=bytes;
    }
    ui_clip_clear();
    /* 未映射页仍保留前后箭头：例如首段前一页自然可能无映射，
     * 读失败不应让鼠标失去返回原页的入口。只停止读取/解码数据，
     * 使用既有地址导航与下一次公开DEBUG READ，不伪造填零内存。 */
    int xx=x+width-12,hh=rows*20,button=hh<32?8:16;
    for(int r=0;r<4;r++){ui_span(xx+4-r,y+2+r,1+r*2,PAL_UI_MUTED);ui_span(xx+1+r,y+hh-button+2+r,7-r*2,PAL_UI_MUTED);}
    if((ui_pressed&1)&&ui_hit(xx,y,12,button))ui_action=22;
    if((ui_pressed&1)&&ui_hit(xx,y+hh-button,12,button))ui_action=23;
}
static void draw(void)
{
    small_window=UI_W<276||UI_H<172;debug_rows=0;
    if(small_window){
        debug_menu=0;ui_background(SC_THEME_FACE_ALT);int h=UI_H<22?UI_H:22;
        ui_button_box(4,(UI_H-h)/2,UI_W-8,h,UI_W>=104?"Enlarge":"+",0);
        if((ui_pressed&1)&&ui_hit(4,(UI_H-h)/2,UI_W-8,h))ui_action=98;
        return;
    }
    const int ids[]={1,2,3,4,5,6,7,8,9};const char *buttons[]={"Step","Run","Pause","Restart","Break","Unbreak","Memory","Code","Regs"};
    int pressed=ui_pressed;if(debug_menu)ui_pressed=0;
    ui_header("SC DEBUG",target_path);debug_y=ui_toolbar(ids,buttons,9,ui_compact?38:70);
    debug_bottom=UI_H-(ui_compact?22:30)-6;
    wide_layout=UI_W>=680&&debug_bottom-debug_y>=240;
    int heading=wide_layout||!ui_compact?28:0;
    int padding=ui_compact?2:8;
    debug_rows=(debug_bottom-debug_y-heading-padding)/20;if(debug_rows<0)debug_rows=0;
    if(debug_rows){
        int right=wide_layout?UI_W-280:UI_W;
        if(wide_layout||!register_mode){
            ui_panel(16,debug_y,right-32,heading+debug_rows*20+padding);
            if(heading)ui_text(24,debug_y+6,memory_mode?"MEMORY":"INSTRUCTIONS",PAL_UI_CYAN+7);
            data_panel(24,debug_y+heading+padding/2,right-48,debug_rows);
        }
        if(wide_layout||register_mode){
            int x=wide_layout?right:16,width=wide_layout?UI_W-right-16:UI_W-32;
            ui_panel(x,debug_y,width,heading+debug_rows*20+padding);
            if(heading)ui_text(x+8,debug_y+6,paused?"PAUSED REGISTERS":"LAST PAUSE",PAL_UI_CYAN+7);
            register_panel(x+8,debug_y+heading+padding/2,width-16,debug_rows);
        }
    }else ui_text(16,debug_y+2,"Enlarge to show target",PAL_UI_MUTED);
    /* 紧凑视图没有额外标题行，状态栏仍明确寄存器是当前暂停现场
     * 或上一次快照。长文案完整裁剪，不覆盖面板或跑出客户区。 */
    ui_clip_set(0,UI_H-(ui_compact?22:30),UI_W,ui_compact?22:30);
    char footer[96];
    if(!paused&&have_context&&(wide_layout||register_mode)){
        /* 紧凑单行寄存器没有标题，Continue的成功文案不能覆盖
         * LAST PAUSE：运行中显示的是先前真实现场而非实时寄存器。
         * 先标快照性质，再附操作结果；完整页脚仍按客户区裁剪。 */
        copy(footer,"LAST PAUSE / ",sizeof(footer));
        append(footer,status[0]?status:"F4 pause",sizeof(footer));
    }else copy(footer,status[0]?status:paused?"PAUSED / F8 step / F5 run":have_context?"LAST PAUSE / F4 pause":"Waiting for target",sizeof(footer));
    ui_footer(footer);ui_clip_clear();
    ui_pressed=pressed;
    if(debug_menu){
        menu_geometry();ui_panel(menu_x,menu_y,menu_width,menu_height);
        for(int i=0;i<9;i++){
            int x=menu_x+8+(i%menu_columns)*(menu_button+8),y=menu_y+8+(i/menu_columns)*menu_step;
            if(ui_compact)ui_small_control(i+1,x,y,menu_button,buttons[i],0);else ui_control(i+1,x,y,menu_button,buttons[i],0);
        }
    }
}
static void start(void)
{
    if(target_pid>=0) sc_debug(target_pid,6,0,0,0);
    u32 cpu[SC_CPU_WORDS];
    if(sc_cpu(cpu)){target_pid=-1;copy(status,"Cannot sample target identity",sizeof(status));debug_operations++;return;}
    target_pid=sc_dbgexec(target_command);
    target_generation=target_pid>=0?cpu[16+target_pid*6+2]+1:0;
    if(target_pid>=0&&!target_generation)target_generation=1;
    have_context=have_break=paused=memory_valid=0;
    last_event=0;
    memory_address=0x400000;
    copy(status,target_pid>=0?"Paused before first instruction":"Cannot load target",sizeof(status));
    char map[64];
    copy(map,target_path,64);
    if(length(map)>59) copy(map,"HOME/S3C",64);
    append(map,".map",64);
    int n=sc_read(map,symbol_map,sizeof(symbol_map)-1);
    if(n<0) symbol_map[0]=0;
    else symbol_map[n]=0;
    refresh_tick=-1;refresh(1);debug_operations++;
}
static void address_dialog(int breakpoint)
{
    char text[128];
    ui_hex(text,breakpoint&&have_context?context[14]:memory_address);
    draw();
    if(!ui_edit_text(text,sizeof(text),breakpoint?"Breakpoint address (hex)":"Memory address (hex)")){copy(status,"Cancelled",sizeof(status));debug_operations++;return;}
    u32 address;
    if(!hex_value(text,&address)) {copy(status,"Invalid hexadecimal address",sizeof(status));
        debug_operations++;return;}
    if(breakpoint) {int r=sc_debug(target_pid,4,address,0,0);
        result(r,"Breakpoint inserted");
        if(!r) {have_break=1;
            break_address=address;}}
    else {memory_address=address;
        memory_mode=1;register_mode=0;
        copy(status,"Memory view",sizeof(status));}
    refresh(1);debug_operations++;
}
int main(void)
{
    if(ui_open("SC DEBUG / mio")<0) return 1;
    sc_args(target_command,sizeof(target_command));
    if(!target_command[0]) copy(target_command,"apps/hello.scx",sizeof(target_command));
    int path_size=0;while(target_command[path_size]&&target_command[path_size]!=' ')path_size++;
    if(path_size>=64){copy(status,"Target path exceeds 63 bytes",sizeof(status));target_command[0]=target_path[0]=0;}
    else{for(int i=0;i<path_size;i++)target_path[i]=target_command[i];target_path[path_size]=0;}
    if(target_command[0])start();
    draw();
    ui_present();
    for(;;) {
        refresh(0);
        if(!ui_frame_due())continue;
        ui_pointer();draw();ui_present();
        int action=ui_action,key=(action||(debug_menu&&(ui_pressed&1)))?-1:sc_key();
        if(action==98){sc_window(ui_win,1);continue;}
        if(debug_menu&&(ui_pressed&1)&&!action){debug_menu=0;ui_followup=1;continue;}
        if(action>=1&&action<=9){const int keys[]={0x88,5,4,2,'b','u','m','i','r'};key=keys[action-1];debug_menu=0;}
        if(action==20&&register_first>0)register_first--;
        if(action==21&&register_first<9)register_first++;
        if(action==22&&memory_address>=16){memory_address-=16;refresh(1);}
        if(action==23){memory_address+=16;refresh(1);}
        if(!small_window&&(ui_pressed&2)){debug_menu=!debug_menu;menu_x=ui_x;menu_y=ui_y;}
        if(key==27){if(debug_menu){debug_menu=0;ui_followup=1;continue;}return 0;}
        if(key==2) start();
        else if(key==4&&target_pid>=0) result(sc_debug(target_pid,7,0,0,0),"Target paused");
        else if(key==5&&target_pid>=0) result(sc_debug(target_pid,2,0,0,0),"Continuing; taskbar switches back");
        else if(key==0x88&&target_pid>=0) result(sc_debug(target_pid,1,0,0,0),"One CPU instruction");
        else if((key=='b'||key=='B')&&target_pid>=0) address_dialog(1);
        else if((key=='u'||key=='U')&&have_break) {
            int code=sc_debug(target_pid,5,break_address,0,0);result(code,"Breakpoint removed");
            /* 运行中或失去目标时撤除可能失败；保留已有断点标记，
             * 不能界面称已撤却让内核的真实0xCC仍然留在目标代码。 */
            if(!code)have_break=0;
        }
        else if(key=='m'||key=='M') address_dialog(0);
        else if(key=='i'||key=='I') {memory_mode=register_mode=0;
            if(have_context) memory_address=context[14];
            status[0]=0;}
        else if(key=='r'||key=='R')register_mode=1;
        else if(key==0x80){if(register_mode){if(register_first>0)register_first--;}else if(memory_address>=16)memory_address-=16;}
        else if(key==0x81){if(register_mode){if(register_first<9)register_first++;}else memory_address+=16;}
        if(key>=0||action||(ui_pressed&2)) {
            if(key>=0||action>=20)refresh(1);
            if(key>=0&&key!=2&&key!='b'&&key!='B'&&key!='m'&&key!='M')debug_operations++;
            draw();
            ui_present();}
    }
}
