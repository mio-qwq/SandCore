/* SandSheet 1.1：有界公式求值、无损 CSV 和统一逻辑坐标布局。
 * 原来的求值会吞掉错误、重复计算依赖，载入后数字/文本没有显示。
 * 本版缓存每次重算的结果；CSV 全部检查通过后才替换当前工作表。 */
#include "SCAPI.H"
#include "NUI.inc"
#include "exui_ext.inc"
#include "exutil.h"
#include "../common/pack_ui.inc"

#define SH_COLS 26
#define SH_ROWS 128
#define SH_SRC 72
#define SH_DISP 26
#define SH_FILE_MAX (768*1024)
#define SH_F_FORMULA 1
#define SH_F_TEXT 2
#define SH_F_ERROR 4
#define SH_F_VISIT 8
#define SH_F_DONE 16
typedef struct {char src[SH_SRC],disp[SH_DISP];double val;u8 flags;} sh_cell;
static sh_cell *sh_grid;
static int sh_sel_col,sh_sel_row,sh_scroll_x,sh_scroll_y,sh_dirty;
static int sh_decimals=4,sh_gridlines=1,sh_editing,sh_depth,sh_help;
static int sh_visible_cols=1,sh_visible_rows=1,sh_col_width=80;
static char sh_path[64],sh_edit[SH_SRC],sh_status[96],sh_clip[SH_SRC];
static int sh_clip_valid,sh_undo_valid,sh_undo_c,sh_undo_r;
static char sh_undo[SH_SRC];
static sh_cell *sh_at(int c,int r){return sh_grid+r*SH_COLS+c;}
static double sh_fabs(double v){return v<0?-v:v;}
static int sh_finite(double v)
{union {double d;u32 w[2];} bits;bits.d=v;return (bits.w[1]&0x7FF00000u)!=0x7FF00000u;}
static double sh_sqrt(double v)
{
    if(v<=0)return 0;
    double scale=1;
    while(v>4){v/=4;scale*=2;}
    while(v<1){v*=4;scale/=2;}
    double x=2;
    for(int i=0;i<12;i++)x=(x+v/x)/2;
    return x*scale;
}
static double sh_pow(double b,int e)
{
    int neg=e<0;if(neg)e=-e;
    double v=1;while(e){if(e&1)v*=b;b*=b;e>>=1;}
    return neg?1/v:v;
}
static int sh_put_u32(char *out,int cap,u32 v,int pad)
{
    char tmp[16];int n=0;
    do{tmp[n++]=(char)('0'+v%10);v/=10;}while(v);
    while(n<pad&&n<15)tmp[n++]='0';
    int used=0;while(n&&used<cap-1)out[used++]=tmp[--n];out[used]=0;return used;
}
/* 在整数段生成之前舍入，正确处理 0.99999 -> 1 和负数近零。
 * 科学记数保留实际尾数，不再把所有大数显示成同一个 1e+big。 */
