/* =====================================================================
 * sheet.c —— SandSheet：表格计算器
 * 所属：SandCore_ExtraSoftware_Pack_1（ext/SoftwarePack1）
 *
 * 特性
 *   - 26 列（A..Z）× 128 行；单元格 = 公式 / 数字 / 文本
 *   - 公式：=A1*2+3、=SUM(A1:B9)、AVG/MIN/MAX/COUNT/ABS/SQRT/^
 *     double 精度、括号/一元负号/右结合幂
 *   - 循环引用检测（visit 旗标）；除零/未知函数/坏引用 -> #ERR
 *   - CSV 读写：公式以 "=..." 原文保存，往返无损；RFC4180 引号
 *   - F6 复制 / F7 粘贴 / F8 行插入 / F9 行删除
 *   - 数字右对齐、文本左对齐、错误高亮、网格线可配置
 *   - 全键盘 + 鼠标选择/翻页；小数位可配置（SHEETX.CFG）
 *
 * 工程约束（宿主 GCC freestanding，无 libgcc/libm/libc）：
 *   - 禁用 long long 的除法/取模/与 double 互转：i386 上这些会
 *     生成 __divdi3/__fixdfdi 之类的 libgcc 辅助符号，-nostdlib
 *     链接直接失败。数值格式化全部用 u32 拆分实现。
 *   - 网格一块 sc_alloc 大缓冲：启动一次分配，编辑路径零分配，
 *     退出由内核按任务代数回收。
 *   - 重算策略：编辑后全量重算（128×128 最坏情形毫秒级），
 *     不做依赖图——正确性优先，复杂度放到确定性上。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "exui_ext.inc"
#include "exutil.h"

/* ---------- 网格 ---------- */
#define SH_COLS 26
#define SH_ROWS 128
#define SH_SRC  72          /* 公式/文本原文容量 */
#define SH_DISP 26          /* 显示文本容量 */
#define SH_FILE_MAX (768*1024)

#define SH_F_FORMULA 1
#define SH_F_TEXT    2
#define SH_F_ERROR   4
#define SH_F_VISIT   8      /* 递归求值中：循环引用检测 */

typedef struct {
    char src[SH_SRC];       /* 原文：公式含 '='，文本原样 */
    char disp[SH_DISP];     /* 计算后的显示文本 */
    double val;
    u8 flags;
} sh_cell;

static sh_cell *sh_grid;
static int sh_sel_col,sh_sel_row;
static int sh_scroll_x,sh_scroll_y;
static int sh_dirty;
static int sh_decimals=4;
static int sh_gridlines=1;
static char sh_path[64];
static char sh_edit[SH_SRC+8];
static int sh_editing;
static char sh_status[64];
static char sh_clip[SH_SRC];    /* 单格剪贴板（F6/F7） */
static int sh_clip_valid;

static sh_cell *sh_at(int c,int r){return sh_grid+(u32)r*SH_COLS+c;}

/* ---------- 数值 <-> 文本（全部 32 位安全） ---------- */
static double sh_fabs(double v){return v<0?-v:v;}

/* 牛顿迭代开方：6 次迭代对 double 足够；v<=0 由调用方判错 */
static double sh_sqrt(double v)
{
    if(v<=0)return 0;
    double x=v>=1?v:v/4;
    for(int i=0;i<6&&x>0;i++)x=(x+v/x)/2;
    return v>=1?x:x*2;
}
static double sh_pow(double base,int e)
{
    double v=1;
    while(e){if(e&1)v*=base;base*=base;e>>=1;}
    return v;
}

/* 无符号十进制追加（可前导零补位），返回新长度 */
static int sh_put_u32(char *out,int cap,u32 v,int pad)
{
    char tmp[11];int n=0;
    do{tmp[n++]=(char)('0'+v%10);v/=10;}while(v);
    while(n<pad&&n<cap)tmp[n++]='0';               /* 前导零（百万段） */
    int used=0;
    while(n&&used<cap-1)out[used++]=tmp[--n];
    out[used]=0;
    return used;
}

/* double -> 文本。|v|>=1e12 显示 "1e+big"：更大的数在 26B 显示格
 * 里没有可读形态，宁可截短不画假精度。整数部分按"百万段"拆成
 * 两个 u32，规避 64 位除法；小数位按配置取位并四舍五入去尾零。 */
