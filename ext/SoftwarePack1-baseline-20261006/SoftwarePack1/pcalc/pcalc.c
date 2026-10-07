/* =====================================================================
 * pcalc.c —— PCalc：表达式 / 程序员计算器
 * 所属：SandCore_ExtraSoftware_Pack_1（ext/SoftwarePack1）
 *
 * 特性
 *   - 32 位整数表达式：+ - * / % & | ^ ~ << >> 比较 逻辑 三目
 *   - 字面量：十进制 / 0xHEX / 0bBIN / 0oOCT / 'c' 字符
 *   - 变量赋值 name=expr、ANS 自动保存、历史环点击回填
 *   - 函数：ABS MIN MAX SQRT GCD LCM POW ROL ROR
 *   - DEC/HEX/OCT/BIN 四进制同屏；错误定位到输入中的字节偏移
 *   - 屏幕键盘（可配置）+ 全键盘操作；鼠标键盘双通道
 *
 * 语义立场：程序员计算器坚持"整数即位模式"——加减乘按补码回绕
 * 是特性而非错误；只有除零、INT_MIN/-1、移位越界报错（x86 会抛
 * #DE，不能放行成未定义行为）。
 *
 * 界面基于共享库 libex：NUI（平台原生界面片段）+ exui_ext 扩展
 * 控件 + exutil（表达式引擎）。布局全部用 NUI 逻辑单位，窗口
 * 任意尺寸/缩放自适应，紧凑模式自动让位。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "exui_ext.inc"
#include "exutil.h"

/* ---------- 容量与状态 ---------- */
#define PC_INPUT   96      /* 输入行容量（含 NUL 预算） */
#define PC_HIST    16      /* 历史环深度 */
#define PC_VARS    16      /* 用户变量上限 */
#define PC_ID_HIST 100     /* 历史行控件 id 基址 */
#define PC_ID_PAD  200     /* 键盘控件 id 基址（行*10+列） */
#define PC_ID_CLEAR 1
#define PC_ID_BKSP 2

static char pc_input[PC_INPUT];
static int  pc_len;
static int  pc_result;                 /* 最近成功结果的位模式（即 ANS） */
static int  pc_has_result;
static char pc_error[48];              /* 非空 = 显示错误行 */
static int  pc_err_pos;                /* 错误字节偏移 */
static char pc_hist[PC_HIST][PC_INPUT];
static int  pc_hist_result[PC_HIST];
static int  pc_hist_count,pc_hist_head;/* head=下一个写入位 */
static char pc_var_name[PC_VARS][16];
static int  pc_var_value[PC_VARS];
static int  pc_var_count;
static int  pc_keypad;                 /* 配置：屏幕键盘开关 */
static char pc_oct[13];                /* OCT 回显（内核无八进制格式化） */

/* ---------- 变量表 ---------- */
static int pc_lookup(const char *name,int *out)
{
    if(equal(name,"ANS")){*out=pc_result;return 1;}
    for(int i=0;i<pc_var_count;i++)
        if(equal(name,pc_var_name[i])){*out=pc_var_value[i];return 1;}
    return 0;
}
static int pc_store(const char *name,int value)
{
    if(equal(name,"ANS")){pc_result=value;pc_has_result=1;return 1;}
    for(int i=0;i<pc_var_count;i++)
        if(equal(name,pc_var_name[i])){pc_var_value[i]=value;return 1;}
    if(pc_var_count>=PC_VARS||length(name)>=16)return 0;
    copy(pc_var_name[pc_var_count],name,16);
    pc_var_value[pc_var_count]=value;
    pc_var_count++;
    return 1;
}

/* ---------- 函数表 ----------
 * 全部整数语义；负数 SQRT、负指数 POW 返回"无此结果"，交给
 * 引擎变成语法错误位置，用户看到的是同一套错误提示。 */