static void sh_fmt(char *out,int cap,double v)
{
    if(cap<2)return;out[0]=0;
    if(!sh_finite(v)){copy(out,"#ERR",cap);return;}
    char tmp[64],*p=tmp;double a=sh_fabs(v);int exponent=0,scientific=0;
    if(a>=1e12||(a>0&&a<0.000001)){
        scientific=1;
        while(a>=10){a/=10;exponent++;}
        while(a<1){a*=10;exponent--;}
    }
    u32 scale=1;for(int i=0;i<sh_decimals;i++)scale*=10;
    a+=0.5/(double)scale;
    if(scientific&&a>=10){a/=10;exponent++;}
    if(v<0&&a>=1.0/(double)scale)*p++='-';
    u32 hi=(u32)(a/1e6),lo=(u32)(a-(double)hi*1e6);
    if(hi){p+=sh_put_u32(p,16,hi,0);p+=sh_put_u32(p,16,lo,6);}
    else p+=sh_put_u32(p,16,lo,0);
    u32 frac=(u32)((a-(double)hi*1e6-(double)lo)*(double)scale);
    if(frac>=scale)frac=scale-1;
    if(sh_decimals&&frac){
        *p++='.';char *start=p;p+=sh_put_u32(p,16,frac,sh_decimals);
        while(p>start&&p[-1]=='0')p--;
    }
    if(scientific){*p++='e';char e[12];ex_dec(e,sizeof(e),exponent);for(char *q=e;*q;) *p++=*q++;}
    *p=0;copy(out,tmp,cap);
}
static void sh_space(const char **s){while(**s==' '||**s=='\t')(*s)++;}
/* 同一数字扫描器供普通格与公式使用；指数无数字/越界显式拒绝。 */
static int sh_number(const char **at,double *out)
{
    const char *s=*at;double v=0,k=0.1;int seen=0;
    while(*s>='0'&&*s<='9'){v=v*10+(*s++-'0');seen=1;}
    if(*s=='.'){s++;while(*s>='0'&&*s<='9'){v+=(*s++-'0')*k;k*=0.1;seen=1;}}
    if(!seen)return -1;
    if(*s=='e'||*s=='E'){
        s++;int neg=0,e=0;
        if(*s=='+'||*s=='-'){neg=*s=='-';s++;}
        if(*s<'0'||*s>'9')return -1;
        while(*s>='0'&&*s<='9'){if(e>308)return -1;e=e*10+(*s++-'0');}
        if(e>308)return -1;
        for(int i=0;i<e;i++)v=neg?v/10:v*10;
    }
    if(!sh_finite(v))return -1;*at=s;*out=v;return 0;
}
static int sh_parse_num(const char *s,double *out)
{
    sh_space(&s);int neg=0;if(*s=='+'||*s=='-'){neg=*s=='-';s++;}
    double v;if(sh_number(&s,&v))return -1;sh_space(&s);if(*s)return -1;
    *out=neg?-v:v;return 0;
}
typedef struct {const char *at;int error,nest;} sh_parser;
static double sh_expr(sh_parser *p);
static double sh_unary(sh_parser *p);
static double sh_eval_cell(sh_cell *c,sh_parser *outer);
static int sh_reference(const char **at,int *col,int *row)
{
    const char *s=*at;char ch=ex_upper(*s);
    if(ch<'A'||ch>'Z'||s[1]<'0'||s[1]>'9')return 0;
    *col=ch-'A';s++;int r=0;
    while(*s>='0'&&*s<='9'){if(r>SH_ROWS)return -1;r=r*10+(*s++-'0');}
    if(r<1||r>SH_ROWS)return -1;*row=r-1;*at=s;return 1;
}
static void sh_aggregate(double v,double *sum,double *mn,double *mx,int *count)
{if(!*count)*mn=*mx=v;if(v<*mn)*mn=v;if(v>*mx)*mx=v;*sum+=v;(*count)++;}
static double sh_atom(sh_parser *p)
{
    sh_space(&p->at);
    if(++p->nest>SH_SRC){p->error=1;return 0;}
    double result=0;
    if(*p->at=='('){
        p->at++;result=sh_expr(p);sh_space(&p->at);
        if(*p->at==')')p->at++;else p->error=1;
    }else if((*p->at>='0'&&*p->at<='9')||*p->at=='.'){
        if(sh_number(&p->at,&result))p->error=1;
    }else{
        int col,row;const char *start=p->at;
        int ref=sh_reference(&p->at,&col,&row);
        if(ref<0)p->error=1;
        else if(ref)result=sh_eval_cell(sh_at(col,row),p);
        else{
            char name[8];int n=0;
            while(ex_upper(*p->at)>='A'&&ex_upper(*p->at)<='Z'){
                if(n<7)name[n++]=ex_upper(*p->at);else p->error=1;p->at++;
            }
            name[n]=0;sh_space(&p->at);
            int aggregate=equal(name,"SUM")||equal(name,"AVG")||equal(name,"MIN")||equal(name,"MAX")||equal(name,"COUNT");
            if(p->at==start||*p->at!='('){p->error=1;p->nest--;return 0;}
            p->at++;int argc=0,count=0;double sum=0,mn=0,mx=0,arg=0;
            sh_space(&p->at);
            if(*p->at!=')')for(;;){
                sh_space(&p->at);const char *saved=p->at,*look=p->at;
                int c0,r0,c1,r1;int isref=sh_reference(&look,&c0,&r0);sh_space(&look);
                if(isref==1&&*look==':'){
                    look++;sh_space(&look);
                    if(!aggregate||sh_reference(&look,&c1,&r1)!=1){p->error=1;break;}
                    p->at=look;
                    if(c0>c1){int t=c0;c0=c1;c1=t;}if(r0>r1){int t=r0;r0=r1;r1=t;}
                    for(int r=r0;r<=r1&&!p->error;r++)for(int c=c0;c<=c1&&!p->error;c++){
                        sh_cell *x=sh_at(c,r);double v=sh_eval_cell(x,p);
                        if(x->src[0]&&!(x->flags&SH_F_TEXT))sh_aggregate(v,&sum,&mn,&mx,&count);
                    }
                }else{
                    arg=sh_expr(p);
                    int skip=isref==1&&p->at==look&&(!sh_at(c0,r0)->src[0]||(sh_at(c0,r0)->flags&SH_F_TEXT));
                    if(!skip)sh_aggregate(arg,&sum,&mn,&mx,&count);
                }
                argc++;sh_space(&p->at);
                if(p->error||p->at==saved)break;
                if(*p->at==','){p->at++;continue;}break;
            }
            if(*p->at==')')p->at++;else p->error=1;
            if(aggregate){
                result=equal(name,"SUM")?sum:equal(name,"AVG")?(count?sum/count:0):equal(name,"MIN")?(count?mn:0):equal(name,"MAX")?(count?mx:0):(double)count;
            }else if(equal(name,"ABS")&&argc==1)result=sh_fabs(arg);
            else if(equal(name,"SQRT")&&argc==1&&arg>=0)result=sh_sqrt(arg);
            else p->error=1;
        }
    }
    p->nest--;return result;
}
static double sh_power(sh_parser *p)
{
    double b=sh_atom(p);sh_space(&p->at);
    if(*p->at=='^'){
        p->at++;double e=sh_unary(p);
        if(!sh_finite(e)||sh_fabs(e)>255||e!=(double)(int)e||(b==0&&e<0)){p->error=1;return 0;}
        b=sh_pow(b,(int)e);
    }return b;
}
static double sh_unary(sh_parser *p)
{sh_space(&p->at);if(*p->at=='-'||*p->at=='+'){int neg=*p->at++=='-';double v=sh_unary(p);return neg?-v:v;}return sh_power(p);}
static double sh_term(sh_parser *p)
{
    double v=sh_unary(p);
    for(;;){sh_space(&p->at);char op=*p->at;if(op!='*'&&op!='/')return v;p->at++;
        double b=sh_unary(p);if(op=='/'&&b==0){p->error=1;return 0;}v=op=='*'?v*b:v/b;}
}
static double sh_expr(sh_parser *p)
{
    double v=sh_term(p);
    for(;;){sh_space(&p->at);char op=*p->at;if(op!='+'&&op!='-')return v;p->at++;
        double b=sh_term(p);v=op=='+'?v+b:v-b;}
}
static double sh_eval_cell(sh_cell *c,sh_parser *outer)
{
    if(c->flags&SH_F_DONE){if((c->flags&SH_F_ERROR)&&outer)outer->error=1;return c->val;}
    if((c->flags&SH_F_VISIT)||sh_depth>=96){if(outer)outer->error=1;return 0;}
    c->flags|=SH_F_VISIT;sh_depth++;
    double v=0;int error=0;
    if(c->flags&SH_F_FORMULA){
        sh_parser p;p.at=c->src+1;p.error=p.nest=0;v=sh_expr(&p);sh_space(&p.at);
        error=p.error||*p.at||!sh_finite(v);
    }else if(c->src[0]&&!(c->flags&SH_F_TEXT))error=sh_parse_num(c->src,&v)!=0;
    sh_depth--;c->flags&=(u8)~(SH_F_VISIT|SH_F_ERROR);c->flags|=SH_F_DONE;
    c->val=error?0:v;
    if(error){c->flags|=SH_F_ERROR;copy(c->disp,"#ERR",SH_DISP);if(outer)outer->error=1;}
    else if(c->flags&SH_F_TEXT){ui_copy_utf8(c->disp,c->src,SH_DISP);for(int i=0;c->disp[i];i++)if(c->disp[i]=='\r'||c->disp[i]=='\n'||c->disp[i]=='\t')c->disp[i]=' ';}
    else if(c->src[0])sh_fmt(c->disp,SH_DISP,v);else c->disp[0]=0;
    return c->val;
}
static void sh_set_cell(sh_cell *c,const char *s)
{
    copy(c->src,s,SH_SRC);c->flags=0;c->val=0;c->disp[0]=0;
    if(s[0]=='=')c->flags=SH_F_FORMULA;
    else if(s[0]&&sh_parse_num(s,&c->val))c->flags=SH_F_TEXT;
}
static void sh_recalc(void)
{
    sh_depth=0;
    for(int i=0;i<SH_COLS*SH_ROWS;i++)sh_grid[i].flags&=(u8)~(SH_F_DONE|SH_F_VISIT|SH_F_ERROR);
    for(int i=0;i<SH_COLS*SH_ROWS;i++)sh_eval_cell(sh_grid+i,0);
}
static void sh_commit(void)
{
    sh_cell *c=sh_at(sh_sel_col,sh_sel_row);
    if(!equal(sh_edit,c->src)){
        copy(sh_undo,c->src,SH_SRC);sh_undo_c=sh_sel_col;sh_undo_r=sh_sel_row;sh_undo_valid=1;
        sh_set_cell(c,sh_edit);sh_dirty=1;sh_recalc();sh_status[0]=0;
    }
    ui_followup=1;
}
static void sh_undo_cell(void)
{
    if(!sh_undo_valid)return;
    sh_set_cell(sh_at(sh_undo_c,sh_undo_r),sh_undo);sh_sel_col=sh_undo_c;sh_sel_row=sh_undo_r;
    sh_dirty=1;sh_undo_valid=0;sh_recalc();copy(sh_status,"Last cell edit undone",sizeof(sh_status));ui_followup=1;
}
/* 插删行同步调整公式引用。被删行及超出末行的引用变为 #REF，
 * 不能静默指向移动后另一笔数据。有限网格插入前确认末行不会丢失。 */