static void sh_fmt(char *out,int cap,double v)
{
    if(cap<8)return;
    out[0]=0;
    if(v!=v){copy(out,"NaN",cap);return;}          /* NaN 的唯一定义式 */
    double a=sh_fabs(v);
    char *p=out;
    if(v<0)*p++='-';
    if(a>=1e12){copy(p,"1e+big",cap-(int)(p-out));return;}
    u32 hi=(u32)(a/1e6);                           /* 百万段 */
    double rest=a-(double)hi*1e6;
    u32 lo=(u32)rest;
    if(!hi){
        p+=sh_put_u32(p,(int)(cap-(p-out)),lo,0);
    }else{
        /* 百万段 + 6 位前导零低位段，组合即完整整数 */
        int n=sh_put_u32(p,(int)(cap-(p-out)),hi,0);
        p+=n;
        int m=sh_put_u32(p,(int)(cap-(p-out)),lo,6);
        p+=m;
        if(!m){copy(out,"too big",cap);return;}    /* 容量不足宁缺毋滥 */
    }
    /* 小数部分：按 decimals+1 位取数字，末位四舍五入 */
    double frac=rest-(double)lo;
    if(sh_decimals>0&&p-out<cap-2){
        double scale=1;
        for(int i=0;i<=sh_decimals;i++)scale*=10;  /* 多取一位用于舍入 */
        int digits=(int)(frac*scale+0.5);
        if(digits>=(int)scale){digits=0;}          /* 进位：小数全 0.999.. */
        char tmp[17];
        for(int i=sh_decimals;i>=0;i--){tmp[i]=(char)('0'+digits%10);digits/=10;}
        tmp[sh_decimals+1]=0;
        int nz=sh_decimals;                        /* 去尾零 */
        while(nz>0&&tmp[nz]=='0')nz--;
        if(nz>=0&&tmp[0]!='\0'&&nz>0){
            *p++='.';
            for(int i=1;i<=nz;i++)*p++=tmp[i];
        }
    }
    *p=0;
    if(!out[0]||((out[0]=='-')&&!out[1]))ex_dec(out,cap,0);
}

/* 文本 -> double：数字 [.数字] [e[-]数字]；不是完整数字判失败 */
static int sh_parse_num(const char *s,double *out)
{
    if(!*s)return -1;
    double v=0;
    int seen=0;
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;seen=1;}
    if(*s=='.'){
        s++;
        double k=0.1;
        while(*s>='0'&&*s<='9'){v+=(*s-'0')*k;k*=0.1;s++;seen=1;}
    }
    if(!seen)return -1;
    if(*s=='e'||*s=='E'){
        s++;
        int eneg=0;
        if(*s=='+'||*s=='-'){eneg=*s=='-';s++;}
        if(!(*s>='0'&&*s<='9'))return -1;
        int e=0;
        while(*s>='0'&&*s<='9'){e=e*10+(*s-'0');if(e>300)e=300;s++;}
        double m=1;
        for(int i=0;i<e;i++)m*=10;
        v=eneg?v/m:v*m;
    }
    if(*s)return -1;                               /* 尾随垃圾 */
    *out=v;
    return 0;
}

/* ---------- 公式求值（递归下降） ---------- */
typedef struct {const char *at;sh_cell *cell;int error;} sh_parser;
static double sh_expr(sh_parser *p);

static sh_cell *sh_ref(int c,int r,sh_parser *p)
{
    if(c<0||c>=SH_COLS||r<0||r>=SH_ROWS){p->error=1;return 0;}
    return sh_at(c,r);
}
/* 递归求值引用格。visit 旗标防环：A1=B1、B1=A1 在第二次进入时
 * 直接判错，不会无限递归；求值结束清旗标。 */
static double sh_eval_cell(sh_cell *c,sh_parser *outer)
{
    if(!(c->flags&SH_F_FORMULA))return c->val;
    if(c->flags&SH_F_VISIT){outer->error=1;return 0;}
    sh_parser p;
    p.at=c->src+1;
    p.cell=c;
    p.error=0;
    c->flags|=SH_F_VISIT;
    double v=sh_expr(&p);
    c->flags&=(u8)~SH_F_VISIT;
    if(p.error){c->flags|=SH_F_ERROR;copy(c->disp,"#ERR",SH_DISP);return 0;}
    c->flags&=(u8)~SH_F_ERROR;
    c->val=v;
    sh_fmt(c->disp,SH_DISP,v);
    return v;
}