static int pc_call(const char *name,const int *args,int argc,int *out)
{
    if(equal(name,"ABS")){
        if(argc!=1)return 0;
        *out=args[0]<0?(int)(0u-(u32)args[0]):args[0];
        return 1;
    }
    if(equal(name,"MIN")||equal(name,"MAX")){
        if(argc!=2)return 0;
        *out=equal(name,"MIN")?ex_min(args[0],args[1]):ex_max(args[0],args[1]);
        return 1;
    }
    if(equal(name,"SQRT")){
        if(argc!=1||args[0]<0)return 0;
        *out=(int)ex_sqrt_u32((u32)args[0]);
        return 1;
    }
    if(equal(name,"GCD")||equal(name,"LCM")){
        if(argc!=2)return 0;
        u32 a=(u32)ex_abs(args[0]),b=(u32)ex_abs(args[1]);
        *out=(int)(equal(name,"GCD")?ex_gcd_u32(a,b):ex_lcm_u32(a,b));
        return 1;
    }
    if(equal(name,"POW")){
        if(argc!=2||args[1]<0)return 0;
        u32 base=(u32)args[0],v=1;
        int e=args[1];
        while(e){if(e&1)v*=base;base*=base;e>>=1;}   /* 回绕即位模式 */
        *out=(int)v;
        return 1;
    }
    if(equal(name,"ROL")||equal(name,"ROR")){
        /* x86 的循环移位没有 C 运算符；n 取模 32 合成 */
        if(argc!=2||args[1]<0)return 0;
        u32 v=(u32)args[0];int n=args[1]&31;
        if(!n)*out=args[0];
        else *out=(int)(equal(name,"ROL")
                        ?(u32)(v<<n)|(v>>(32-n))
                        :(u32)(v>>n)|(v<<(32-n)));
        return 1;
    }
    return 0;
}

/* ---------- 求值 ---------- */
/* 顶层赋值识别："标识符 = 表达式"且 '=' 后不是 '='。
 * 手工扫描标识符边界，a==b、a<=b 不会被误判成赋值。 */