static void sh_adjust_refs(int at,int insert)
{
    for(int i=0;i<SH_COLS*SH_ROWS;i++){
        sh_cell *x=sh_grid+i;if(!(x->flags&SH_F_FORMULA))continue;
        char out[SH_SRC];int used=0,bad=0;const char *s=x->src;
        while(*s){
            const char *next=s;int c,r;
            if(sh_reference(&next,&c,&r)==1){
                int nr=r;
                if(insert&&r>=at)nr++;
                if(!insert&&r>at)nr--;
                if((!insert&&r==at)||nr>=SH_ROWS){bad=1;break;}
                char num[12];ex_dec(num,sizeof(num),nr+1);
                if(used+1+length(num)>=SH_SRC){bad=1;break;}
                out[used++]=(char)('A'+c);for(char *q=num;*q;)out[used++]=*q++;s=next;
            }else{if(used>=SH_SRC-1){bad=1;break;}out[used++]=*s++;}
        }
        out[used]=0;sh_set_cell(x,bad?"=#REF":out);
    }
}
static void sh_insert_row(int at)
{
    for(int c=0;c<SH_COLS;c++)if(sh_at(c,SH_ROWS-1)->src[0]){
        if(!ui_confirm("Insert row?","The last row will be discarded."))return;break;}
    for(int r=SH_ROWS-1;r>at;r--)for(int c=0;c<SH_COLS;c++)*sh_at(c,r)=*sh_at(c,r-1);
    for(int c=0;c<SH_COLS;c++)sh_set_cell(sh_at(c,at),"");
    sh_adjust_refs(at,1);sh_dirty=1;sh_undo_valid=0;sh_recalc();ui_followup=1;
}
static void sh_delete_row(int at)
{
    if(!ui_confirm("Delete row?","Data in the selected row will be removed."))return;
    for(int r=at;r<SH_ROWS-1;r++)for(int c=0;c<SH_COLS;c++)*sh_at(c,r)=*sh_at(c,r+1);
    for(int c=0;c<SH_COLS;c++)sh_set_cell(sh_at(c,SH_ROWS-1),"");
    sh_adjust_refs(at,0);sh_dirty=1;sh_undo_valid=0;sh_recalc();ui_followup=1;
}
/* CSV 扫描独立于行切分，保留引号中的 CR/LF。先装入临时网格；
 * 长字段、超行列、未闭引号都拒绝，不破坏正在编辑的工作表。 */