static double sh_atom(sh_parser *p)
{
    while(*p->at==' ')p->at++;
    char c=*p->at;
    if(c=='('){
        p->at++;
        double v=sh_expr(p);
        while(*p->at==' ')p->at++;
        if(*p->at!=')'){p->error=1;return 0;}
        p->at++;
        return v;
    }
    if(c>='0'&&c<='9'){
        double v=0,k=0.1;
        while(*p->at>='0'&&*p->at<='9'){v=v*10+(*p->at-'0');p->at++;}
        if(*p->at=='.'){
            p->at++;
            while(*p->at>='0'&&*p->at<='9'){v+=(*p->at-'0')*k;k*=0.1;p->at++;}
        }
        return v;
    }
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')){
        char name[8];
        int n=0;
        while(((*p->at>='A'&&*p->at<='Z')||(*p->at>='a'&&*p->at<='z'))&&n<7)
            name[n++]=*p->at++;
        name[n]=0;
        for(int i=0;i<n;i++)name[i]=ex_upper(name[i]);
        if(n==1&&p->at[0]>='0'&&p->at[0]<='9'){
            int col=name[0]-'A';
            int row=0;
            while(*p->at>='0'&&*p->at<='9')row=row*10+(*p->at++-'0');
            row--;
            sh_cell *a=sh_ref(col,row,p);
            if(!a)return 0;
            while(*p->at==' ')p->at++;
            if(*p->at==':'){p->error=1;return 0;}  /* 裸区间无意义 */
            return sh_eval_cell(a,p);
        }
        while(*p->at==' ')p->at++;
        if(*p->at!='('){p->error=1;return 0;}
        p->at++;
        double args[8];
        int argc=0;
        int r0=0,r1=-1,c0=0,c1=-1;
        for(;;){
            while(*p->at==' ')p->at++;
            if(argc<8){
                if(*p->at>='A'&&*p->at<='Z'){       /* 区间实参 */
                    int col=*p->at++-'A';
                    int row=0;
                    while(*p->at>='0'&&*p->at<='9')row=row*10+(*p->at++-'0');
                    row--;
                    c0=col;r0=row;
                    while(*p->at==' ')p->at++;
                    if(*p->at==':'){
                        p->at++;
                        if(*p->at>='A'&&*p->at<='Z'){
                            c1=*p->at++-'A';
                            int row2=0;
                            while(*p->at>='0'&&*p->at<='9')
                                row2=row2*10+(*p->at++-'0');
                            r1=row2-1;
                        }else{p->error=1;return 0;}
                    }else{c1=col;r1=row;}
                    argc++;
                }else args[argc++]=sh_expr(p);
            }else sh_expr(p);                       /* 超限参数照常消费 */
            while(*p->at==' ')p->at++;
            if(*p->at==','){p->at++;continue;}
            if(*p->at==')'){p->at++;break;}
            p->error=1;
            return 0;
        }
        if(p->error)return 0;
        if(equal(name,"SUM")||equal(name,"AVG")||equal(name,"MIN")||
           equal(name,"MAX")||equal(name,"COUNT")){
            double sum=0,mn=0,mx=0;
            int count=0;
            if(r1>=0&&c1>=0){
                if(r0>r1){int t=r0;r0=r1;r1=t;}
                if(c0>c1){int t=c0;c0=c1;c1=t;}
                for(int r=r0;r<=r1;r++)for(int cc=c0;cc<=c1;cc++){
                    sh_cell *x=sh_ref(cc,r,p);
                    if(!x)return 0;
                    double v=sh_eval_cell(x,p);
                    if(p->error)return 0;
                    if(x->flags&(SH_F_ERROR|SH_F_TEXT))continue;
                    if(!count){mn=mx=v;}
                    if(v<mn)mn=v;
                    if(v>mx)mx=v;
                    sum+=v;
                    count++;
                }
            }else{
                for(int i=0;i<argc;i++){
                    if(!count){mn=mx=args[i];}
                    if(args[i]<mn)mn=args[i];
                    if(args[i]>mx)mx=args[i];
                    sum+=args[i];
                    count++;
                }
            }
            if(equal(name,"SUM"))return sum;
            if(equal(name,"AVG"))return count?sum/count:0;
            if(equal(name,"MIN"))return count?mn:0;
            if(equal(name,"MAX"))return count?mx:0;
            return (double)count;
        }
        if(equal(name,"ABS")){
            if(argc!=1){p->error=1;return 0;}
            return sh_fabs(args[0]);
        }
        if(equal(name,"SQRT")){
            if(argc!=1||args[0]<0){p->error=1;return 0;}
            return sh_sqrt(args[0]);
        }
        p->error=1;
        return 0;
    }
    p->error=1;
    return 0;
}
static double sh_unary(sh_parser *p)
{
    while(*p->at==' ')p->at++;
    if(*p->at=='-'){p->at++;return -sh_unary(p);}
    if(*p->at=='+'){p->at++;return sh_unary(p);}
    return sh_atom(p);
}
static double sh_power(sh_parser *p)
{
    double base=sh_unary(p);
    while(*p->at==' ')p->at++;
    if(*p->at=='^'){
        p->at++;
        double e=sh_power(p);                        /* 右结合 */
        if(e!=(double)(long long)e||sh_fabs(e)>255){p->error=1;return 0;}
        return sh_pow(base,(int)e);
    }
    return base;
}
static double sh_term(sh_parser *p)
{
    double v=sh_power(p);
    for(;;){
        while(*p->at==' ')p->at++;
        if(*p->at=='*'){p->at++;v*=sh_power(p);}
        else if(*p->at=='/'){
            p->at++;
            double d=sh_power(p);
            if(d==0){p->error=1;return 0;}
            v/=d;
        }else return v;
    }
}
static double sh_expr(sh_parser *p)
{
    double v=sh_term(p);
    for(;;){
        while(*p->at==' ')p->at++;
        if(*p->at=='+'){p->at++;v+=sh_term(p);}
        else if(*p->at=='-'){p->at++;v-=sh_term(p);}
        else return v;
    }
}