static void pc_evaluate(void)
{
    pc_error[0]=0;
    if(!pc_len)return;
    const char *s=pc_input;
    int i=0;
    while(i<pc_len&&((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||
                     (s[i]>='0'&&s[i]<='9')||s[i]=='_'))i++;
    int is_assign=i>0&&!(s[0]>='0'&&s[0]<='9')
        &&i<pc_len&&s[i]=='='&&(i+1>=pc_len||s[i+1]!='=');
    const char *expr=is_assign?s+i+1:s;
    if(!expr[0]){copy(pc_error,"Empty expression",sizeof(pc_error));return;}
    int value,err_pos;
    if(ex_eval(expr,pc_lookup,pc_call,&value,&err_pos)){
        copy(pc_error,"Syntax error",sizeof(pc_error));
        pc_err_pos=err_pos+(is_assign?i+1:0);
        if(pc_err_pos>=pc_len)pc_err_pos=pc_len-1;
        if(pc_err_pos<0)pc_err_pos=0;
        return;
    }
    if(is_assign){
        char name[16];
        int n=i<15?i:15;
        for(int k=0;k<n;k++)name[k]=s[k];
        name[n]=0;
        if(!pc_store(name,value)){
            copy(pc_error,"Variable table full",sizeof(pc_error));
            return;
        }
    }
    pc_result=value;
    pc_has_result=1;
    int dup=0;
    for(int k=0;k<pc_hist_count;k++)
        if(equal(pc_hist[k],pc_input))dup=1;
    if(!dup){
        copy(pc_hist[pc_hist_head],pc_input,PC_INPUT);
        pc_hist_result[pc_hist_head]=value;
        pc_hist_head=(pc_hist_head+1)%PC_HIST;
        if(pc_hist_count<PC_HIST)pc_hist_count++;
    }
}

/* ---------- 输入通道 ---------- */
static void pc_edit(int key)
{
    if(key==8){
        if(pc_len){pc_len--;pc_input[pc_len]=0;}
    }else if(key==10){
        pc_evaluate();
    }else if(key>=32&&key<127&&pc_len+1<PC_INPUT){
        pc_input[pc_len++]=(char)key;
        pc_input[pc_len]=0;
    }
    ui_followup=1;
}

static void pc_hist_load(int idx)
{
    copy(pc_input,pc_hist[idx],PC_INPUT);
    pc_len=length(pc_input);
    ui_followup=1;
}

/* ---------- 屏幕键盘 ----------
 * 键位表绘制与动作共用一份：绘制遍历标签，动作按 id 反查。
 * 函数键键帽已带左括号，插入文本即键帽。 */
static const char *pc_pad[4][10]={
    {"7","8","9","/","<<","4","5","6","*","&&"},
    {"1","2","3","-","||","0","(",")","+","^^"},
    {"A","B","C","D","E","F","%","^","|","~"},
    {"SQRT(","ABS(","MIN(","MAX(","GCD(","LCM(",
     "POW(","ROL(","ROR(","="}
};

/* ---------- 绘制 ---------- */
static void pc_draw(void)
{
    ui_header("PCalc","32-bit programmer calculator / SandCore ext");

    /* 输入行：纸面 + 光标。光标用 tick 相位闪烁，需要周期帧。 */
    int in_y=ui_compact?32:64;
    int in_h=34;
    ui_rect(16,in_y,UI_W-32,in_h,PAL_UI_INK);
    ui_rect(16,in_y,UI_W-32,1,PAL_UI_LINE);
    ui_rect(16,in_y+in_h-1,UI_W-32,1,PAL_UI_LINE);
    int caret=24+ex_utf8_width(pc_input);
    if((sc_tick()/40)&1)ui_rect(caret,in_y+4,8,in_h-8,PAL_UI_CYAN+6);
    ui_clip_set(16,in_y,UI_W-32,in_h);
    ui_text(24,in_y+9,pc_input,PAL_UI_TEXT);
    ui_clip_clear();

    /* 结果 / 错误行 */
    int info_y=in_y+in_h+6;
    if(pc_error[0]){
        ui_text(24,info_y,pc_error,PAL_UI_ALERT);
        char pos[12];ex_dec(pos,sizeof(pos),pc_err_pos);
        int w8=ex_text_w(pc_error);
        ui_text(24+w8+12,info_y,"@",PAL_UI_MUTED);
        ui_text(24+w8+20,info_y,pos,PAL_UI_MUTED);
        /* 定位符画在错误行下方对应字节处（键盘输入必为 ASCII） */
        ui_rect(24+pc_err_pos*8,info_y+18,8,2,PAL_UI_ALERT);
    }else{
        char line[48];
        copy(line,"= ",sizeof(line));
        char num[16];ex_dec(num,sizeof(num),pc_result);
        append(line,num,sizeof(line));
        ui_text(24,info_y,line,pc_has_result?PAL_UI_TEXT:PAL_UI_MUTED);
    }

    /* 四进制面板 */
    int base_y=info_y+22;
    int panel_h=ui_compact?60:72;
    ui_panel(16,base_y,UI_W-32,panel_h);
    int col_w=(UI_W-64)/2;
    if(pc_has_result){
        u32 v=(u32)pc_result;
        char buf[40];
        ex_hex(buf,8,v);
        ui_text(24,base_y+8,"HEX",PAL_UI_MUTED);
        ui_text(60,base_y+8,buf,PAL_UI_CYAN+7);
        ex_bin(buf,sizeof(buf),v,32);
        ui_text(24,base_y+28,"BIN",PAL_UI_MUTED);
        ui_text(60,base_y+28,buf,PAL_UI_TEXT);
        ex_dec(buf,sizeof(buf),pc_result);
        ui_text(24+col_w,base_y+8,"DEC",PAL_UI_MUTED);
        ui_text(24+col_w+36,base_y+8,buf,PAL_UI_TEXT);
        for(int i=10;i>=0;i--){pc_oct[i]=(char)('0'+(v&7));v>>=3;}
        pc_oct[11]=0;
        ui_text(24+col_w,base_y+28,"OCT",PAL_UI_MUTED);
        ui_text(24+col_w+36,base_y+28,pc_oct,PAL_UI_TEXT);
    }else{
        ui_text(24,base_y+8,"Type an expression, Enter to evaluate",
                PAL_UI_MUTED);
        ui_text(24,base_y+28,"x=5  ANS*2+1  0xFF&0x0F  SQRT(1024)",
                PAL_UI_MUTED);
    }

    /* 屏幕键盘（可选；占底部 4 行 + 操作行） */
    int footer_h=ui_compact?22:30;
    int pad_top=UI_H-footer_h-(pc_keypad?5*26:0)-4;
    if(pc_keypad){
        int cell=(UI_W-32)/10-4;
        for(int r=0;r<4;r++)for(int c=0;c<10;c++){
            ui_small_control(PC_ID_PAD+r*10+c,16+c*(cell+4),
                             pad_top+r*26,cell,pc_pad[r][c],0);
        }
        ui_small_control(PC_ID_CLEAR,16,pad_top+4*26,60,"C",0);
        ui_small_control(PC_ID_BKSP,82,pad_top+4*26,60,"BKSP",0);
    }

    /* 历史环（键盘让位后的剩余区域） */
    int list_y=base_y+panel_h+8;
    int list_bottom=pc_keypad?pad_top-6:UI_H-footer_h-4;
    int list_h=list_bottom-list_y;
    if(list_h>=40){
        ui_text(16,list_y,"History",PAL_UI_MUTED);
        int row_y=list_y+18;
        int rows=(list_h-18)/20;
        int shown=ex_min(rows,pc_hist_count);
        for(int i=0;i<shown;i++){
            int idx=(pc_hist_head-1-i+PC_HIST)%PC_HIST;
            if(ex_row(PC_ID_HIST+i,16,row_y,UI_W-32,20))pc_hist_load(idx);
            char num[16];ex_dec(num,sizeof(num),pc_hist_result[idx]);
            char label[PC_INPUT];
            ex_text_cut(label,sizeof(label),pc_hist[idx],
                        UI_W-48-ex_text_w(num));
            ui_text(24,row_y+2,label,PAL_UI_TEXT);
            ui_text(UI_W-24-ex_text_w(num),row_y+2,num,PAL_UI_CYAN+7);
            row_y+=20;
        }
    }
    ui_footer(pc_keypad?"Enter evaluate / F1 keypad / Esc clear error"
                        :"F1 keypad / Enter evaluate / Esc clear error");
}

/* ---------- 控件分派 ---------- */
static void pc_action(int id)
{
    if(id==PC_ID_CLEAR){
        pc_len=0;pc_input[0]=0;pc_error[0]=0;
        ui_followup=1;
        return;
    }
    if(id==PC_ID_BKSP){pc_edit(8);return;}
    if(id>=PC_ID_HIST&&id<PC_ID_HIST+PC_HIST){
        pc_hist_load(id-PC_ID_HIST);
        return;
    }
    if(id>=PC_ID_PAD&&id<PC_ID_PAD+40){
        const char *label=pc_pad[(id-PC_ID_PAD)/10][(id-PC_ID_PAD)%10];
        for(const char *p=label;*p&&pc_len+1<PC_INPUT;p++)
            pc_input[pc_len++]=*p;
        pc_input[pc_len]=0;
        ui_followup=1;
    }
}

/* ---------- 主循环 ---------- */
int main(void)
{
    if(ui_open("PCalc / SandCore ext")<0)return 1;
    /* 配置可缺省：安装器写默认值，手工运行回退内置默认。
     * 只在启动读一次；改配置重开应用，避免每帧文件 IO。 */
    char cfg[2048];
    if(sc_read("HOME/PCALX.CFG",cfg,sizeof(cfg)-1)>=0){
        cfg[sizeof(cfg)-1]=0;
        char v[16];
        pc_keypad=!(ex_cfg_get(cfg,"keypad",v,sizeof(v))==0&&equal(v,"0"));
    }else pc_keypad=1;
    ui_followup=1;
    for(;;){
        /* 周期 4 tick：只为了光标闪烁；无输入无变化时仍然让出 */
        if(!ui_frame_due_interval(4))continue;
        ui_pointer();
        pc_draw();
        ui_present();
        int key=ui_action?-1:sc_key();
        if(key==27){
            /* 第一层 Esc 只清错误；再按才退出，防误触丢输入 */
            if(pc_error[0]){pc_error[0]=0;ui_followup=1;continue;}
            return 0;
        }
        if(key==1)pc_keypad=!pc_keypad;        /* F1 切换键盘 */
        if(ui_action)pc_action(ui_action);
        if(key>=0)pc_edit(key);
    }
}