static int sh_csv_parse(const char *s,sh_cell *grid)
{
    int row=0,col=0;
    while(*s){
        if(row>=SH_ROWS||col>=SH_COLS)return -1;
        char text[SH_SRC];int n=0,quoted=*s=='"',closed=!quoted;
        if(quoted)s++;
        while(*s){
            char ch=*s;
            if(quoted&&ch=='"'){
                if(s[1]=='"'){ch='"';s+=2;}
                else{s++;closed=1;break;}
            }else{
                if(!quoted&&(ch==','||ch=='\r'||ch=='\n'))break;
                if(!quoted&&ch=='"')return -1;s++;
            }
            if(n>=SH_SRC-1)return -1;text[n++]=ch;
        }
        if(!closed)return -1;text[n]=0;
        if(quoted&&*s&&*s!=','&&*s!='\r'&&*s!='\n')return -1;
        sh_set_cell(grid+row*SH_COLS+col,text);
        if(*s==','){s++;col++;if(!*s&&col>=SH_COLS)return -1;}
        else if(*s=='\r'||*s=='\n'){if(*s++=='\r'&&*s=='\n')s++;row++;col=0;}
        else if(!*s)break;else return -1;
    }return 0;
}
static void sh_load(const char *path)
{
    u32 info[2];if(sc_stat(path,info)||info[0]!=1||info[1]>=SH_FILE_MAX){copy(sh_status,"Cannot open file, or file exceeds 768 KiB",sizeof(sh_status));ui_followup=1;return;}
    char *buf=sc_alloc(info[1]+1);sh_cell *next=sc_alloc(sizeof(sh_cell)*SH_COLS*SH_ROWS);
    if(!buf||!next){if(buf)sc_free(buf);if(next)sc_free(next);copy(sh_status,"Out of memory; current sheet kept",sizeof(sh_status));ui_followup=1;return;}
    for(int i=0;i<SH_COLS*SH_ROWS;i++)sh_set_cell(next+i,"");
    int n=sc_read(path,buf,info[1]);int failed=n!=(int)info[1];if(!failed){buf[n]=0;for(int i=0;i<n;i++)if(!buf[i])failed=1;}
    if(!failed)failed=sh_csv_parse(buf,next);
    if(failed){copy(sh_status,"CSV rejected: invalid quotes, size or grid bounds",sizeof(sh_status));sc_free(next);}
    else{
        sc_free(sh_grid);sh_grid=next;copy(sh_path,path,sizeof(sh_path));
        sh_dirty=sh_editing=sh_undo_valid=sh_sel_col=sh_sel_row=sh_scroll_x=sh_scroll_y=0;
        sh_recalc();copy(sh_status,"Loaded CSV",sizeof(sh_status));
    }sc_free(buf);ui_followup=1;
}
static void sh_save(const char *path)
{
    /* 最坏情况下每个字符都是双引号，容量必须按转义后的大小预算。 */
    u32 cap=SH_ROWS*SH_COLS*(SH_SRC*2+3)+1,used=0;char *buf=sc_alloc(cap);
    if(!buf){copy(sh_status,"Out of memory",sizeof(sh_status));ui_followup=1;return;}
    int lastrow=0,lastcol=0;
    for(int r=0;r<SH_ROWS;r++)for(int c=0;c<SH_COLS;c++)if(sh_at(c,r)->src[0]){if(r>lastrow)lastrow=r;if(c>lastcol)lastcol=c;}
    for(int r=0;r<=lastrow;r++){
        const char *fields[SH_COLS];for(int c=0;c<=lastcol;c++)fields[c]=sh_at(c,r)->src;
        int n=ex_csv_join(buf+used,(int)(cap-used),fields,lastcol+1);
        if(n<0){sc_free(buf);copy(sh_status,"CSV serialization failed",sizeof(sh_status));ui_followup=1;return;}used+=(u32)n;
    }
    int n=sc_write(path,buf,(int)used);sc_free(buf);
    if(n!=(int)used)copy(sh_status,"Write failed; sheet still marked unsaved",sizeof(sh_status));
    else{copy(sh_path,path,sizeof(sh_path));sh_dirty=0;copy(sh_status,"Saved CSV",sizeof(sh_status));}
    ui_followup=1;
}
static void sh_open_dialog(void)
{char path[64];copy(path,sh_path,64);if(!ui_edit_path(path,64,"Open CSV"))return;if(sh_dirty&&!ui_confirm("Discard changes?","Current sheet is not saved."))return;sh_load(path);}
static void sh_save_as(void)
{char path[64];copy(path,sh_path,64);if(ui_edit_path(path,64,"Save CSV as"))sh_save(path);}
static void sh_move(int dc,int dr)
{
    sh_sel_col=ex_clamp(sh_sel_col+dc,0,SH_COLS-1);sh_sel_row=ex_clamp(sh_sel_row+dr,0,SH_ROWS-1);sh_status[0]=0;ui_followup=1;
}
static void sh_begin_edit(void)
{copy(sh_edit,sh_at(sh_sel_col,sh_sel_row)->src,SH_SRC);sh_editing=1;sh_status[0]=0;ui_followup=1;}
static void sh_edit_key(int key)
{
    int n=length(sh_edit);
    if(key==10||key==9){sh_commit();sh_editing=0;sh_move(key==9,key==10);}
    else if(key==27){sh_editing=0;ui_followup=1;}
    else if(key==8){if(n){n--;while(n&&((u8)sh_edit[n]&192)==128)n--;sh_edit[n]=0;}ui_followup=1;}
    else if(key>=32&&key<127&&n+1<SH_SRC){sh_edit[n++]=(char)key;sh_edit[n]=0;ui_followup=1;}
}
static void sh_draw(void)
{
    ui_header(sh_dirty?"SandSheet *":"SandSheet",sh_path[0]?sh_path:"Formula spreadsheet / F1 help");
    int ty=ui_compact?32:62;
    const int ids[]={1,2,3,4,9,10};const char *labels[]={"Open","Save","SaveAs","Clear","Undo","Help"};
    int by=ui_toolbar(ids,labels,6,ty);ui_rect(16,by,UI_W-32,24,PAL_UI_INK);
    char ref[12],num[10];ref[0]=(char)('A'+sh_sel_col);ex_dec(num,10,sh_sel_row+1);copy(ref+1,num,11);
    ui_text(22,by+4,ref,PAL_UI_CYAN+7);
    const char *bar=sh_editing?sh_edit:sh_at(sh_sel_col,sh_sel_row)->src;
    int tail=sh_editing?ex_max(0,length(bar)-(UI_W-112)/8):0;
    while(tail>0&&((u8)bar[tail]&192)==128)tail--;
    ui_clip_set(76,by,UI_W-96,24);ui_text(80,by+4,bar+tail,PAL_UI_TEXT);
    if(sh_editing&&((sc_tick()/40)&1))ui_rect(80+ex_text_w(bar+tail),by+4,2,16,PAL_UI_CYAN+7);ui_clip_clear();
    int gx=16,gy=by+28,gw=UI_W-32,footer=ui_compact?22:30,nav=UI_H-footer-26;
    int rows=ex_clamp((nav-gy-20)/20,0,SH_ROWS),cols=ex_min(SH_COLS,ex_max(1,(gw-40)/80));
    sh_visible_cols=cols;sh_visible_rows=ex_max(1,rows);sh_col_width=(gw-40)/cols;
    if(sh_sel_col<sh_scroll_x)sh_scroll_x=sh_sel_col;if(sh_sel_col>=sh_scroll_x+cols)sh_scroll_x=sh_sel_col-cols+1;
    if(sh_sel_row<sh_scroll_y)sh_scroll_y=sh_sel_row;if(sh_sel_row>=sh_scroll_y+sh_visible_rows)sh_scroll_y=sh_sel_row-sh_visible_rows+1;
    sh_scroll_x=ex_clamp(sh_scroll_x,0,SH_COLS-cols);sh_scroll_y=ex_clamp(sh_scroll_y,0,SH_ROWS-sh_visible_rows);
    int gh=20+rows*20;ui_rect(gx,gy,gw,gh,PAL_UI_INK);ui_rect(gx,gy,gw,20,PAL_UI_PANEL);ui_rect(gx,gy,40,gh,PAL_UI_PANEL);
    for(int c=0;c<cols;c++){char label[2]={(char)('A'+sh_scroll_x+c),0};ui_text(gx+40+c*sh_col_width+sh_col_width/2-4,gy+2,label,PAL_UI_MUTED);}
    for(int r=0;r<rows&&sh_scroll_y+r<SH_ROWS;r++){
        ex_dec(num,10,sh_scroll_y+r+1);ui_text(gx+36-ex_text_w(num),gy+22+r*20,num,PAL_UI_MUTED);
        for(int c=0;c<cols;c++){
            int cx=gx+40+c*sh_col_width,cy=gy+20+r*20;sh_cell *x=sh_at(c+sh_scroll_x,r+sh_scroll_y);
            int selected=c+sh_scroll_x==sh_sel_col&&r+sh_scroll_y==sh_sel_row;
            if(selected)ui_rect_rgb(cx,cy,sh_col_width,20,ui_role(SC_THEME_SELECT));
            char text[SH_DISP];ex_text_cut(text,sizeof(text),x->disp,sh_col_width-8);
            if(sh_gridlines){ui_rect(cx,cy+19,sh_col_width,1,PAL_UI_LINE);ui_rect(cx+sh_col_width-1,cy,1,20,PAL_UI_LINE);}
            int tx=x->flags&SH_F_TEXT?cx+4:cx+sh_col_width-4-ex_text_w(text);
            ui_clip_set(cx+2,cy+1,sh_col_width-4,18);
            if(selected)ui_selected_text(tx,cy+2,text,1);else ui_text(tx,cy+2,text,x->flags&SH_F_ERROR?PAL_UI_ALERT:PAL_UI_TEXT);
            ui_clip_clear();if(selected)pk_border(cx,cy,sh_col_width,20,ui_role(SC_THEME_ACCENT));
        }
    }
    if((ui_pressed&1)&&ui_hit(gx+40,gy+20,cols*sh_col_width,rows*20)&&!sh_editing){
        sh_sel_col=sh_scroll_x+(ui_x-gx-40)/sh_col_width;sh_sel_row=sh_scroll_y+(ui_y-gy-20)/20;sh_status[0]=0;ui_followup=1;}
    ui_small_control(5,16,nav,60,"PgUp",0);ui_small_control(6,82,nav,60,"PgDn",0);
    ui_small_control(7,148,nav,60,"Left",0);ui_small_control(8,214,nav,60,"Right",0);
    pk_footer(sh_status[0]?sh_status:sh_editing?"Enter commit / Tab next / Esc cancel":"F2 edit / F3 save / F6 copy / F7 paste / F8 insert row / F9 delete row");
    if(sh_help){
        int w=ex_min(UI_W-32,580),h=ex_min(UI_H-48,232),x=(UI_W-w)/2,y=(UI_H-h)/2;ui_panel(x,y,w,h);
        const char *lines[]={"SandSheet / F1 closes help","Enter or F2 edits; Tab moves without clearing","Formulas: =A1*2, =SUM(A1:B9,5), =SQRT(A1)","AVG MIN MAX COUNT ABS / right-associative ^","F6 copy / F7 paste / Undo reverses last cell edit","F8 insert / F9 delete: references follow rows","CSV supports escaped quotes and multiline cells"};
        ui_clip_set(x+8,y+8,w-16,h-16);for(int i=0;i<7;i++)pk_label(x+12,y+12+i*26,w-24,lines[i],i?PAL_UI_TEXT:PAL_UI_CYAN+7);ui_clip_clear();ui_action=0;
    }
}
static void sh_action(int id)
{
    if(id==PK_ABOUT_ID){pk_about("SandSheet","Spreadsheet and CSV editor");return;}
    if(sh_editing&&id!=10){sh_commit();sh_editing=0;}
    switch(id){
    case 1:sh_open_dialog();break;case 2:sh_path[0]?sh_save(sh_path):sh_save_as();break;case 3:sh_save_as();break;
    case 4:sh_edit[0]=0;sh_commit();break;case 9:sh_undo_cell();break;case 10:sh_help=!sh_help;break;
    case 5:sh_move(0,-sh_visible_rows);break;case 6:sh_move(0,sh_visible_rows);break;
    case 7:sh_move(-sh_visible_cols,0);break;case 8:sh_move(sh_visible_cols,0);break;
    }ui_followup=1;
}
int main(void)
{
    if(ui_open("SandSheet / SandCore ext")<0)return 1;
    sh_grid=sc_alloc(sizeof(sh_cell)*SH_COLS*SH_ROWS);if(!sh_grid)return 1;
    for(int i=0;i<SH_COLS*SH_ROWS;i++)sh_set_cell(sh_grid+i,"");
    char cfg[2048],v[16];if(pk_read_text("HOME/SHEETX.CFG",cfg,sizeof(cfg))>=0){
        int d;if(!ex_cfg_get(cfg,"decimals",v,16)&&!ex_parse_int(v,&d))sh_decimals=ex_clamp(d,0,6);
        if(!ex_cfg_get(cfg,"gridlines",v,16))sh_gridlines=!equal(v,"0");}
    ui_followup=1;
    for(;;){
        if(!ui_frame_due_interval(sh_editing?40:0))continue;ui_pointer();sh_draw();ui_present();
        int key=ui_action?-1:sc_key();if(ui_action){sh_action(ui_action);continue;}
        if(key==1){sh_help=!sh_help;ui_followup=1;continue;}
        if(sh_help){if(key==27){sh_help=0;ui_followup=1;}continue;}
        if(sh_editing&&key>=0){sh_edit_key(key);continue;}
        if(key==27){if(!sh_dirty||ui_confirm("Quit SandSheet?","Unsaved changes will be lost."))return 0;}
        else if(key==0x80)sh_move(0,-1);else if(key==0x81)sh_move(0,1);
        else if(key==0x82)sh_move(-1,0);else if(key==0x83||key==9)sh_move(1,0);
        else if(key==10||key==2)sh_begin_edit();
        else if(key==3)sh_path[0]?sh_save(sh_path):sh_save_as();else if(key==4)sh_open_dialog();else if(key==5)sh_save_as();
        else if(key==6){copy(sh_clip,sh_at(sh_sel_col,sh_sel_row)->src,SH_SRC);sh_clip_valid=1;copy(sh_status,"Copied cell",sizeof(sh_status));ui_followup=1;}
        else if(key==7&&sh_clip_valid){copy(sh_edit,sh_clip,SH_SRC);sh_commit();}
        else if(key==0x88)sh_insert_row(sh_sel_row);else if(key==0x89)sh_delete_row(sh_sel_row);
        else if(key>=32&&key<127){sh_edit[0]=(char)key;sh_edit[1]=0;sh_editing=1;ui_followup=1;}
    }
}