/* ---------- 重算与提交 ---------- */
/* 编辑后的唯一重算入口：全量重算并重建显示文本。
 * 文本格的 disp 在提交时生成，重算跳过它们（否则会把文本
 * 覆盖成 0）。 */
static void sh_recalc(void)
{
    for(int r=0;r<SH_ROWS;r++)for(int c=0;c<SH_COLS;c++){
        sh_cell *x=sh_at(c,r);
        x->flags&=(u8)~SH_F_VISIT;
        if(x->flags&SH_F_FORMULA)sh_eval_cell(x,0);
    }
}

static void sh_set_cell(sh_cell *x,const char *text)
{
    x->flags&=(u8)~(SH_F_FORMULA|SH_F_TEXT|SH_F_ERROR);
    if(!text[0]){x->src[0]=0;x->val=0;x->disp[0]=0;return;}
    copy(x->src,text,SH_SRC);
    if(text[0]=='='){
        x->flags|=SH_F_FORMULA;
        sh_parser p;p.at=text+1;p.cell=x;p.error=0;
        x->flags|=SH_F_VISIT;
        double v=sh_expr(&p);
        x->flags&=(u8)~SH_F_VISIT;
        if(p.error){x->flags|=SH_F_ERROR;copy(x->disp,"#ERR",SH_DISP);}
        else{x->val=v;sh_fmt(x->disp,SH_DISP,v);}
    }else{
        double v;
        if(sh_parse_num(text,&v)==0){x->val=v;sh_fmt(x->disp,SH_DISP,v);}
        else{x->flags|=SH_F_TEXT;copy(x->disp,text,SH_DISP);}
    }
}

static void sh_commit(void)
{
    sh_cell *x=sh_at(sh_sel_col,sh_sel_row);
    int changed=length(sh_edit)!=length(x->src)||!equal(sh_edit,x->src);
    sh_set_cell(x,sh_edit);
    if(changed){sh_dirty=1;sh_recalc();}
}

/* ---------- 行插入/删除 ----------
 * 行块整体平移，尾部溢出的行丢弃——表格是有限网格，这个语义
 * 要在文档里说明而不是含糊其辞。 */
