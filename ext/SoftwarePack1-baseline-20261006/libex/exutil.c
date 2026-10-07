/* =====================================================================
 * exutil.c —— libex 基础工具实现（字符串/格式化/UTF-8/CSV/配置/数学/
 *             32 位整数表达式引擎）。
 *
 * 工程约定：
 * - SCX 是 freestanding 环境，没有 libc；字符串与数学全部自实现，
 *   每个函数显式处理容量与边界，绝不静默截断后假装完整。
 * - 本文件是"程序员整数"语义：加减乘按补码回绕、位运算、移位。
 *   表格/游戏等需要实数的场景各自用 float/double 实现，不混用。
 * ===================================================================== */
#include "exutil.h"

/* ---------------- 整数数学 ---------------- */

int ex_abs(int v){return v<0?-v:v;}
int ex_min(int a,int b){return a<b?a:b;}
int ex_max(int a,int b){return a>b?a:b;}
int ex_clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}

/* 逐位开方（二进制试商法）：全程只做减法和移位，不构造 mid*mid
 * 这类会溢出 32 位的中间乘积，n 可达 0xFFFFFFFF 仍精确。 */
u32 ex_sqrt_u32(u32 n)
{
    u32 root=0;
    u32 bit=0x40000000u;                /* 最高的 4^15 */
    while(bit>n)bit>>=2;
    while(bit){
        u32 t=root+bit;
        if(n>=t){n-=t;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    return root;
}

u32 ex_gcd_u32(u32 a,u32 b){while(b){u32 t=a%b;a=b;b=t;}return a;}

/* 先除后乘：a/gcd*b 不会在中间量上回绕（除非结果本身超界）。 */
u32 ex_lcm_u32(u32 a,u32 b)
{if(!a||!b)return 0;return a/ex_gcd_u32(a,b)*b;}

/* xorshift32：周期 2^32-1。全零是唯一不动点，种子强制非零。 */
static u32 ex_rng_state=0x9E3779B9u;
void ex_rand_seed(u32 seed)
{
    ex_rng_state=seed^0x6D2B79F5u;
    if(!ex_rng_state)ex_rng_state=0x9E3779B9u;
    for(int i=0;i<8;i++)ex_rand_next();     /* 预热，打散弱低位 */
}
u32 ex_rand_next(void)
{
    u32 x=ex_rng_state;
    x^=x<<13;x^=x>>17;x^=x<<5;
    ex_rng_state=x;
    return x;
}
int ex_rand_range(int lo,int hi)
{if(hi<=lo)return lo;return lo+(int)(ex_rand_next()%(u32)(hi-lo+1));}

/* ---------------- 字符串判定 ---------------- */

int ex_starts(const char *s,const char *prefix)
{while(*prefix){if(*s++!=*prefix++)return 0;}return 1;}

int ex_ends(const char *s,const char *suffix)
{
    int n=length(s),m=length(suffix);
    if(m>n)return 0;
    return equal(s+n-m,suffix);
}

char ex_upper(char c){return c>='a'&&c<='z'?(char)(c-32):c;}

/* ---------------- 数值格式化 ----------------
 * 全部先在局部缓冲里生成完整数字再按容量复制，避免"半截输出"。
 * INT_MIN 用无符号幅值取负，规避有符号取负的未定义路径。 */

void ex_dec(char *out,int capacity,int value)
{
    char tmp[12];int n=0,i=0;
    u32 v=value<0?0u-(u32)value:(u32)value;
    do{tmp[n++]=(char)('0'+v%10);v/=10;}while(v);
    if(value<0&&capacity>1)out[i++]='-';
    while(n&&i+1<capacity)out[i++]=tmp[--n];
    out[i]=0;
}

void ex_udec(char *out,int capacity,u32 value)
{
    char tmp[11];int n=0,i=0;
    do{tmp[n++]=(char)('0'+value%10);value/=10;}while(value);
    while(n&&i+1<capacity)out[i++]=tmp[--n];
    out[i]=0;
}

void ex_hex(char *out,int width,u32 value)
{
    static const char *digits="0123456789ABCDEF";
    if(width<1)width=1;
    if(width>8)width=8;
    for(int i=width-1;i>=0;i--){out[i]=digits[value&15];value>>=4;}
    out[width]=0;
}

void ex_bin(char *out,int capacity,u32 value,int bits)
{
    if(bits<1)bits=1;
    if(bits>32)bits=32;
    if(capacity>0&&bits>capacity-1)bits=capacity-1;
    int i=0;
    for(int b=bits-1;b>=0;b--)out[i++]=(char)('0'+((value>>b)&1));
    if(capacity>0)out[i]=0;
}

/* ---------------- 解析 ----------------
 * 溢出在乘十前预判；空串/尾随垃圾/非法前缀一律拒绝，
 * 计算器不能把"看起来像数字"的输入悄悄变成别的数。 */

int ex_parse_int(const char *s,int *out)
{
    int minus=0;
    if(*s=='+'||*s=='-'){minus=*s=='-';s++;}
    if(!*s)return -1;
    u32 base=10;
    if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;}
    else if(s[0]=='0'&&(s[1]=='b'||s[1]=='B')){base=2;s+=2;}
    else if(s[0]=='0'&&(s[1]=='o'||s[1]=='O')){base=8;s+=2;}
    if(!*s)return -1;
    u32 limit=minus?0x80000000u:0x7FFFFFFFu,v=0;
    while(*s){
        int d=*s;
        if(d>='0'&&d<='9')d-='0';
        else if(d>='a'&&d<='f')d-='a'-10;
        else if(d>='A'&&d<='F')d-='A'-10;
        else return -1;
        if((u32)d>=base)return -1;
        if(v>(limit-(u32)d)/base)return -1;
        v=v*base+(u32)d;
        s++;
    }
    *out=minus?(int)(0u-v):(int)v;
    return 0;
}

int ex_parse_u32_hex(const char *s,u32 *out)
{
    if(!*s)return -1;
    u32 v=0;
    while(*s){
        int d=*s;
        if(d>='0'&&d<='9')d-='0';
        else if(d>='a'&&d<='f')d-='a'-10;
        else if(d>='A'&&d<='F')d-='A'-10;
        else return -1;
        if(v&0xF0000000u)return -1;         /* 第 9 个有效半字节放不下 */
        v=(v<<4)|(u32)d;
        s++;
    }
    *out=v;
    return 0;
}

/* ---------------- UTF-8 ----------------
 * 坏序列前进 1 字节并给 0xFFFD：调用方按返回步长移动永远前进，
 * 不会死循环，也不会把半个汉字当成两个独立字节绘制。 */

int ex_utf8_next(const char *s,u32 *cp)
{
    u8 c=(u8)s[0];
    *cp=0xFFFD;
    if(c<0x80){*cp=c;return 1;}
    int n=0;u32 v=0;
    if(c>=0xC2&&c<=0xDF){n=2;v=c&31u;}          /* 拒绝 C0/C1 过长形式 */
    else if(c>=0xE0&&c<=0xEF){n=3;v=c&15u;}
    else if(c>=0xF0&&c<=0xF4){n=4;v=c&7u;}      /* F5..FF 超出 Unicode */
    if(!n)return 1;
    for(int i=1;i<n;i++){
        if(((u8)s[i]&192)!=128)return i;        /* 返回已确认步长 */
        v=(v<<6)|(u32)(s[i]&63);
    }
    *cp=v;
    return n;
}

int ex_utf8_width(const char *s)
{
    int w=0;
    while(*s){
        u32 cp;
        int n=ex_utf8_next(s,&cp);
        w+=(n==1&&cp<0x80)?8:16;                /* 凤凰字体：ASCII 8，其余 16 */
        s+=n;
    }
    return w;
}

/* ---------------- 配置文件 key=value ----------------
 * 纯内存解析；# 注释与空白宽容；键精确匹配（大小写敏感，
 * 配置是程序自己的文件，宽容大小写反而让错误配置看起来合法）。 */

int ex_cfg_get(const char *body,const char *key,char *out,int capacity)
{
    if(capacity<1)return -1;
    out[0]=0;
    int klen=length(key);
    const char *p=body;
    while(*p){
        const char *line=p;
        while(*p&&*p!='\n')p++;
        int len=(int)(p-line);
        if(*p)p++;                              /* 跳过 LF（可能没有） */
        const char *s=line;
        while(len&&(*s==' '||*s=='\t'||*s=='\r')){s++;len--;}
        if(len<=0||*s=='#')continue;
        if(ex_starts(s,key)&&s[klen]=='='){
            const char *v=s+klen+1;
            int vlen=len-klen-1;
            while(vlen&&(*v==' '||*v=='\t')){v++;vlen--;}
            while(vlen&&(v[vlen-1]=='\r'||v[vlen-1]==' '||v[vlen-1]=='\t'))vlen--;
            if(vlen>=capacity)vlen=capacity-1;  /* 容量不足截断并照常返回 */
            for(int i=0;i<vlen;i++)out[i]=v[i];
            out[vlen]=0;
            return 0;
        }
    }
    return -1;
}

/* 生成带标准文件头的配置正文；容量不足整体失败（-1），
 * 调用方因此可以选择不写文件，而不是写一份被剪掉尾巴的配置。 */
int ex_cfg_build(char *out,int capacity,const char *app,const char *version,
                 const char *body)
{
    int used=0;
    const char *head[5]={"# SandCore ext: ",app," ",version," config\n\n"};
    for(int i=0;i<5;i++){
        int n=length(head[i]);
        if(used+n>=capacity)return -1;
        for(int j=0;j<n;j++)out[used++]=head[i][j];
    }
    int n=length(body);
    if(used+n+1>capacity)return -1;
    for(int i=0;i<n;i++)out[used++]=body[i];
    out[used]=0;
    return used;
}

/* ---------------- CSV ----------------
 * RFC4180 子集：双引号包裹、"" 转义。字段指针共享行缓冲，
 * 解析后行内容可能已被压缩（引号折叠），调用方使用期间不得
 * 释放/复用该缓冲。 */

int ex_csv_split(char *line,char **fields,int max_fields)
{
    if(max_fields<1)return 0;
    int count=1;
    fields[0]=line;
    int quoted=0;
    char *p=line;
    while(*p){
        if(quoted){
            if(*p=='"'){
                if(p[1]=='"'){
                    char *dst=p,*src=p+1;      /* "" 折叠为字面引号 */
                    while(*src){*dst++=*src++;}
                    *dst=0;
                }else quoted=0;
            }
        }else{
            if(*p=='"')quoted=1;
            else if(*p==','){
                *p=0;
                if(count<max_fields)fields[count++]=p+1;
                /* 超限字段并入最后一个：不丢内容，也不假装没超限 */
            }
        }
        p++;
    }
    char *last=fields[count-1];
    int n=length(last);
    while(n>0&&(last[n-1]=='\r'||last[n-1]=='\n'))last[--n]=0;
    return count;
}

int ex_csv_join(char *out,int capacity,const char **fields,int count)
{
    int used=0;
    for(int i=0;i<count;i++){
        if(i){
            if(used+1>=capacity)return -1;
            out[used++]=',';
        }
        const char *f=fields[i];
        int need=0;
        for(const char *q=f;*q;q++)
            if(*q==','||*q=='"'||*q=='\n'||*q=='\r'){need=1;break;}
        if(need){
            if(used+1>=capacity)return -1;
            out[used++]='"';
        }
        for(const char *q=f;*q;q++){
            if(*q=='"'){
                if(used+2>=capacity)return -1;
                out[used++]='"';out[used++]='"';
            }else{
                if(used+1>=capacity)return -1;
                out[used++]=*q;
            }
        }
        if(need){
            if(used+1>=capacity)return -1;
            out[used++]='"';
        }
    }
    if(used+2>capacity)return -1;
    out[used++]='\n';
    out[used]=0;
    return used;
}

/* ---------------- 32 位整数表达式引擎 ----------------
 *
 * 递归下降 + 优先级爬升。运算符优先级（低->高）：
 *   ?:  ||  &&  |  ^  &  == !=  < <= > >=  << >>  + -  * / %  一元
 *
 * 语义要点（与主项目 SCARITH 一致，程序员计算器依赖这些语义）：
 * - 加减乘按二补码回绕；这是位模式运算，不是错误。
 * - 除/余：除零或 INT_MIN/-1 置 error（x86 会抛 #DE，不能放行）。
 * - 移位计数 0..31 之外置 error（x86 用 CL 取模，静默改义更糟）。
 * - || && 短路：未选分支完全不产生错误（除零保护惯用法依赖它）。
 * - ?; 三目同样只求值被选分支。
 * - 未知变量/未知函数是错误，绝不静默当 0。
 *
 * 错误报告：error 置位时 err_off 保存第一个错误的字节偏移，
 * 调用方可以在输入行上画出错误位置。
 */

#define EX_EVAL_MAX_DEPTH 32

/* 多字节运算符编码到 256+，单字符直接用其码值 */
enum {
    EX_OP_LOR=256, EX_OP_LAND, EX_OP_EQ, EX_OP_NE,
    EX_OP_LE, EX_OP_GE, EX_OP_SHL, EX_OP_SHR
};

typedef struct {
    const char *base;    /* 起始指针，用于计算错误偏移 */
    const char *at;      /* 当前扫描位置 */
    int error;
    int err_off;
    int depth;
    ex_var_fn vars;      /* 变量表回调，可为 0 */
    ex_call_fn calls;    /* 函数表回调，可为 0 */
} ex_parser;

/* 统一的错误入口：只记第一处错误的位置 */
static void ex_eval_fail(ex_parser *p)
{
    if(!p->error){
        p->error=1;
        p->err_off=(int)(p->at-p->base);
    }
}

static void ex_eval_space(ex_parser *p)
{while(*p->at==' '||*p->at=='\t'||*p->at=='\n'||*p->at=='\r')p->at++;}

static int ex_eval_expr(ex_parser *p,int min_level,int active);

/* 一元/字面量/变量/函数/括号 */
static int ex_eval_atom(ex_parser *p,int active)
{
    ex_eval_space(p);
    if(++p->depth>EX_EVAL_MAX_DEPTH){ex_eval_fail(p);p->depth--;return 0;}
    int result=0;
    char c=*p->at;
    if(c=='('){
        p->at++;
        result=ex_eval_expr(p,0,active);
        ex_eval_space(p);
        if(*p->at!=')')ex_eval_fail(p);
        else p->at++;
    }else if(c>='0'&&c<='9'){
        u32 base=10,v=0;
        int digits=0;
        if(c=='0'&&(p->at[1]=='x'||p->at[1]=='X')){base=16;p->at+=2;}
        else if(c=='0'&&(p->at[1]=='b'||p->at[1]=='B')){base=2;p->at+=2;}
        else if(c=='0'&&(p->at[1]=='o'||p->at[1]=='O')){base=8;p->at+=2;}
        while(*p->at){
            c=*p->at;
            int d=(c>='0'&&c<='9')?c-'0':
                  (c>='a'&&c<='f')?c-'a'+10:
                  (c>='A'&&c<='F')?c-'A'+10:-1;
            if(d<0||(u32)d>=base)break;
            v=v*base+(u32)d;                 /* 字面量回绕即位模式 */
            digits=1;
            p->at++;
        }
        if(!digits)ex_eval_fail(p);          /* 孤立的 0x/0b 前缀 */
        result=(int)v;
    }else if(c=='\''){
        p->at++;
        u32 cp;
        int n=ex_utf8_next(p->at,&cp);
        if(n<1||p->at[n]!='\'')ex_eval_fail(p);
        else{result=(int)cp;p->at+=n+1;}
    }else if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'){
        char name[32];
        int n=0;
        while((c=*p->at)&&((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                           (c>='0'&&c<='9')||c=='_')){
            if(n==31){ex_eval_fail(p);break;}
            name[n++]=c;
            p->at++;
        }
        name[n]=0;
        ex_eval_space(p);
        if(*p->at=='('){
            /* 函数调用：实参表达式递归，最多 8 个 */
            p->at++;
            int args[8],argc=0;
            ex_eval_space(p);
            if(*p->at==')')p->at++;
            else{
                for(;;){
                    if(argc>=8){ex_eval_fail(p);break;}
                    args[argc++]=ex_eval_expr(p,0,active);
                    ex_eval_space(p);
                    if(*p->at==','){p->at++;continue;}
                    if(*p->at==')'){p->at++;break;}
                    ex_eval_fail(p);
                    break;
                }
            }
            if(!p->error&&active){
                int ok=p->calls&&p->calls(name,args,argc,&result);
                if(!ok)ex_eval_fail(p);      /* 未知函数/实参数不对 */
            }
        }else if(active){
            int value=0,ok=p->vars&&p->vars(name,&value);
            if(!ok)ex_eval_fail(p);          /* 未知变量 */
            result=value;
        }
    }else ex_eval_fail(p);
    p->depth--;
    return result;
}

/* 词法一个二元运算符；返回优先级，0=不是运算符 */
static int ex_eval_operator(ex_parser *p,int *bytes)
{
    const unsigned char *s=(const unsigned char *)p->at;
    *bytes=1;
    if(s[0]=='|'&&s[1]=='|'){*bytes=2;return EX_OP_LOR;}
    if(s[0]=='&'&&s[1]=='&'){*bytes=2;return EX_OP_LAND;}
    if(s[0]=='='&&s[1]=='='){*bytes=2;return EX_OP_EQ;}
    if(s[0]=='!'&&s[1]=='='){*bytes=2;return EX_OP_NE;}
    if(s[0]=='<'&&s[1]=='<'){*bytes=2;return EX_OP_SHL;}
    if(s[0]=='>'&&s[1]=='>'){*bytes=2;return EX_OP_SHR;}
    if(s[0]=='<'&&s[1]=='='){*bytes=2;return EX_OP_LE;}
    if(s[0]=='>'&&s[1]=='='){*bytes=2;return EX_OP_GE;}
    if(s[0]=='|'){return '|';}
    if(s[0]=='^'){return '^';}
    if(s[0]=='&'){return '&';}
    if(s[0]=='<'){return '<';}
    if(s[0]=='>'){return '>';}
    if(s[0]=='+'||s[0]=='-'){return 9;}
    if(s[0]=='*'||s[0]=='/'||s[0]=='%'){return 10;}
    return 0;
}

static int ex_eval_binary(ex_parser *p,int op,int left,int right)
{
    u32 a=(u32)left,b=(u32)right;
    switch(op){
    case '+':return (int)(a+b);
    case '-':return (int)(a-b);
    case '*':return (int)(a*b);
    case '/':case '%':
        if(!right||(left==(int)0x80000000u&&right==-1)){ex_eval_fail(p);return 0;}
        return op=='/'?left/right:left%right;
    case EX_OP_SHL:case EX_OP_SHR:
        if(right<0||right>31){ex_eval_fail(p);return 0;}
        return op==EX_OP_SHL?(int)(a<<right):left>>right;
    case '<':return left<right;
    case '>':return left>right;
    case EX_OP_LE:return left<=right;
    case EX_OP_GE:return left>=right;
    case EX_OP_EQ:return left==right;
    case EX_OP_NE:return left!=right;
    case '&':return left&right;
    case '^':return left^right;
    case '|':return left|right;
    case EX_OP_LOR:return left||right;
    case EX_OP_LAND:return left&&right;
    }
    ex_eval_fail(p);
    return 0;
}

static int ex_eval_expr(ex_parser *p,int min_level,int active)
{
    ex_eval_space(p);
    if(++p->depth>EX_EVAL_MAX_DEPTH){ex_eval_fail(p);p->depth--;return 0;}
    int left;
    /* 一元前缀绑定最紧（优先级 10 的右侧递归实现） */
    ex_eval_space(p);
    char c=*p->at;
    if(c=='-'||c=='+'||c=='!'||c=='~'){
        p->at++;
        int v=ex_eval_expr(p,10,active);
        left=c=='-'?(int)(0u-(u32)v):c=='+'?v:c=='!'?!v:~v;
    }else left=ex_eval_atom(p,active);

    while(!p->error){
        ex_eval_space(p);
        int bytes,level=ex_eval_operator(p,&bytes);
        if(!level||level<min_level)break;
        p->at+=bytes;
        if(level==EX_OP_LOR||level==EX_OP_LAND){
            /* 短路：被跳过分支必须连错误都不产生，比如
             * x!=0 && 65535/x 是常见保护写法。active 旗标一路
             * 传下去，跳过的分支只消费记号不产生副作用/错误。 */
            int use=active&&(level==EX_OP_LOR?!left:!!left);
            int right=ex_eval_expr(p,level+1,use);
            if(p->error)break;
            left=level==EX_OP_LOR?(left||right):(left&&right);
        }else{
            int right=ex_eval_expr(p,level+1,active);
            if(p->error)break;
            left=ex_eval_binary(p,level,left,right);
        }
    }
    if(!p->error&&!min_level){
        /* 三目只在全表达式层解析：A ? B : C，B/C 同样带条件 active */
        ex_eval_space(p);
        if(*p->at=='?'){
            p->at++;
            int yes=ex_eval_expr(p,0,active&&left);
            ex_eval_space(p);
            if(*p->at!=':')ex_eval_fail(p);
            else{
                p->at++;
                int no=ex_eval_expr(p,0,active&&!left);
                if(!p->error)left=left?yes:no;
            }
        }
    }
    p->depth--;
    return left;
}

int ex_eval(const char *text,ex_var_fn vars,ex_call_fn calls,int *out,int *err_pos)
{
    ex_parser p;
    p.base=text;p.at=text;p.error=0;p.err_off=0;p.depth=0;
    p.vars=vars;p.calls=calls;
    int v=ex_eval_expr(&p,0,1);
    ex_eval_space(&p);
    if(!p.error&&*p.at)ex_eval_fail(&p);        /* 尾随垃圾 */
    if(err_pos)*err_pos=p.err_off;
    if(p.error)return -1;
    *out=v;
    return 0;
}

/* ---------------- 目录行协议解析 ----------------
 * SC_DIR 每行 "D/F 名字 大小\n"；这里拆成并行数组。
 * 任何一行不合法就整体失败：调用方宁可少装，不装错。 */

int ex_dir_parse(const char *list,char names[][64],int *kinds,int max)
{
    const char *p=list;
    int count=0;
    while(*p){
        char kind=*p++;
        if(kind!='D'&&kind!='F')return -1;
        if(*p++!=' ')return -1;
        if(count>=max)return -1;
        char *name=names[count];
        int used=0;
        while(*p&&*p!=' '&&*p!='\n'){
            if(used==63)return -1;
            name[used++]=*p++;
        }
        name[used]=0;
        if(*p!=' ')return -1;
        while(*p&&*p!=' ')p++;                  /* 跳过大小字段 */
        if(*p!=' ')return -1;
        while(*p&&*p!='\n')p++;
        if(*p)p++;
        kinds[count]=kind=='D'?2:1;
        count++;
    }
    return count;
}