static void sh_insert_row(int at)
{
    if(at<0||at>=SH_ROWS)return;
    for(int r=SH_ROWS-1;r>at;r--)
        for(int c=0;c<SH_COLS;c++)*sh_at(c,r)=*sh_at(c,r-1);
    for(int c=0;c<SH_COLS;c++){
        sh_cell *x=sh_at(c,at);
        x->src[0]=0;x->disp[0]=0;x->val=0;x->flags=0;
    }
    sh_dirty=1;
    sh_recalc();
    ui_followup=1;
}
static void sh_delete_row(int at)
{
    if(at<0||at>=SH_ROWS)return;
    for(int r=at;r<SH_ROWS-1;r++)
        for(int c=0;c<SH_COLS;c++)*sh_at(c,r)=*sh_at(c,r+1);
    for(int c=0;c<SH_COLS;c++){
        sh_cell *x=sh_at(c,SH_ROWS-1);
        x->src[0]=0;x->disp[0]=0;x->val=0;x->flags=0;
    }
    sh_dirty=1;
    sh_recalc();
    ui_followup=1;
}

/* ---------- 文件 ---------- */
static void sh_load(const char *path)
{
    u8 *buf=sc_alloc(SH_FILE_MAX);
    if(!buf){copy(sh_status,"Out of memory",sizeof(sh_status));ui_followup=1;return;}
    u32 info[2];
    int truncated=0;
    if(sc_stat(path,info)==0&&info[0]==1&&info[1]>(u32)SH_FILE_MAX)
        truncated=1;                               /* SC_READ 按 max 截断 */
    int n=sc_read(path,buf,truncated?SH_FILE_MAX:SH_FILE_MAX-1);
    if(n<0){sc_free(buf);copy(sh_status,"Cannot read file",sizeof(sh_status));
        ui_followup=1;return;}
    buf[n]=0;
    for(int r=0;r<SH_ROWS;r++)for(int c=0;c<SH_COLS;c++){
        sh_cell *x=sh_at(c,r);
        x->src[0]=0;x->disp[0]=0;x->val=0;x->flags=0;
    }
    char *p=(char *)buf;
    int row=0,skipped=0;
    while(*p&&row<SH_ROWS){
        char *line=p;
        while(*p&&*p!='\n')p++;
        if(*p)*p++=0;
        char *fields[SH_COLS+1];
        int fc=ex_csv_split(line,fields,SH_COLS+1);
        if(fc>SH_COLS)skipped=1;
        for(int c=0;c<fc&&c<SH_COLS;c++){
            sh_cell *x=sh_at(c,row);
            if(!fields[c][0])continue;
            copy(x->src,fields[c],SH_SRC);
            if(fields[c][0]=='=')x->flags|=SH_F_FORMULA;
            else{
                double v;
                if(sh_parse_num(fields[c],&v)==0)x->val=v;
                else x->flags|=SH_F_TEXT;
            }
        }
        row++;
    }
    while(*p){while(*p&&*p!='\n')p++;if(*p)p++;skipped=1;}
    sc_free(buf);
    copy(sh_path,path,sizeof(sh_path));
    sh_dirty=0;
    sh_sel_col=sh_sel_row=0;
    sh_scroll_x=sh_scroll_y=0;
    sh_recalc();
    copy(sh_status,truncated?"Loaded (file too big, truncated)":
        skipped?"Loaded (extra cells dropped)":"Loaded",sizeof(sh_status));
    ui_followup=1;
}

static void sh_save(const char *path)
{
    u32 cap=SH_ROWS*SH_COLS*16+4096;
    u8 *buf=sc_alloc(cap);
    if(!buf){copy(sh_status,"Out of memory",sizeof(sh_status));ui_followup=1;return;}
    u32 used=0;
    for(int r=0;r<SH_ROWS;r++){
        const char *fields[SH_COLS];
        char slots[SH_COLS][SH_SRC];
        for(int c=0;c<SH_COLS;c++){
            copy(slots[c],sh_at(c,r)->src,SH_SRC);
            fields[c]=slots[c];
        }
        char line[SH_COLS*SH_SRC+SH_COLS+2];
        int n=ex_csv_join(line,sizeof(line),fields,SH_COLS);
        if(n<0){sc_free(buf);
            copy(sh_status,"Serialize failed",sizeof(sh_status));
            ui_followup=1;return;}
        if(used+(u32)n+2>cap){sc_free(buf);
            copy(sh_status,"Overflow",sizeof(sh_status));ui_followup=1;return;}
        for(int i=0;i<n;i++)buf[used++]=(u8)line[i];
    }
    buf[used]=0;
    int n=sc_write(path,buf,(int)used);
    sc_free(buf);
    if(n!=(int)used){copy(sh_status,"Write failed",sizeof(sh_status));
        ui_followup=1;return;}
    copy(sh_path,path,sizeof(sh_path));
    sh_dirty=0;
    copy(sh_status,"Saved",sizeof(sh_status));
    ui_followup=1;
}

static void sh_open_dialog(void)
{
    char path[64];
    copy(path,sh_path,sizeof(path));
    if(!ui_edit_path(path,sizeof(path),"Open CSV"))return;
    if(sh_dirty&&!ui_confirm("Discard changes?","Current sheet is not saved."))
        return;
    sh_load(path);
}
static void sh_save_as(void)
{
    char path[64];
    copy(path,sh_path,sizeof(path));
    if(!ui_edit_path(path,sizeof(path),"Save CSV as"))return;
    sh_save(path);
}

/* ---------- 导航与编辑 ---------- */
static void sh_move(int dc,int dr)
{
    sh_sel_col=ex_clamp(sh_sel_col+dc,0,SH_COLS-1);
    sh_sel_row=ex_clamp(sh_sel_row+dr,0,SH_ROWS-1);
    if(sh_sel_col<sh_scroll_x)sh_scroll_x=sh_sel_col;
    if(sh_sel_row<sh_scroll_y)sh_scroll_y=sh_sel_row;
    ui_followup=1;
}
static void sh_begin_edit(void)
{
    copy(sh_edit,sh_at(sh_sel_col,sh_sel_row)->src,sizeof(sh_edit));
    sh_editing=1;
    ui_followup=1;
}
static void sh_edit_key(int key)
{
    int n=length(sh_edit);
    if(key==10){sh_commit();sh_editing=0;sh_move(0,1);}
    else if(key==9){sh_commit();sh_editing=0;sh_move(1,0);}
    else if(key==27){sh_editing=0;ui_followup=1;}
    else if(key==8){if(n){sh_edit[n-1]=0;ui_followup=1;}}
    else if(key>=32&&key<127&&n+1<(int)sizeof(sh_edit)){
        sh_edit[n++]=(char)key;
        sh_edit[n]=0;
        ui_followup=1;
    }
}

/* ---------- 绘制 ---------- */
#define SH_HDR_W 40
#define SH_COL_W 56
#define SH_ROW_H 20

static void sh_draw(void)
{
    ui_header(sh_dirty?"SandSheet *":"SandSheet",
              sh_path[0]?sh_path:"unsaved sheet");
    int tools_y=ui_compact?30:60;
    ui_small_control(1,16,tools_y-2,52,"Open",0);
    ui_small_control(2,74,tools_y-2,52,"Save",0);
    ui_small_control(3,132,tools_y-2,64,"Save as",0);
    ui_small_control(4,202,tools_y-2,60,"Clear",0);
    /* 公式栏 */
    int bar_y=tools_y+22;
    ui_rect(16,bar_y,UI_W-32,26,PAL_UI_INK);
    char ref[12];
    ref[0]=(char)('A'+sh_sel_col);
    char num[8];ex_dec(num,sizeof(num),sh_sel_row+1);
    copy(ref+1,num,12);
    ui_text(22,bar_y+5,ref,PAL_UI_CYAN+7);
    const char *bar=sh_editing?sh_edit:sh_at(sh_sel_col,sh_sel_row)->src;
    ui_text(76,bar_y+5,bar,sh_editing?PAL_UI_TEXT:PAL_UI_MUTED);
    if(sh_editing&&((sc_tick()/40)&1))
        ui_rect(80+ex_utf8_width(sh_edit),bar_y+4,8,18,PAL_UI_CYAN+6);
    /* 网格 */
    int gx=16,gy=bar_y+30;
    int footer_h=ui_compact?22:30;
    int gh=UI_H-footer_h-gy-24;
    int gw=UI_W-32;
    int cols=(gw-SH_HDR_W)/SH_COL_W;
    int rows=(gh-SH_ROW_H)/SH_ROW_H;
    if(cols<1)cols=1;
    if(rows<1)rows=1;
    ui_rect(gx,gy,gw,gh,PAL_UI_INK);
    ui_rect(gx,gy,gw,SH_ROW_H,PAL_UI_PANEL);
    ui_rect(gx,gy,SH_HDR_W,gh,PAL_UI_PANEL);
    for(int c=0;c<cols&&(sh_scroll_x+c)<SH_COLS;c++){
        char label[2];
        label[0]=(char)('A'+sh_scroll_x+c);label[1]=0;
        ui_text(gx+SH_HDR_W+c*SH_COL_W+SH_COL_W/2-4,gy+3,label,PAL_UI_MUTED);
    }
    for(int r=0;r<rows&&(sh_scroll_y+r)<SH_ROWS;r++){
        char rn[8];ex_dec(rn,sizeof(rn),sh_scroll_y+r+1);
        ui_text(gx+SH_HDR_W-ex_text_w(rn)-4,gy+SH_ROW_H+r*SH_ROW_H+3,
                rn,PAL_UI_MUTED);
    }
    /* 先画网格线再画内容：线在空格上可见、不切断文字观感 */
    if(sh_gridlines){
        for(int c=1;c<=cols;c++)
            ui_rect(gx+SH_HDR_W+c*SH_COL_W-1,gy+SH_ROW_H,1,
                    gh-SH_ROW_H,PAL_UI_LINE);
        for(int r=1;r<=rows;r++)
            ui_rect(gx+SH_HDR_W,gy+SH_ROW_H+r*SH_ROW_H-1,gw-SH_HDR_W,1,
                    PAL_UI_LINE);
    }
    for(int r=0;r<rows&&(sh_scroll_y+r)<SH_ROWS;r++)
        for(int c=0;c<cols&&(sh_scroll_x+c)<SH_COLS;c++){
            sh_cell *x=sh_at(sh_scroll_x+c,sh_scroll_y+r);
            int cx=gx+SH_HDR_W+c*SH_COL_W,cy=gy+SH_ROW_H+r*SH_ROW_H;
            char show[SH_DISP];
            ex_text_cut(show,sizeof(show),x->disp,SH_COL_W-8);
            if(x->flags&SH_F_ERROR)
                ui_text(cx+4,cy+3,show,PAL_UI_ALERT);
            else if(x->flags&SH_F_FORMULA)
                ui_text(cx+4,cy+3,show,PAL_UI_TEXT);   /* 公式结果左对齐 */
            else if(x->flags&SH_F_TEXT)
                ui_text(cx+4,cy+3,show,PAL_UI_TEXT);
            else{
                int w8=ex_text_w(show);                /* 数字右对齐 */
                ui_text(ex_max(cx+2,cx+SH_COL_W-w8-4),cy+3,show,PAL_UI_TEXT);
            }
        }
    /* 选中框最后画，压住网格线 */
    {
        int c=sh_sel_col-sh_scroll_x,r=sh_sel_row-sh_scroll_y;
        if(c>=0&&c<cols&&r>=0&&r<rows){
            int cx=gx+SH_HDR_W+c*SH_COL_W,cy=gy+SH_ROW_H+r*SH_ROW_H;
            ui_rect(cx,cy,SH_COL_W,1,PAL_UI_CYAN+7);
            ui_rect(cx,cy+SH_ROW_H-1,SH_COL_W,1,PAL_UI_CYAN+7);
            ui_rect(cx,cy,1,SH_ROW_H,PAL_UI_CYAN+7);
            ui_rect(cx+SH_COL_W-1,cy,1,SH_ROW_H,PAL_UI_CYAN+7);
        }
    }
    /* 鼠标选择（只在网格有效区内命中） */
    if(ui_focus&&(ui_pressed&1)&&ui_hit(gx,gy,gw,gh)
       &&ui_x>=gx+SH_HDR_W&&ui_y>=gy+SH_ROW_H){
        int c=(ui_x-gx-SH_HDR_W)/SH_COL_W;
        int r=(ui_y-gy-SH_ROW_H)/SH_ROW_H;
        if(c<cols&&r<rows&&(sh_scroll_x+c)<SH_COLS&&(sh_scroll_y+r)<SH_ROWS){
            sh_sel_col=sh_scroll_x+c;
            sh_sel_row=sh_scroll_y+r;
            ui_followup=1;
        }
    }
    ui_small_control(5,gx,gy+gh-22,52,"PgUp",0);
    ui_small_control(6,gx+56,gy+gh-22,52,"PgDn",0);
    ui_small_control(7,gx+112,gy+gh-22,52,"PgLf",0);
    ui_small_control(8,gx+168,gy+gh-22,52,"PgRt",0);
    /* 页脚 = 状态消息 + 当前格统计 */
    sh_cell *x=sh_at(sh_sel_col,sh_sel_row);
    char foot[96];
    if(sh_status[0])copy(foot,sh_status,64);
    else if(x->flags&SH_F_FORMULA){
        copy(foot,"=",sizeof(foot));
        append(foot,x->src+1,sizeof(foot));
        append(foot," -> ",sizeof(foot));
        append(foot,x->disp,sizeof(foot));
    }else copy(foot,x->src[0]?x->src:"Ready. F2 edit, F3 save, F4 open.",
               sizeof(foot));
    ui_footer(foot);
}

static void sh_action(int id)
{
    switch(id){
    case 1:sh_open_dialog();break;
    case 2:sh_path[0]?sh_save(sh_path):sh_save_as();break;
    case 3:sh_save_as();break;
    case 4:{
        sh_cell *x=sh_at(sh_sel_col,sh_sel_row);
        x->src[0]=0;x->disp[0]=0;x->val=0;x->flags=0;
        sh_dirty=1;
        sh_recalc();
        ui_followup=1;
        break;
    }
    case 5:sh_scroll_y=ex_max(0,sh_scroll_y-16);ui_followup=1;break;
    case 6:sh_scroll_y=ex_min(SH_ROWS-1,sh_scroll_y+16);ui_followup=1;break;
    case 7:sh_scroll_x=ex_max(0,sh_scroll_x-8);ui_followup=1;break;
    case 8:sh_scroll_x=ex_min(SH_COLS-1,sh_scroll_x+8);ui_followup=1;break;
    }
}

int main(void)
{
    if(ui_open("SandSheet / SandCore ext")<0)return 1;
    sh_grid=sc_alloc(sizeof(sh_cell)*SH_COLS*SH_ROWS);
    if(!sh_grid)return 1;
    for(int r=0;r<SH_ROWS;r++)for(int c=0;c<SH_COLS;c++){
        sh_cell *x=sh_at(c,r);
        x->src[0]=0;x->disp[0]=0;x->val=0;x->flags=0;
    }
    char cfg[2048];
    if(sc_read("HOME/SHEETX.CFG",cfg,sizeof(cfg)-1)>=0){
        cfg[sizeof(cfg)-1]=0;
        char v[16];
        if(ex_cfg_get(cfg,"decimals",v,sizeof(v))==0){
            int d;
            if(ex_parse_int(v,&d)==0)sh_decimals=ex_clamp(d,0,6);
        }
        if(ex_cfg_get(cfg,"gridlines",v,sizeof(v))==0)
            sh_gridlines=!equal(v,"0");
    }
    ui_followup=1;
    for(;;){
        if(!ui_frame_due_interval(100))continue;   /* 静止页每秒兜底刷新光标 */
        ui_pointer();
        sh_draw();
        ui_present();
        int key=ui_action?-1:sc_key();
        if(ui_action)sh_action(ui_action);
        if(sh_editing&&key>=0){sh_edit_key(key);continue;}
        if(key>=0){
            if(key==27)return 0;
            else if(key==0x80)sh_move(0,-1);
            else if(key==0x81)sh_move(0,1);
            else if(key==0x82)sh_move(-1,0);
            else if(key==0x83)sh_move(1,0);
            else if(key==10){sh_edit[0]=0;sh_editing=1;sh_edit_key(10);}
            else if(key==9){sh_edit[0]=0;sh_editing=1;sh_edit_key(9);}
            else if(key==2)sh_begin_edit();                        /* F2 */
            else if(key==3)sh_path[0]?sh_save(sh_path):sh_save_as();
            else if(key==4)sh_open_dialog();                       /* F4 */
            else if(key==5)sh_save_as();                           /* F5 */
            else if(key==6){                            /* F6 复制 */
                copy(sh_clip,sh_at(sh_sel_col,sh_sel_row)->src,SH_SRC);
                sh_clip_valid=1;
                copy(sh_status,"Copied",sizeof(sh_status));
                ui_followup=1;
            }
            else if(key==7){                            /* F7 粘贴 */
                if(sh_clip_valid){
                    copy(sh_edit,sh_clip,SH_SRC);
                    sh_commit();
                    copy(sh_status,"Pasted",sizeof(sh_status));
                }
                ui_followup=1;
            }
            else if(key==0x88)sh_insert_row(sh_sel_row);  /* F8 行插入 */
            else if(key==0x89)sh_delete_row(sh_sel_row);  /* F9 行删除 */
            else if(key>=32&&key<127){                  /* 直接输入覆盖 */
                sh_edit[0]=(char)key;sh_edit[1]=0;
                sh_editing=1;
                ui_followup=1;
            }
        }
    }
}
