/* =====================================================================
 * SCCC（s3c）—— mio 的沙核原生 32 位 C 编译器，M7。
 *
 * 【自编译约束】本文件及三个 .inc 都是普通 C 源码，最终以本编译器
 * 支持的语言编译自身。不得根据输入文件名选择预存机器码，也不得把
 * 宿主程序复制成“编译结果”。宿主 GCC 仅构建最初的启动版本。
 *
 * 【阶段分工】词法保留位置与原始宏标记，预处理生成普通 C 标记流；
 * 类型图描述整数/指针/数组/聚合/函数，语法树描述副作用与控制流；
 * 后端逐函数生成 x86，最后解决全局地址并封装 SCX1MIO。所有容量
 * 有明确上限，溢出在写数组之前诊断，不能输出残缺却“成功”的文件。
 *
 * 【内存为何使用固定工作区】没有 libc/malloc；大数组属于 SCX BSS，
 * 加载器映射并清零。语法树每生成完一个函数即复用，符号/类型/重定位
 * 保留到整编译单元结束；指针不会指向已经复用的函数语法树。
 * 递归局部缓冲保持小，宏参数与 include 行缓冲放 BSS，避免 128KB
 * 用户栈被深层展开耗尽。文档：C.md、FS.md、SYSCALL.md。
 * ===================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned int u32;
static int length(const char *s) { return (int)strlen(s); }
static int equal(const char *a,const char *b) { return !strcmp(a,b); }
static void copy(char *d,const char *s,int n) { if(n) { snprintf(d,(size_t)n,"%s",s); } }
static void append(char *d,const char *s,int n) { int used=length(d); if(used<n) copy(d+used,s,n-used); }
static void decimal(char *d,int n) { sprintf(d,"%d",n); }
static void sc_exit(int n) { exit(n); }
static void sc_yield(void) {}
static void sc_puts(const char *s) { fputs(s,stderr); }
static const char *resolve(const char *p) {
    if(!strcmp(p,"SYS/INC/SCAPI.H")) return "user/SCAPI.H";
    return p;
}
static int sc_stat(const char *p,u32 *out) {
    FILE *f=fopen(resolve(p),"rb"); if(!f) return -1;
    fseek(f,0,SEEK_END); out[0]=1; out[1]=(u32)ftell(f); fclose(f); return 0;
}
static int sc_read(const char *p,void *out,int n) {
    FILE *f=fopen(resolve(p),"rb"); if(!f) return -1;
    int used=(int)fread(out,1,(size_t)n,f); fclose(f); return used;
}
static int sc_write(const char *p,const void *bytes,int n) {
    FILE *f=fopen(p,"wb"); if(!f) return -1;
    int used=(int)fwrite(bytes,1,(size_t)n,f); fclose(f); return used;
}


#define TOKEN_MAX 65536
#define POOL_MAX 196608
#define SOURCE_MAX 196608
#define IDENT_MAX 4096
#define FILE_MAX 32
#define LINE_MAX 1024
#define DEPTH_MAX 16
#define MACRO_MAX 256
#define MACRO_BODY_MAX 8192
#define MACRO_ARGS 8
#define MACRO_ARG_TOKENS 128
#define TYPE_MAX 4096
#define SYMBOL_MAX 6144
#define PARAM_MAX 4096
#define NODE_MAX 8192
#define FIX_MAX 12288
#define CODE_MAX 262000
#define DATA_MAX 131072
#define BASE 0x400000

/* 单字节运算符直接保存 ASCII，多字节运算符才分配扩展编号。
 * 标记 v 是整数值或字符串池偏移；字符串池前四字节保存字节数，
 * 所以 "A\0B" 不会被普通 strlen 截断。位置为 文件索引:16/行号:16。 */
enum {
    T_ID=256,T_NUM,T_UNUM,T_STR,T_NL,T_EQ,T_NE,T_LE,T_GE,T_SHL,T_SHR,
    T_AND,T_OR,T_INC,T_DEC,T_ARROW,T_ADDSET,T_SUBSET,T_MULSET,T_DIVSET,
    T_MODSET,T_ANDSET,T_ORSET,T_XORSET,T_SHLSET,T_SHRSET,T_ELLIPSIS,T_PASTE
};
typedef struct { int k,v,where; } Tok;
typedef struct { char *source;
    int offset,line,file,comment; } Lexer;
typedef struct { char path[64];
    int source,size; } Source;
typedef struct { int name,first,count,argc,active,variadic;
    int params[MACRO_ARGS]; } Macro;
typedef struct { int parent,active,taken,otherwise; } Conditional;

/* 类型引用以整数索引表示，自编译时不依赖宿主指针宽度/结构体标签
 * 的隐藏表示。聚合字段/函数参数的列表存独立表，作用域只是名字解析
 * 的可见性标记，不会让已建语法树的符号编号失效。 */
enum { TY_VOID,TY_CHAR,TY_SHORT,TY_INT,TY_PTR,TY_ARRAY,TY_FUNC,TY_STRUCT,TY_UNION };
typedef struct { int kind,size,base,count,fields,flags; } Type;
enum { S_GLOBAL,S_LOCAL,S_PARAM,S_FUNCTION,S_TYPEDEF,S_ENUM,S_FIELD,S_TAG };
typedef struct { int name,type,kind,value,scope,defined,next,aux; } Symbol;
typedef struct { int name,type,next; } Parameter;
enum {
    N_CONST,N_SYMBOL,N_STRING,N_DEREF,N_ADDR,N_CAST,N_BINARY,N_UNARY,N_ASSIGN,
    N_INC,N_CALL,N_COND,N_COMMA,N_BLOCK,N_EXPR,N_IF,N_WHILE,N_DO,N_FOR,
    N_RETURN,N_BREAK,N_CONTINUE,N_SWITCH,N_CASE,N_ASM,N_ARG,N_INIT,N_LABEL,N_GOTO
};
typedef struct { int kind,type,a,b,c,value,where,next; } Node;
typedef struct { int offset,kind,symbol,addend; } Fix;

static char pool[POOL_MAX],source_arena[SOURCE_MAX];
static int pool_used,source_used,identifiers[IDENT_MAX],identifier_count;
static Source sources[FILE_MAX];
static int source_count;
static Tok tokens[TOKEN_MAX];
static int token_count,position,error_position;
static Macro macros[MACRO_MAX];
static int macro_count,macro_body_used;
static Tok macro_body[MACRO_BODY_MAX];
static Tok lines[DEPTH_MAX][LINE_MAX];
static Tok macro_arguments[DEPTH_MAX][MACRO_ARGS][MACRO_ARG_TOKENS];
static int macro_arg_lengths[DEPTH_MAX][MACRO_ARGS];
static Conditional conditions[128];
static int condition_count;
static int line_ends[DEPTH_MAX][LINE_MAX],source_once[FILE_MAX],macro_busy[MACRO_MAX];
static Tok replacements[DEPTH_MAX][LINE_MAX],condition_tokens[LINE_MAX];
static Tok raw_chunk[16384];
static int raw_used;
static Type types[TYPE_MAX];
static Symbol symbols[SYMBOL_MAX];
static Parameter parameters[PARAM_MAX];
static Node nodes[NODE_MAX];
static Fix fixes[FIX_MAX];
static int type_count,symbol_count,param_count,node_count,fix_count;
static int function_symbol,scope_depth,local_bytes;
static u8 code[CODE_MAX+36],data[DATA_MAX];
static int code_used,data_used,bss_used;
static char log_path[64],map_path[64],diagnostic[512],map_text[16384];
static int map_used;
/* 后端会在函数语法树完成后立即消费它；前置原型保证宿主与 SCCC
 * 自身采用同一套声明，不依赖宿主编译器的隐式函数扩展。 */
static void emit_function(int symbol,int body);
static void global_init(int type,int offset,int expression);
static void define_global(int symbol,int initializer);
static u32 constant_value(int expression);
static void compile_unit(void);
static void finish_image(const char *output);

static void fatal(const char *reason)
{
    /* 报错后立即 EXIT，任何阶段都不会再提交输出文件；旧的成功文件
     * 由调用者决定是否保留，日志指出失败，IDE 不应运行旧文件冒充新编译。
     * 文件名/行号来自标记位置，宏/包含文件也能追踪到具体源位置。 */
    int file=(((u32)error_position>>16)&32767),line=error_position&65535;
    char number[12];
    decimal(number,line);
    copy(diagnostic,"SCCC ERROR ",sizeof(diagnostic));
    if(file<source_count) append(diagnostic,sources[file].path,sizeof(diagnostic));
    append(diagnostic,":",sizeof(diagnostic));
    append(diagnostic,number,sizeof(diagnostic));
    append(diagnostic,": ",sizeof(diagnostic));
    append(diagnostic,reason,sizeof(diagnostic));
    append(diagnostic,"\n",sizeof(diagnostic));
    sc_write(log_path,diagnostic,length(diagnostic));
    sc_puts(diagnostic);
    sc_exit(1);
    for(;;) sc_yield();
}
static int text_size(int at) { return *(int *)(pool+at-4); }
static char *spelling(int at) { return pool+at; }
static int store_text(const char *s,int n)
{
    if(n<0 || pool_used+12+n+1>POOL_MAX) fatal("string pool capacity exceeded");
    int at=pool_used+12;
    *(int *)(pool+at-12)=at;
    *(int *)(pool+at-8)=0;
    *(int *)(pool+at-4)=n;
    for(int i=0;i<n;i++) pool[at+i]=s[i];
    pool[at+n]=0;
    pool_used=at+n+1;
    return at;
}
static int intern(const char *s,int n)
{
    for(int i=0;i<identifier_count;i++) {
        int at=identifiers[i],same=1;
        if(text_size(at)!=n) continue;
        for(int j=0;j<n;j++) if(pool[at+j]!=s[j]) same=0;
        if(same) return at;
    }
    if(identifier_count>=IDENT_MAX) fatal("identifier capacity exceeded");
    int at=store_text(s,n);
    identifiers[identifier_count++]=at;
    return at;
}
static u32 number(Tok *t) { return *(u32 *)(pool+t->v-8); }
static int raw_text(Tok *t) { return *(int *)(pool+t->v-12); }
static void make_number(Tok *t,u32 value,int where)
{
    char b[12],rev[11];
    u32 v=value;
    int n=0,i=0;
    do { rev[n++]=(char)('0'+v%10);
        v/=10; } while(v);
    while(n) b[i++]=rev[--n];
    b[i]=0;
    t->k=value>0x7FFFFFFFu?T_UNUM:T_NUM;
    t->where=where;
    t->v=store_text(b,i);
    *(u32 *)(pool+t->v-8)=value;
}
static int word(Tok *t,const char *s) { return t->k==T_ID && equal(spelling(t->v),s); }
static void tokcopy(Tok *d,Tok *s) { d->k=s->k;
    d->v=s->v;
    d->where=s->where; }

/* mio：词法器只识别字符/标记，不执行宏。每个源文件保存独立 Lexer，
 * include 递归返回后不会丢失原文件游标。物理反斜杠换行先被 peek 吃掉，
 * 因而长宏、字符串及分裂的标识符均遵守 C 的行连接顺序；行号仍增加。 */
static int lexpeek(Lexer *l)
{
    for(;;) {
        char *s=l->source+l->offset;
        if(s[0]=='\\' && s[1]=='\n') { l->offset+=2;
            l->line++;
            continue; }
        if(s[0]=='\\' && s[1]=='\r' && s[2]=='\n') { l->offset+=3;
            l->line++;
            continue; }
        return (u8)s[0];
    }
}
static int lexmatch(Lexer *l,const char *s)
{
    Lexer test=*l;
    while(*s) { if(lexpeek(&test)!=(u8)*s++) return 0;
        test.offset++; }
    *l=test;
    return 1;
}
static int hexvalue(int c)
{
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int escape(Lexer *l)
{
    int c=lexpeek(l);
    if(!c) fatal("unfinished escape");
    l->offset++;
    if(c=='n') return '\n';
    if(c=='r') return '\r';
    if(c=='t') return '\t';
    if(c=='b') return '\b';
    if(c=='f') return '\f';
    if(c=='v') return '\v';
    if(c=='a') return 7;
    if(c=='x') {
        u32 value=0;
        int n=0;
        while(hexvalue(lexpeek(l))>=0) { value=value*16+hexvalue(lexpeek(l));
            l->offset++;
            n++; }
        if(!n) fatal("hex escape needs digits");
        return value&255;
    }
    if(c>='0' && c<='7') {
        int value=c-'0',n=1;
        while(n<3 && lexpeek(l)>='0' && lexpeek(l)<='7') { value=value*8+lexpeek(l)-'0';
            l->offset++;
            n++; }
        return value&255;
    }
    return c;
}
static int store_source(Lexer *l,int start)
{
    char text[1024];
    int n=0;
    for(int i=start;i<l->offset;i++) {
        if(l->source[i]=='\\' && l->source[i+1]=='\n') { i++;
            continue; }
        if(l->source[i]=='\\' && l->source[i+1]=='\r' && l->source[i+2]=='\n') { i+=2;
            continue; }
        if(n>=1023) fatal("literal spelling exceeds 1023 bytes");
        text[n++]=l->source[i];
    }
    return store_text(text,n);
}
static int identifier_char(int c)
{ return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' || (c>='0' && c<='9'); }
static void lexnext(Lexer *l,Tok *t)
{
    int c,space=0;
    for(;;) {
        c=lexpeek(l);
        if(c==' ' || c=='\t' || c=='\r' || c=='\v' || c=='\f') { l->offset++;
            space=1;
            continue; }
        if(l->comment) {
            int ended=0;
            while(lexpeek(l)) {
                if(lexmatch(l,"*/")) { l->comment=0;
                    ended=1;
                    break; }
                if(lexpeek(l)=='\n') {
                    t->where=(l->file<<16)|l->line;
                    t->k=T_NL;
                    t->v=0;
                    l->line++;
                    l->offset++;
                    return;
                }
                l->offset++;
            }
            if(!ended) fatal("unfinished comment");
            continue;
        }
        if(lexmatch(l,"//")) { space=1;
            while(lexpeek(l) && lexpeek(l)!='\n') l->offset++;
            continue; }
        if(lexmatch(l,"/*")) { space=1;
            l->comment=1;
            continue; }
        break;
    }
    t->where=(l->file<<16)|l->line|(space?(int)0x80000000u:0);
    error_position=t->where;
    t->v=0;
    if(!c) { t->k=0;
        return; }
    if(c=='\n') { l->offset++;
        l->line++;
        t->k=T_NL;
        return; }
    if(identifier_char(c) && !(c>='0' && c<='9')) {
        char name[128];
        int n=0;
        while(identifier_char(lexpeek(l))) {
            if(n>=127) fatal("identifier exceeds 127 bytes");
            name[n++]=(char)lexpeek(l);
            l->offset++;
        }
        t->k=T_ID;
        t->v=intern(name,n);
        return;
    }
    if(c>='0' && c<='9') {
        int start=l->offset,base=10,digits=0,unsigned_flag=0;
        u32 value=0;
        if(lexmatch(l,"0x") || lexmatch(l,"0X")) base=16;
        else if(c=='0') base=8;
        while(hexvalue(lexpeek(l))>=0 && hexvalue(lexpeek(l))<base) {
            value=value*(u32)base+(u32)hexvalue(lexpeek(l));
            l->offset++;
            digits++;
        }
        if(!digits) fatal("integer literal needs digits");
        while(lexpeek(l)=='u' || lexpeek(l)=='U' || lexpeek(l)=='l' || lexpeek(l)=='L') {
            if(lexpeek(l)=='u' || lexpeek(l)=='U') unsigned_flag=1;
            l->offset++;
        }
        if(identifier_char(lexpeek(l)) || lexpeek(l)=='.') fatal("invalid or unsupported numeric literal");
        t->k=unsigned_flag || value>0x7FFFFFFFu?T_UNUM:T_NUM;
        t->v=store_source(l,start);
        *(u32 *)(pool+t->v-8)=value;
        return;
    }
    if(c=='\"' || c=='\'') {
        int start=l->offset,quote=c,n=0;
        char bytes[1024];
        l->offset++;
        while(lexpeek(l) && lexpeek(l)!=quote) {
            int ch=lexpeek(l);
            l->offset++;
            if(ch=='\n') fatal("newline inside literal");
            if(ch=='\\') ch=escape(l);
            if(n>=1023) fatal("literal exceeds 1023 bytes");
            bytes[n++]=(char)ch;
        }
        if(lexpeek(l)!=quote) fatal("unfinished literal");
        l->offset++;
        if(quote=='\'') {
            if(n!=1) fatal("character literal must contain one byte");
            t->k=T_NUM;
            t->v=store_source(l,start);
            *(u32 *)(pool+t->v-8)=(u8)bytes[0];
        } else { t->k=T_STR;
        t->v=store_text(bytes,n);
            int raw=store_source(l,start);
            *(int *)(pool+t->v-12)=raw; }
        return;
    }
    /* 最长匹配必须先测三字符，否则 >>= 会被拆成 >> 与 =，复合赋值
     * 的副作用与类型规则就不再可能正确恢复。字符串/注释先于此表。 */
    const char *ops[]={"<<=",">>=","...","==","!=","<=",">=","<<",">>","&&","||","++","--","->","+=","-=","*=","/=","%=","&=","|=","^=","##"};
    int kinds[]={T_SHLSET,T_SHRSET,T_ELLIPSIS,T_EQ,T_NE,T_LE,T_GE,T_SHL,T_SHR,T_AND,T_OR,T_INC,T_DEC,T_ARROW,T_ADDSET,T_SUBSET,T_MULSET,T_DIVSET,T_MODSET,T_ANDSET,T_ORSET,T_XORSET,T_PASTE};
    for(int i=0;i<23;i++) if(lexmatch(l,ops[i])) { t->k=kinds[i];
        return; }
    if(c>=128) fatal("non-ASCII identifier is unsupported");
    t->k=c;
    l->offset++;
}

/* =====================================================================
 * mio：宏预处理。展开作用于标记而不是字符串替换，因此注释/字符串
 * 中同名内容不会被替换，函数参数也按嵌套括号而不是第一个逗号切开。
 * include 与宏递归分别受 DEPTH_MAX 约束；参数/替换缓冲属于 BSS，
 * 深层展开不会在用户栈上叠出几十 KB 的二维数组。
 * 原始参数用于 #/##，普通参数先展开；随后禁用当前宏再重扫替换体，
 * 防止直接/间接递归，同时允许 f(f(1)) 的内层参数先正常展开。
 * ===================================================================== */
static int macro_find(int name)
{ for(int i=0;i<macro_count;i++) if(macros[i].active && macros[i].name==name) return i;
    return -1; }
static void pp_add(Tok *out,int *used,int capacity,Tok *t)
{
    if(*used>=capacity) fatal("preprocessor token capacity exceeded");
    tokcopy(out+*used,t);
    (*used)++;
}
static const char *operator_text(int k)
{
    const char *ops[]={"==","!=","<=",">=","<<",">>","&&","||","++","--","->","+=","-=","*=","/=","%=","&=","|=","^=","<<=",">>=","...","##"};
    int kinds[]={T_EQ,T_NE,T_LE,T_GE,T_SHL,T_SHR,T_AND,T_OR,T_INC,T_DEC,T_ARROW,T_ADDSET,T_SUBSET,T_MULSET,T_DIVSET,T_MODSET,T_ANDSET,T_ORSET,T_XORSET,T_SHLSET,T_SHRSET,T_ELLIPSIS,T_PASTE};
    for(int i=0;i<23;i++) if(k==kinds[i]) return ops[i];
    return 0;
}
static void token_text(Tok *t,char *out,int capacity)
{
    if(t->k==T_ID) copy(out,spelling(t->v),capacity);
    else if(t->k==T_STR || t->k==T_NUM || t->k==T_UNUM) copy(out,spelling(raw_text(t)),capacity);
    else if(t->k<256) { out[0]=(char)t->k;
        out[1]=0; }
    else {
        const char *s=operator_text(t->k);
        if(!s) fatal("cannot spell token");
        copy(out,s,capacity);
    }
}
static void string_token(Tok *t,const char *s,int n,int where)
{
    t->k=T_STR;
    t->v=store_text(s,n);
    t->where=where;
    char raw[2048];
    int used=0;
    raw[used++]='"';
    for(int i=0;i<n;i++) {
        if(s[i]=='"' || s[i]=='\\') raw[used++]='\\';
        if(used>2044) fatal("stringification capacity exceeded");
        raw[used++]=s[i];
    }
    raw[used++]='"';
    *(int *)(pool+t->v-12)=store_text(raw,used);
}
static void stringify(Tok *tokens,int n,Tok *out,int where)
{
    char text[1024],part[1024];
    int used=0;
    for(int i=0;i<n;i++) {
        token_text(tokens+i,part,sizeof(part));
        int size=length(part);
        if(i && ((u32)tokens[i].where&0x80000000u)) {
            if(used>=1023) fatal("stringification exceeds 1023 bytes");
            text[used++]=' ';
        }
        if(used+size>1023) fatal("stringification exceeds 1023 bytes");
        for(int j=0;j<size;j++) text[used++]=part[j];
    }
    string_token(out,text,used,where);
}
static void paste(Tok *left,Tok *right)
{
    /* 空参数在 ## 边上保留占位，不能把它前面的无关 token 一并粘走。 */
    if(left->k==-1) { tokcopy(left,right);
        return; }
    if(right->k==-1) return;
    char text[1024],part[1024];
    token_text(left,text,sizeof(text));
    token_text(right,part,sizeof(part));
    if(length(text)+length(part)>=1024) fatal("token paste exceeds 1023 bytes");
    append(text,part,sizeof(text));
    Lexer l;
    l.source=text;
    l.offset=0;
    l.line=1;
    l.file=0;
    l.comment=0;
    Tok joined,end;
    lexnext(&l,&joined);
    lexnext(&l,&end);
    if(end.k || !joined.k) fatal("token paste must form exactly one token");
    joined.where=left->where;
    tokcopy(left,&joined);
}
static int macro_parameter(Macro *m,int name)
{ for(int i=0;i<m->argc;i++) if(m->params[i]==name) return i;
    return -1; }
static int macro_alias(int name)
{
    /* 对象宏可给函数宏起别名：#define ALIAS FUNC。只跟单标记链，
     * 一旦看到函数宏即可把紧随调用的括号纳入本次重扫；循环受宏数限制。 */
    int i=macro_find(name);
    for(int n=0;n<MACRO_MAX && i>=0;n++) {
        if(macros[i].argc>=0) return i;
        if(macros[i].count!=1 || macro_body[macros[i].first].k!=T_ID) return -1;
        i=macro_find(macro_body[macros[i].first].v);
    }
    return -1;
}
static void pp_expand(Tok *input,int count,int depth,Tok *output,int *used,int capacity)
{
    if(depth>=DEPTH_MAX) fatal("macro expansion depth exceeded");
    for(int i=0;i<count;i++) {
        Tok *t=input+i;
        error_position=t->where;
        if(word(t,"__LINE__")) { Tok number_token;
            make_number(&number_token,(u32)(t->where&65535),t->where);
            pp_add(output,used,capacity,&number_token);
            continue; }
        if(word(t,"__FILE__")) {
            Tok string;
            int file=((u32)t->where>>16)&32767;
            string_token(&string,sources[file].path,length(sources[file].path),t->where);
            pp_add(output,used,capacity,&string);
            continue;
        }
        int original=t->k==T_ID?macro_find(t->v):-1,mi=original;
        if(mi<0 || macro_busy[mi]) { pp_add(output,used,capacity,t);
            continue; }
        Macro *m=macros+mi;
        if(m->argc<0 && i+1<count && input[i+1].k=='(') {
            int alias=macro_alias(t->v);
            if(alias>=0 && !macro_busy[alias]) { mi=alias;
                m=macros+mi; }
        }
        if(m->argc>=0) {
            if(i+1>=count || input[i+1].k!='(') { pp_add(output,used,capacity,t);
                continue; }
            for(int a=0;a<MACRO_ARGS;a++) macro_arg_lengths[depth][a]=0;
            int nesting=0,argument=0,closed=0;
            i+=2;
            for(;i<count;i++) {
                int k=input[i].k;
                if(k==')' && nesting==0) { closed=1;
                    break; }
                if(k==',' && nesting==0 && !(m->variadic && argument==m->argc-1)) {
                    argument++;
                    if(argument>=MACRO_ARGS) fatal("too many macro arguments");
                    continue;
                }
                if(k=='(') nesting++;
                if(k==')') nesting--;
                pp_add(macro_arguments[depth][argument],&macro_arg_lengths[depth][argument],MACRO_ARG_TOKENS,input+i);
            }
            if(!closed) fatal("unfinished macro invocation");
            int argc=argument+1;
            if(argc==1 && !macro_arg_lengths[depth][0] && m->argc==0) argc=0;
            if((!m->variadic && argc!=m->argc) || (m->variadic && argc<m->argc-1)) fatal("macro argument count mismatch");
        }
        Tok *replacement=replacements[depth];
        int n=0,pasting=0;
        for(int b=0;b<m->count;b++) {
            Tok *body=macro_body+m->first+b;
            if(body->k==T_PASTE) { if(!b || b==m->count-1) fatal("paste cannot be at macro edge");
                pasting=1;
                continue; }
            int parameter=body->k==T_ID?macro_parameter(m,body->v):-1;
            if(body->k=='#' && b+1<m->count) {
                int p=macro_parameter(m,macro_body[m->first+b+1].v);
                if(p>=0) {
                    Tok string;
                    stringify(macro_arguments[depth][p],macro_arg_lengths[depth][p],&string,t->where);
                    if(pasting && n) paste(replacement+n-1,&string);
                    else pp_add(replacement,&n,LINE_MAX,&string);
                    pasting=0;
                    b++;
                    continue;
                }
            }
            if(parameter>=0) {
                Tok *arg=macro_arguments[depth][parameter];
                int amount=macro_arg_lengths[depth][parameter];
                int raw=pasting || (b+1<m->count && macro_body[m->first+b+1].k==T_PASTE);
                if(raw) {
                    if(!amount && b+1<m->count && macro_body[m->first+b+1].k==T_PASTE) {
                        Tok empty;
                        empty.k=-1;
                        empty.v=0;
                        empty.where=body->where;
                        pp_add(replacement,&n,LINE_MAX,&empty);
                    }
                    for(int j=0;j<amount;j++) {
                        if(pasting && j==0 && n) paste(replacement+n-1,arg+j);
                        else pp_add(replacement,&n,LINE_MAX,arg+j);
                    }
                } else pp_expand(arg,amount,depth+1,replacement,&n,LINE_MAX);
            } else {
                if(pasting && n) paste(replacement+n-1,body);
                else pp_add(replacement,&n,LINE_MAX,body);
            }
            pasting=0;
        }
        int compact=0;
        for(int j=0;j<n;j++) if(replacement[j].k!=-1) tokcopy(replacement+compact++,replacement+j);
        n=compact;
        /* 替换结果最后若是函数宏，与原调用后紧随的 (...) 一起重扫。
         * 例如 ID(F)(1)；其余后续标记仍在外层处理，不一直禁用当前宏。 */
        if(n && replacement[n-1].k==T_ID && macro_alias(replacement[n-1].v)>=0
           && i+1<count && input[i+1].k=='(') {
            int nesting=0;
            i++;
            do {
                if(i>=count) fatal("unfinished rescanned macro invocation");
                if(input[i].k=='(') nesting++;
                if(input[i].k==')') nesting--;
                pp_add(replacement,&n,LINE_MAX,input+i);
                if(nesting) i++;
            } while(nesting);
        }
        macro_busy[original]=macro_busy[mi]=1;
        pp_expand(replacement,n,depth+1,output,used,capacity);
        macro_busy[original]=macro_busy[mi]=0;
    }
}
static int pp_precedence(int op)
{
    if(op==T_OR) return 1;
    if(op==T_AND) return 2;
    if(op=='|') return 3;
    if(op=='^') return 4;
    if(op=='&') return 5;
    if(op==T_EQ || op==T_NE) return 6;
    if(op=='<' || op=='>' || op==T_LE || op==T_GE) return 7;
    if(op==T_SHL || op==T_SHR) return 8;
    if(op=='+' || op=='-') return 9;
    if(op=='*' || op=='/' || op=='%') return 10;
    return 0;
}
static u32 pp_expression(Tok *ts,int count,int *at,int minimum,int evaluate)
{
    if(*at>=count) fatal("incomplete #if expression");
    Tok *t=ts+*at;
    (*at)++;
    u32 value=0;
    if(t->k==T_NUM || t->k==T_UNUM) value=number(t);
    else if(t->k==T_ID) value=0;
    else if(t->k=='(') {
        value=pp_expression(ts,count,at,1,evaluate);
        if(*at>=count || ts[*at].k!=')') fatal("#if needs closing parenthesis");
        (*at)++;
    } else if(t->k=='!' || t->k=='~' || t->k=='-' || t->k=='+') {
        value=pp_expression(ts,count,at,11,evaluate);
        if(t->k=='!') value=!value;
        if(t->k=='~') value=~value;
        if(t->k=='-') value=0u-value;
    } else fatal("invalid #if expression");
    while(*at<count) {
        int op=ts[*at].k,precedence=pp_precedence(op);
        if(precedence<minimum) break;
        (*at)++;
        int right_evaluate=evaluate;
        if((op==T_AND && !value) || (op==T_OR && value)) right_evaluate=0;
        u32 right=pp_expression(ts,count,at,precedence+1,right_evaluate);
        if(!evaluate) { value=0;
            continue; }
        if(op=='+') value+=right;
        else if(op=='-') value-=right;
        else if(op=='*') value*=right;
        else if(op=='/' || op=='%') {
            if(!right) fatal("division by zero in #if");
            if(value==0x80000000u && right==0xFFFFFFFFu) fatal("signed division overflow in #if");
            if(op=='/') value=(u32)((int)value/(int)right);
            else value=(u32)((int)value%(int)right);
        } else if(op=='&') value&=right;
        else if(op=='^') value^=right;
        else if(op=='|') value|=right;
        else if(op==T_SHL) value<<=(right&31);
        else if(op==T_SHR) value=(u32)((int)value>>(right&31));
        else if(op==T_EQ) value=value==right;
        else if(op==T_NE) value=value!=right;
        else if(op=='<') value=(int)value<(int)right;
        else if(op=='>') value=(int)value>(int)right;
        else if(op==T_LE) value=(int)value<=(int)right;
        else if(op==T_GE) value=(int)value>=(int)right;
        else if(op==T_AND) value=value && right;
        else if(op==T_OR) value=value || right;
    }
    if(minimum==1 && *at<count && ts[*at].k=='?') {
        (*at)++;
        u32 yes=pp_expression(ts,count,at,1,evaluate && value!=0);
        if(*at>=count || ts[*at].k!=':') fatal("#if conditional needs colon");
        (*at)++;
        u32 no=pp_expression(ts,count,at,1,evaluate && value==0);
        value=value?yes:no;
    }
    return value;
}
static int pp_condition(Tok *line,int count)
{
    int used=0;
    for(int i=0;i<count;i++) {
        if(word(line+i,"defined")) {
            int paren=0;
            if(++i<count && line[i].k=='(') { paren=1;
                i++; }
            if(i>=count || line[i].k!=T_ID) fatal("defined needs an identifier");
            Tok value;
            make_number(&value,macro_find(line[i].v)>=0,line[i].where);
            pp_add(condition_tokens,&used,LINE_MAX,&value);
            if(paren && (++i>=count || line[i].k!=')')) fatal("defined needs closing parenthesis");
        } else pp_add(condition_tokens,&used,LINE_MAX,line+i);
    }
    int expanded=0;
    pp_expand(condition_tokens,used,0,replacements[DEPTH_MAX-1],&expanded,LINE_MAX);
    int at=0;
    u32 value=pp_expression(replacements[DEPTH_MAX-1],expanded,&at,1,1);
    if(at!=expanded) fatal("extra token in #if");
    return value!=0;
}
static int load_source(const char *path)
{
    for(int i=0;i<source_count;i++) if(equal(path,sources[i].path)) return i;
    u32 info[2];
    if(sc_stat(path,info) || info[0]!=1) return -1;
    if(source_count>=FILE_MAX || length(path)>63) fatal("include file capacity exceeded");
    if(info[1]>(u32)(SOURCE_MAX-source_used-1)) fatal("source arena capacity exceeded");
    int i=source_count++;
    copy(sources[i].path,path,64);
    sources[i].source=source_used;
    sources[i].size=(int)info[1];
    int n=sc_read(path,source_arena+source_used,(int)info[1]);
    if(n!=(int)info[1]) fatal("source read failed");
    for(int k=0;k<n;k++) if(!source_arena[source_used+k]) fatal("NUL inside source file");
    source_arena[source_used+n]=0;
    source_used+=n+1;
    return i;
}
static void pp_flush(void)
{
    if(raw_used) pp_expand(raw_chunk,raw_used,0,tokens,&token_count,TOKEN_MAX-1);
    raw_used=0;
}
static void pp_file(const char *path,int depth);
static void pp_include(Tok *line,int count,const char *current,int depth)
{
    int n=0;
    pp_expand(line,count,0,replacements[DEPTH_MAX-1],&n,LINE_MAX);
    Tok *ts=replacements[DEPTH_MAX-1];
    char name[64]={0};
    if(n==1 && ts[0].k==T_STR) copy(name,spelling(ts[0].v),sizeof(name));
    else if(n>=3 && ts[0].k=='<' && ts[n-1].k=='>') {
        for(int i=1;i<n-1;i++) { char part[128];
            token_text(ts+i,part,sizeof(part));
            if(length(name)+length(part)>63) fatal("include path too long");
            append(name,part,sizeof(name)); }
    } else fatal("include needs quoted or angle filename");
    char candidate[64];
    copy(candidate,current,sizeof(candidate));
    int end=length(candidate);
    while(end && candidate[end-1]!='/') end--;
    candidate[end]=0;
    if(end+length(name)<64) {
        append(candidate,name,sizeof(candidate));
        if(load_source(candidate)>=0) { pp_file(candidate,depth+1);
            return; }
    }
    copy(candidate,"SYS/INC/",sizeof(candidate));
    if(length(candidate)+length(name)>63) fatal("include path too long");
    append(candidate,name,sizeof(candidate));
    if(load_source(candidate)<0) fatal("include file not found");
    pp_file(candidate,depth+1);
}
static void pp_define(Tok *line,int count,Lexer *lexer,int depth)
{
    if(count<3 || line[2].k!=T_ID) fatal("define needs an identifier");
    int mi=macro_find(line[2].v);
    if(mi<0) { if(macro_count>=MACRO_MAX) fatal("macro capacity exceeded");
        mi=macro_count++; }
    Macro *m=macros+mi;
    m->name=line[2].v;
    m->active=1;
    m->argc=-1;
    m->variadic=0;
    int at=3;
    Lexer adjacent=*lexer;
    adjacent.offset=line_ends[depth][2];
    if(at<count && line[at].k=='(' && lexpeek(&adjacent)=='(') {
        m->argc=0;
        at++;
        while(at<count && line[at].k!=')') {
            if(line[at].k==T_ELLIPSIS) {
                if(m->argc>=MACRO_ARGS) fatal("too many macro parameters");
                m->variadic=1;
                m->params[m->argc++]=intern("__VA_ARGS__",11);
                at++;
                break;
            }
            if(line[at].k!=T_ID || m->argc>=MACRO_ARGS) fatal("invalid macro parameter");
            for(int i=0;i<m->argc;i++) if(m->params[i]==line[at].v) fatal("duplicate macro parameter");
            m->params[m->argc++]=line[at++].v;
            if(at>=count || line[at].k!=',') break;
            at++;
            if(at>=count || line[at].k==')') fatal("missing macro parameter after comma");
        }
        if(at>=count || line[at].k!=')') fatal("macro parameter list is unfinished");
        at++;
    }
    m->first=macro_body_used;
    m->count=count-at;
    if(macro_body_used+m->count>MACRO_BODY_MAX) fatal("macro body capacity exceeded");
    for(;at<count;at++) tokcopy(macro_body+macro_body_used++,line+at);
}
static void pp_directive(Tok *line,int count,Lexer *l,int depth,int baseline)
{
    if(count<2) return;
    int active=!condition_count || conditions[condition_count-1].active;
    if(word(line+1,"if") || word(line+1,"ifdef") || word(line+1,"ifndef")) {
        if(condition_count>=128) fatal("conditional nesting exceeded");
        int truth=0;
        if(active) {
            if(word(line+1,"if")) truth=pp_condition(line+2,count-2);
            else {
                if(count!=3 || line[2].k!=T_ID) fatal("ifdef needs one identifier");
                truth=macro_find(line[2].v)>=0;
                if(word(line+1,"ifndef")) truth=!truth;
            }
        }
        Conditional *c=conditions+condition_count++;
        c->parent=active;
        c->active=active && truth;
        c->taken=c->active;
        c->otherwise=0;
        return;
    }
    if(word(line+1,"elif") || word(line+1,"else") || word(line+1,"endif")) {
        if(condition_count<=baseline) fatal("unmatched conditional directive");
        Conditional *c=conditions+condition_count-1;
        if(word(line+1,"endif")) { condition_count--;
            return; }
        if(c->otherwise) fatal("branch after #else");
        if(word(line+1,"else")) { c->otherwise=1;
            c->active=c->parent && !c->taken;
            c->taken=1; }
        else { c->active=c->parent && !c->taken && pp_condition(line+2,count-2);
            if(c->active) c->taken=1; }
        return;
    }
    if(!active) return;
    if(word(line+1,"define")) pp_define(line,count,l,depth);
    else if(word(line+1,"undef")) {
        if(count!=3 || line[2].k!=T_ID) fatal("undef needs one identifier");
        int i=macro_find(line[2].v);
        if(i>=0) macros[i].active=0;
    } else if(word(line+1,"include")) pp_include(line+2,count-2,sources[l->file].path,depth);
    else if(word(line+1,"pragma") && count==3 && word(line+2,"once")) source_once[l->file]=1;
    else if(word(line+1,"error")) fatal("active #error directive");
    else fatal("unsupported preprocessing directive");
}
static int pp_incomplete(void)
{
    for(int i=0;i<raw_used;i++) if(raw_chunk[i].k==T_ID && macro_alias(raw_chunk[i].v)>=0) {
        if(i+1==raw_used) return 1;
        if(raw_chunk[i+1].k!='(') continue;
        int nesting=0,j=i+1;
        for(;j<raw_used;j++) { if(raw_chunk[j].k=='(') nesting++;
            if(raw_chunk[j].k==')' && --nesting==0) break; }
        if(j==raw_used) return 1;
        i=j;
    }
    return 0;
}
static void pp_file(const char *path,int depth)
{
    if(depth>=DEPTH_MAX) fatal("include depth exceeded");
    int file=load_source(path);
    if(file<0) fatal("source file not found");
    if(source_once[file]) return;
    Lexer l;
    l.source=source_arena+sources[file].source;
    l.offset=0;
    l.line=1;
    l.file=file;
    l.comment=0;
    int baseline=condition_count;
    for(;;) {
        int n=0;
        Tok t;
        for(;;) {
            lexnext(&l,&t);
            if(!t.k || t.k==T_NL) break;
            if(n>=LINE_MAX) fatal("logical line exceeds token capacity");
            tokcopy(lines[depth]+n,&t);
            line_ends[depth][n]=l.offset;
            n++;
        }
        if(n && lines[depth][0].k=='#') {
            pp_flush();
            pp_directive(lines[depth],n,&l,depth,baseline);
        } else if(!condition_count || conditions[condition_count-1].active) {
            if(n) lines[depth][0].where|=(int)0x80000000u;
            for(int i=0;i<n;i++) pp_add(raw_chunk,&raw_used,16384,lines[depth]+i);
            if(raw_used>8192 && !pp_incomplete()) pp_flush();
        }
        if(!t.k) break;
    }
    pp_flush();
    if(condition_count!=baseline) fatal("unfinished conditional in source file");
}
static void preprocess(const char *path)
{
    /* 架构与编译器身份是本工具真实提供的宏，用户可用它选择 API 内联
     * 汇编兼容分支。不是冒充宿主 GCC，也不伪造不提供的标准库能力。 */
    const char *predefined[]={"__SCCC__","__i386__","__STDC_HOSTED__"};
    for(int i=0;i<3;i++) {
        Macro *m=macros+macro_count++;
        m->name=intern(predefined[i],length(predefined[i]));
        m->active=1;
        m->argc=-1;
        m->first=macro_body_used;
        m->count=1;
        make_number(macro_body+macro_body_used++,i==2?0:1,0);
    }
    pp_file(path,0);
    tokens[token_count].k=0;
    tokens[token_count].v=0;
    tokens[token_count].where=token_count?tokens[token_count-1].where:0;
}

/* =====================================================================
 * mio：C 类型图与递归下降语法。类型先决定字节数/指针步长，再建有类型
 * 的表达式节点，后端不重新猜测“这是地址还是整数”。数组在取值时
 * 衰减为指针，在 sizeof/取地址时保留数组身份，因此二维索引的步长
 * 由完整元素类型决定，不硬写成四字节。
 *
 * 局部名字退出作用域后变不可见，符号记录仍保留到函数生成完毕。
 * 若立刻复用符号槽，先前语法树中的变量就会错误指向后一个同名变量。
 * 所有声明先检查容量，聚合/数组大小检查溢出，错误不进入代码生成。
 * ===================================================================== */
#define C_VOID 0
#define C_CHAR 1
#define C_UCHAR 2
#define C_SHORT 3
#define C_USHORT 4
#define C_INT 5
#define C_UINT 6
#define Q_UNSIGNED 1
#define Q_CONST 2
#define Q_VOLATILE 4
#define D_STATIC 1
#define D_EXTERN 2
#define D_TYPEDEF 4
#define D_INLINE 8

static int switch_node;
static char joined_literal[8192];
static Tok *current_token(void) { error_position=tokens[position].where;
    return tokens+position; }
static int take(int k) { if(current_token()->k!=k) return 0;
    position++;
    return 1; }
static int take_word(const char *s) { if(!word(current_token(),s)) return 0;
    position++;
    return 1; }
static void need(int k) { if(!take(k)) fatal("unexpected token; required punctuation is missing"); }
static int new_type(int kind,int size,int base,int count,int fields,int flags)
{
    if(type_count>=TYPE_MAX) fatal("type capacity exceeded");
    int i=type_count++;
    Type *t=types+i;
    t->kind=kind;
    t->size=size;
    t->base=base;
    t->count=count;
    t->fields=fields;
    t->flags=flags;
    return i;
}
static int qualified(int type,int flags)
{
    if((types[type].flags|flags)==types[type].flags) return type;
    Type *t=types+type;
    return new_type(t->kind,t->size,t->base,t->count,t->fields,t->flags|flags);
}
static int pointer_to(int base)
{
    for(int i=0;i<type_count;i++) if(types[i].kind==TY_PTR && types[i].base==base && !types[i].flags) return i;
    return new_type(TY_PTR,4,base,0,0,0);
}
static int array_of(int base,int count)
{
    if(count<0 || (count && types[base].size>0x3D0000/count)) fatal("array size exceeds address space");
    return new_type(TY_ARRAY,types[base].size*count,base,count,0,0);
}
static int alignment(int type)
{
    Type *t=types+type;
    if(t->kind==TY_ARRAY) return alignment(t->base);
    if(t->kind==TY_STRUCT || t->kind==TY_UNION) return t->base?t->base:1;
    return t->size>4?4:t->size?t->size:1;
}
static int promoted(int type)
{
    Type *t=types+type;
    if(t->kind==TY_ARRAY) return pointer_to(t->base);
    if(t->kind==TY_FUNC) return pointer_to(type);
    if(t->kind==TY_CHAR || t->kind==TY_SHORT) return C_INT;
    return type;
}
static int find_symbol(int name,int tags)
{
    for(int i=symbol_count-1;i>0;i--) {
        Symbol *s=symbols+i;
        if(s->name==name && s->scope>=0 && (tags?(s->kind==S_TAG):(s->kind!=S_TAG && s->kind!=S_FIELD))) return i;
    }
    return 0;
}
static int symbol_new(int name,int type,int kind,int value,int scope)
{
    if(symbol_count>=SYMBOL_MAX) fatal("symbol capacity exceeded");
    int i=symbol_count++;
    Symbol *s=symbols+i;
    s->name=name;
    s->type=type;
    s->kind=kind;
    s->value=value;
    s->scope=scope;
    s->defined=0;
    s->next=0;
    s->aux=0;
    return i;
}
static int node_new(int kind,int type,int a,int b,int c,int value)
{
    if(node_count>=NODE_MAX) fatal("function syntax tree capacity exceeded");
    int i=node_count++;
    Node *n=nodes+i;
    n->kind=kind;
    n->type=type;
    n->a=a;
    n->b=b;
    n->c=c;
    n->value=value;
    n->where=error_position;
    n->next=0;
    return i;
}
static int constant(u32 value,int type) { return node_new(N_CONST,type,0,0,0,(int)value); }
static int expression(int minimum);
static int declaration_spec(int *storage);
static int declarator(int base,int *name,int abstract);
static int statement(void);
static int initialize(int type,int symbol,int offset,int global);
static int aggregate(int kind)
{
    int name=0,type=-1,tag=0;
    if(current_token()->k==T_ID) { name=current_token()->v;
        position++;
        tag=find_symbol(name,1);
        if(tag) type=symbols[tag].type; }
    if(type<0) {
        type=new_type(kind,0,1,0,0,0);
        if(name) tag=symbol_new(name,type,S_TAG,0,scope_depth);
    }
    if(types[type].kind!=kind) fatal("aggregate tag kind mismatch");
    if(!take('{')) return type;
    if(types[type].fields) fatal("aggregate is already defined");
    int first=0,last=0,size=0,max_align=1;
    while(!take('}')) {
        int storage=0,base=declaration_spec(&storage);
        if(base<0 || storage) fatal("aggregate member needs an ordinary type");
        do {
            int member=0,mt=declarator(base,&member,0);
            if(!member || !types[mt].size || types[mt].kind==TY_FUNC) fatal("invalid aggregate member");
            for(int f=first;f;f=symbols[f].next) if(symbols[f].name==member) fatal("duplicate aggregate member");
            int align=alignment(mt);
            if(align>max_align) max_align=align;
            int at=kind==TY_UNION?0:(size+align-1)/align*align;
            if(at>0x3D0000-types[mt].size) fatal("aggregate exceeds address space");
            int f=symbol_new(member,mt,S_FIELD,at,-2);
            if(last) symbols[last].next=f;
            else first=f;
            last=f;
            if(at+types[mt].size>size) size=at+types[mt].size;
            if(current_token()->k==':') fatal("bit-fields are unsupported in this version");
        } while(take(','));
        need(';');
    }
    types[type].size=(size+max_align-1)/max_align*max_align;
    types[type].base=max_align;
    types[type].fields=first;
    types[type].count=1;
    (void)tag;
    return type;
}
static int enumeration(void)
{
    if(current_token()->k==T_ID) position++;
    if(!take('{')) return C_INT;
    u32 value=0;
    while(!take('}')) {
        if(current_token()->k!=T_ID) fatal("enum needs an identifier");
        int name=current_token()->v;
        position++;
        if(take('=')) value=constant_value(expression(2));
        int old=find_symbol(name,0);
        if(old && symbols[old].scope==scope_depth) fatal("duplicate enum name");
        symbol_new(name,C_INT,S_ENUM,(int)value,scope_depth);
        value++;
        if(!take(',')) { need('}');
            break; }
    }
    return C_INT;
}
static int declaration_spec(int *storage)
{
    int type=-1,flags=0,sign=0,short_flag=0,long_count=0,builtin=0;
    for(;;) {
        Tok *t=current_token();
        if(take_word("const")) flags|=Q_CONST;
        else if(take_word("volatile")) flags|=Q_VOLATILE;
        else if(take_word("static")) *storage|=D_STATIC;
        else if(take_word("extern")) *storage|=D_EXTERN;
        else if(take_word("typedef")) *storage|=D_TYPEDEF;
        else if(take_word("inline") || take_word("__inline__")) *storage|=D_INLINE;
        else if(take_word("register") || take_word("auto")) {}
        else if(take_word("unsigned")) { sign=1;
            builtin=1; }
        else if(take_word("signed")) { sign=-1;
            builtin=1; }
        else if(take_word("short")) { short_flag=1;
            builtin=1; }
        else if(take_word("long")) { long_count++;
            builtin=1;
            if(long_count>1) fatal("64-bit integers are unsupported"); }
        else if(take_word("int")) { type=C_INT;
            builtin=1; }
        else if(take_word("char")) { type=C_CHAR;
            builtin=1; }
        else if(take_word("void")) type=C_VOID;
        else if(take_word("struct")) type=aggregate(TY_STRUCT);
        else if(take_word("union")) type=aggregate(TY_UNION);
        else if(take_word("enum")) type=enumeration();
        else if(take_word("float") || take_word("double")) fatal("floating point is unsupported");
        else if(t->k==T_ID && type<0 && !builtin) {
            int s=find_symbol(t->v,0);
            if(!s || symbols[s].kind!=S_TYPEDEF) break;
            type=symbols[s].type;
            position++;
        } else break;
    }
    if(type<0 && builtin) type=C_INT;
    if(type<0) return -1;
    if(short_flag) { if(type!=C_INT) fatal("short qualifier needs int");
        type=C_SHORT; }
    if(sign==1) {
        if(type==C_CHAR) type=C_UCHAR;
        else if(type==C_SHORT) type=C_USHORT;
        else if(type==C_INT) type=C_UINT;
        else fatal("unsigned qualifier needs integer type");
    }
    return qualified(type,flags);
}
static int suffix(int base)
{
    if(take('[')) {
        int count=0;
        if(!take(']')) { count=(int)constant_value(expression(2));
            need(']'); }
        int inner=suffix(base);
        return array_of(inner,count);
    }
    if(take('(')) {
        int first=0,last=0,argc=0,variadic=0;
        if(!take(')')) {
            if(word(current_token(),"void") && tokens[position+1].k==')') { position+=2; }
            else for(;;) {
                if(take(T_ELLIPSIS)) { variadic=1;
                    need(')');
                    break; }
                int storage=0,type=declaration_spec(&storage),name=0;
                if(type<0) fatal("parameter needs a type");
                type=declarator(type,&name,1);
                if(types[type].kind==TY_ARRAY) type=pointer_to(types[type].base);
                if(types[type].kind==TY_FUNC) type=pointer_to(type);
                if(!types[type].size || types[type].kind==TY_STRUCT || types[type].kind==TY_UNION) fatal("aggregate by-value parameters are unsupported");
                if(param_count>=PARAM_MAX) fatal("parameter capacity exceeded");
                int p=param_count++;
                parameters[p].name=name;
                parameters[p].type=type;
                parameters[p].next=0;
                if(last) parameters[last].next=p;
                else first=p;
                last=p;
                argc++;
                if(take(')')) break;
                need(',');
            }
        }
        if(types[base].kind==TY_ARRAY || types[base].kind==TY_FUNC || types[base].kind==TY_STRUCT || types[base].kind==TY_UNION) fatal("unsupported function return type");
        return new_type(TY_FUNC,0,base,argc,first,variadic);
    }
    return base;
}
static void replace_hole(int type,int hole,int replacement)
{
    if(types[type].base==hole) { types[type].base=replacement;
        if(types[type].kind==TY_ARRAY) types[type].size=types[replacement].size*types[type].count;
        return;
    }
    if(types[type].kind==TY_PTR || types[type].kind==TY_ARRAY || types[type].kind==TY_FUNC) replace_hole(types[type].base,hole,replacement);
}
static int declarator(int base,int *name,int abstract)
{
    while(take('*')) {
        base=pointer_to(base);
        int flags=0;
        while(word(current_token(),"const") || word(current_token(),"volatile")) {
            if(take_word("const")) flags|=Q_CONST;
            else { position++;
                flags|=Q_VOLATILE; }
        }
        base=qualified(base,flags);
    }
    if(take('(')) {
        int hole=new_type(TY_VOID,0,0,0,0,0);
        int inner=declarator(hole,name,abstract);
        need(')');
        int outside=suffix(base);
        if(inner==hole) return outside;
        replace_hole(inner,hole,outside);
        return inner;
    }
    if(current_token()->k==T_ID) { *name=current_token()->v;
        position++; }
    else if(!abstract) fatal("declaration needs an identifier");
    return suffix(base);
}
static int type_ahead(void)
{
    Tok *t=current_token();
    const char *words[]={"void","char","short","int","long","unsigned","signed","struct","union","enum","const","volatile"};
    for(int i=0;i<12;i++) if(word(t,words[i])) return 1;
    int s=t->k==T_ID?find_symbol(t->v,0):0;
    return s && symbols[s].kind==S_TYPEDEF;
}
static int lvalue(int n)
{ return (nodes[n].kind==N_SYMBOL && symbols[nodes[n].value].kind!=S_FUNCTION) || nodes[n].kind==N_DEREF; }
static int binary(int op,int left,int right)
{
    int a=promoted(nodes[left].type),b=promoted(nodes[right].type),type=C_INT;
    if(op=='+' || op=='-') {
        if(types[a].kind==TY_PTR) type=a;
        else if(op=='+' && types[b].kind==TY_PTR) type=b;
        if(op=='-' && types[a].kind==TY_PTR && types[b].kind==TY_PTR) type=C_INT;
    }
    if(type==C_INT && op!=T_EQ && op!=T_NE && op!=T_LE && op!=T_GE && op!='<' && op!='>' && op!=T_AND && op!=T_OR)
        type=(types[a].flags|types[b].flags)&Q_UNSIGNED?C_UINT:C_INT;
    return node_new(N_BINARY,type,left,right,0,op);
}
static int primary(void)
{
    Tok *t=current_token();
    int n=0;
    if(t->k==T_NUM || t->k==T_UNUM) { n=constant(number(t),t->k==T_UNUM?C_UINT:C_INT);
        position++; }
    else if(t->k==T_STR) {
        int bytes=0;
        char *literal=joined_literal;
        while(current_token()->k==T_STR) {
            int at=current_token()->v,size=text_size(at);
            if(bytes+size>=8192) fatal("joined string literal exceeds 8191 bytes");
            for(int i=0;i<size;i++) literal[bytes++]=pool[at+i];
            position++;
        }
        if(data_used+bytes+1>DATA_MAX) fatal("initialized data capacity exceeded");
        int at=data_used;
        for(int i=0;i<bytes;i++) data[data_used++]=(u8)literal[i];
        data[data_used++]=0;
        n=node_new(N_STRING,array_of(C_CHAR,bytes+1),0,0,0,at);
    } else if(t->k==T_ID) {
        int name=t->v,s=find_symbol(name,0);
        position++;
        if(!s && current_token()->k=='(') {
            int type=new_type(TY_FUNC,0,C_INT,-1,0,0);
            s=symbol_new(name,type,S_FUNCTION,0,0);
        }
        if(!s || symbols[s].kind==S_TYPEDEF) fatal("unknown identifier");
        if(symbols[s].kind==S_ENUM) n=constant((u32)symbols[s].value,C_INT);
        else n=node_new(N_SYMBOL,symbols[s].type,0,0,0,s);
    } else if(take('(')) { n=expression(1);
    need(')'); }
    else fatal("expression needs a value");
    for(;;) {
        if(take('[')) {
            int subscript=expression(1);
            need(']');
            int pt=promoted(nodes[n].type);
            if(types[pt].kind!=TY_PTR) fatal("subscript needs an array or pointer");
            n=node_new(N_DEREF,types[pt].base,binary('+',n,subscript),0,0,0);
        } else if(current_token()->k=='.' || current_token()->k==T_ARROW) {
            int arrow=current_token()->k==T_ARROW;
            position++;
            int type=nodes[n].type;
            if(arrow) {
                type=promoted(type);
                if(types[type].kind!=TY_PTR) fatal("arrow needs a pointer");
                type=types[type].base;
                n=node_new(N_DEREF,type,n,0,0,0);
            }
            if(types[type].kind!=TY_STRUCT && types[type].kind!=TY_UNION) fatal("member access needs aggregate");
            if(current_token()->k!=T_ID) fatal("member access needs a name");
            int f=types[type].fields;
            while(f && symbols[f].name!=current_token()->v) f=symbols[f].next;
            if(!f) fatal("unknown aggregate member");
            position++;
            int field_type=qualified(symbols[f].type,types[type].flags&(Q_CONST|Q_VOLATILE));
            int address=node_new(N_ADDR,pointer_to(type),n,0,0,0);
            /* 字段偏移以字节加，不允许普通指针步长再次乘 sizeof(struct)。 */
            address=node_new(N_CAST,pointer_to(C_CHAR),address,0,0,0);
            address=binary('+',address,constant((u32)symbols[f].value,C_INT));
            n=node_new(N_DEREF,field_type,address,0,0,0);
        } else if(take('(')) {
            int type=promoted(nodes[n].type);
            if(types[type].kind!=TY_PTR || types[types[type].base].kind!=TY_FUNC) fatal("call needs a function");
            type=types[type].base;
            int args=0,argc=0;
            if(!take(')')) do {
                int value=expression(2);
                args=node_new(N_ARG,nodes[value].type,value,args,0,0);
                argc++;
                if(take(')')) break;
                need(',');
            } while(1);
            if(types[type].count>=0 && ((!types[type].flags && argc!=types[type].count) || (types[type].flags && argc<types[type].count))) fatal("function argument count mismatch");
            n=node_new(N_CALL,types[type].base,n,args,0,argc);
        } else if(current_token()->k==T_INC || current_token()->k==T_DEC) {
            int delta=current_token()->k==T_INC?1:-1;
            position++;
            if(!lvalue(n) || (types[nodes[n].type].flags&Q_CONST)) fatal("increment needs a writable lvalue");
            n=node_new(N_INC,nodes[n].type,n,0,1,delta);
        } else break;
    }
    return n;
}
static int unary(void)
{
    if(take_word("sizeof")) {
        int type;
        if(current_token()->k=='(') {
            int saved=position;
            position++;
            if(type_ahead()) { int storage=0,name=0;
                type=declaration_spec(&storage);
                type=declarator(type,&name,1);
                need(')'); }
            else { position=saved;
                type=nodes[unary()].type; }
        } else type=nodes[unary()].type;
        if(!types[type].size) fatal("sizeof needs a complete object type");
        return constant((u32)types[type].size,C_UINT);
    }
    if(current_token()->k=='(') {
        int saved=position;
        position++;
        if(type_ahead()) {
            int storage=0,name=0,type=declaration_spec(&storage);
            type=declarator(type,&name,1);
            need(')');
            return node_new(N_CAST,type,unary(),0,0,0);
        }
        position=saved;
    }
    int op=current_token()->k;
    if(op=='*' || op=='&' || op=='+' || op=='-' || op=='!' || op=='~' || op==T_INC || op==T_DEC) {
        position++;
        int a=unary(),type=promoted(nodes[a].type);
        if(op=='*') { if(types[type].kind!=TY_PTR) fatal("dereference needs pointer");
            return node_new(N_DEREF,types[type].base,a,0,0,0); }
        if(op=='&') { if(!lvalue(a) && nodes[a].kind!=N_STRING && types[nodes[a].type].kind!=TY_FUNC) fatal("address needs an object");
            return node_new(N_ADDR,pointer_to(nodes[a].type),a,0,0,0); }
        if(op==T_INC || op==T_DEC) {
            if(!lvalue(a) || (types[nodes[a].type].flags&Q_CONST)) fatal("increment needs writable lvalue");
            return node_new(N_INC,nodes[a].type,a,0,0,op==T_INC?1:-1);
        }
        return node_new(N_UNARY,op=='!'?C_INT:type,a,0,0,op);
    }
    return primary();
}
static int precedence(int op)
{
    if(op==',') return 1;
    if(op=='=' || (op>=T_ADDSET && op<=T_SHRSET)) return 2;
    if(op=='?') return 3;
    int p=pp_precedence(op);
    return p?p+3:0;
}
static int expression(int minimum)
{
    int left=unary();
    for(;;) {
        int op=current_token()->k,p=precedence(op);
        if(p<minimum) break;
        position++;
        if(op=='?') {
            int yes=expression(1);
            need(':');
            int no=expression(3);
            int type=promoted(nodes[yes].type);
            if(types[type].kind!=TY_PTR && (types[promoted(nodes[no].type)].flags&Q_UNSIGNED)) type=C_UINT;
            left=node_new(N_COND,type,left,yes,no,0);
        } else {
            int assignment=p==2,right=expression(assignment?p:p+1);
            if(assignment) {
                if(!lvalue(left) || (types[nodes[left].type].flags&Q_CONST) || types[nodes[left].type].kind==TY_ARRAY) fatal("assignment needs writable lvalue");
                left=node_new(N_ASSIGN,nodes[left].type,left,right,0,op);
            } else if(op==',') left=node_new(N_COMMA,nodes[right].type,left,right,0,0);
            else left=binary(op,left,right);
        }
    }
    return left;
}

/* 声明/初始化与语句部分接在同一个源片段中，后端逐函数消费完成的树。 */
/* 初始化树保留“目标符号 + 字节偏移”，不用伪造普通赋值去绕 const。
 * 聚合花括号先生成整对象清零，再覆盖显式元素；省略元素因而确实为零。
 * 不完整数组在见到最后一个元素后补大小，局部栈槽随后才统一分配。
 * 全局初始化也使用这棵树，但后端只接受编译期常量/地址重定位。
 */
static void chain(int *first,int *last,int item)
{
    if(!item) return;
    if(*last) nodes[*last].next=item;
    else *first=item;
    while(nodes[item].next) item=nodes[item].next;
    *last=item;
}
static int initialize(int type,int symbol,int offset,int global)
{
    Type *t=types+type;
    int first=0,last=0;
    if(t->kind==TY_ARRAY && types[t->base].kind==TY_CHAR && current_token()->k==T_STR) {
        int string=primary(),n=types[nodes[string].type].count;
        if(!t->count) { t->count=n;
            t->size=n*types[t->base].size; }
        if(t->count<n-1) fatal("string initializer exceeds array");
        int from=nodes[string].value;
        for(int i=0;i<t->count;i++) {
            int value=constant(i<n?data[from+i]:0,C_INT);
            chain(&first,&last,node_new(N_INIT,t->base,symbol,offset+i,value,0));
        }
    } else if(take('{')) {
        if(t->kind==TY_ARRAY || t->kind==TY_STRUCT || t->kind==TY_UNION) {
            chain(&first,&last,node_new(N_INIT,type,symbol,offset,0,1));
            int i=0,field=t->fields;
            while(!take('}')) {
                int element,at;
                if(t->kind==TY_ARRAY) {
                    if(t->count && i>=t->count) fatal("too many array initializers");
                    element=t->base;
                    at=offset+i*types[element].size;
                } else {
                    if(!field) fatal("too many aggregate initializers");
                    element=symbols[field].type;
                    at=offset+symbols[field].value;
                }
                chain(&first,&last,initialize(element,symbol,at,global));
                i++;
                if(t->kind!=TY_ARRAY) field=t->kind==TY_UNION?0:symbols[field].next;
                if(take('}')) break;
                need(',');
            }
            if(t->kind==TY_ARRAY && !t->count) {
                if(!i) fatal("empty initializer cannot infer array size");
                t->count=i;
                t->size=i*types[t->base].size;
                if(t->size<0 || t->size>0x3D0000) fatal("inferred array exceeds address space");
            }
        } else {
            chain(&first,&last,initialize(type,symbol,offset,global));
            take(',');
            need('}');
        }
    } else {
        if(t->kind==TY_ARRAY) fatal("array initializer needs braces or string");
        int value=expression(2);
        if((t->kind==TY_STRUCT || t->kind==TY_UNION) && nodes[value].type!=type) fatal("aggregate initializer type mismatch");
        chain(&first,&last,node_new(N_INIT,type,symbol,offset,value,0));
    }
    (void)global;
    return first;
}
static void scope_close(int depth)
{
    for(int i=1;i<symbol_count;i++) if(symbols[i].scope==depth) symbols[i].scope=-1;
}
static int declaration_ahead(void)
{
    return type_ahead() || word(current_token(),"static") || word(current_token(),"extern")
        || word(current_token(),"typedef") || word(current_token(),"register")
        || word(current_token(),"auto") || word(current_token(),"inline");
}
static int local_declaration(void)
{
    int storage=0,base=declaration_spec(&storage),first=0,last=0;
    if(base<0) fatal("local declaration needs a type");
    if(take(';')) return 0;
    do {
        int name=0,type=declarator(base,&name,0),old=find_symbol(name,0),s;
        if(old && symbols[old].scope==scope_depth) fatal("duplicate local name");
        if(storage&D_TYPEDEF) s=symbol_new(name,type,S_TYPEDEF,0,scope_depth);
        else if(types[type].kind==TY_FUNC || (storage&D_EXTERN)) {
            if(old && symbols[old].scope==0) s=old;
            else s=symbol_new(name,type,types[type].kind==TY_FUNC?S_FUNCTION:S_GLOBAL,0,0);
        } else s=symbol_new(name,type,(storage&D_STATIC)?S_GLOBAL:S_LOCAL,0,scope_depth);
        int init=0;
        if(take('=')) {
            if((storage&D_TYPEDEF) || (storage&D_EXTERN) || types[type].kind==TY_FUNC) fatal("invalid initializer on declaration");
            init=initialize(type,s,0,storage&D_STATIC);
        }
        if(symbols[s].kind==S_LOCAL) {
            if(!types[type].size) fatal("local object needs complete type");
            local_bytes=(local_bytes+types[type].size+3)/4*4;
            if(local_bytes>65536) fatal("local frame exceeds 64KB");
            symbols[s].value=-local_bytes;
            symbols[s].defined=1;
            chain(&first,&last,init);
        } else if((storage&D_STATIC) && !(storage&D_TYPEDEF)) define_global(s,init);
    } while(take(','));
    need(';');
    return first?node_new(N_BLOCK,C_VOID,first,0,0,0):0;
}
static int asm_operand_list(int output)
{
    int first=0,last=0;
    if(current_token()->k==':' || current_token()->k==')') return 0;
    do {
        if(current_token()->k!=T_STR) fatal("asm operand needs a constraint string");
        int constraint=current_token()->v;
        position++;
        need('(');
        int value=expression(1);
        need(')');
        if(output && !lvalue(value)) fatal("asm output must be an lvalue");
        chain(&first,&last,node_new(N_ARG,nodes[value].type,value,0,0,constraint));
    } while(take(','));
    return first;
}
static int asm_statement(void)
{
    take_word("volatile");
    take_word("__volatile__");
    need('(');
    if(current_token()->k!=T_STR) fatal("asm template needs a string");
    int text=primary(),outputs=0,inputs=0;
    if(take(':')) {
        outputs=asm_operand_list(1);
        if(take(':')) {
            inputs=asm_operand_list(0);
            if(take(':')) {
                while(current_token()->k!=')') {
                    if(current_token()->k!=T_STR) fatal("asm clobber needs a string");
                    /* 固定寄存器后端不跨表达式缓存值；memory/cc 仍解析，
                     * 并要求所有非保留寄存器更改显式写在模板或约束里。 */
                    position++;
                    if(!take(',')) break;
                }
            }
        }
    }
    need(')');
    need(';');
    return node_new(N_ASM,C_VOID,text,outputs,inputs,0);
}
static int statement(void)
{
    if(current_token()->k==T_ID && tokens[position+1].k==':') {
        int name=current_token()->v;
        position+=2;
        return node_new(N_LABEL,C_VOID,statement(),0,0,name);
    }
    if(take('{')) {
        scope_depth++;
        int first=0,last=0;
        while(!take('}')) {
            if(!current_token()->k) fatal("unfinished block");
            chain(&first,&last,declaration_ahead()?local_declaration():statement());
        }
        scope_close(scope_depth--);
        return node_new(N_BLOCK,C_VOID,first,0,0,0);
    }
    if(take_word("if")) {
        need('(');
        int test=expression(1);
        need(')');
        int yes=statement(),no=0;
        if(take_word("else")) no=statement();
        return node_new(N_IF,C_VOID,test,yes,no,0);
    }
    if(take_word("while")) {
        need('(');
        int test=expression(1);
        need(')');
        return node_new(N_WHILE,C_VOID,test,statement(),0,0);
    }
    if(take_word("do")) {
        int body=statement();
        if(!take_word("while")) fatal("do needs while");
        need('(');
        int test=expression(1);
        need(')');
        need(';');
        return node_new(N_DO,C_VOID,test,body,0,0);
    }
    if(take_word("for")) {
        need('(');
        scope_depth++;
        int init=0,test=0,step=0;
        if(declaration_ahead()) init=local_declaration();
        else if(!take(';')) { init=node_new(N_EXPR,C_VOID,expression(1),0,0,0);
            need(';'); }
        if(!take(';')) { test=expression(1);
            need(';'); }
        if(!take(')')) { step=expression(1);
            need(')'); }
        int loop=node_new(N_FOR,C_VOID,test,statement(),step,0),first=0,last=0;
        chain(&first,&last,init);
        chain(&first,&last,loop);
        scope_close(scope_depth--);
        return node_new(N_BLOCK,C_VOID,first,0,0,0);
    }
    if(take_word("switch")) {
        need('(');
        int test=expression(1);
        need(')');
        int previous=switch_node;
        int n=node_new(N_SWITCH,C_VOID,test,0,0,0);
        switch_node=n;
        nodes[n].b=statement();
        switch_node=previous;
        return n;
    }
    if(word(current_token(),"case") || word(current_token(),"default")) {
        if(!switch_node) fatal("case outside switch");
        int otherwise=take_word("default"),value=0;
        if(!otherwise) { position++;
            value=(int)constant_value(expression(2)); }
        need(':');
        for(int c=nodes[switch_node].c;c;c=nodes[c].b)
            if((otherwise && nodes[c].type) || (!otherwise && !nodes[c].type && nodes[c].value==value)) fatal("duplicate switch label");
        int n=node_new(N_CASE,otherwise,0,nodes[switch_node].c,0,value);
        nodes[switch_node].c=n;
        nodes[n].a=statement();
        return n;
    }
    if(take_word("return")) {
        int value=0;
        if(!take(';')) { value=expression(1);
            need(';'); }
        int result=types[symbols[function_symbol].type].base;
        if((types[result].kind==TY_VOID && value) || (types[result].kind!=TY_VOID && !value)) fatal("return value disagrees with function type");
        return node_new(N_RETURN,result,value,0,0,0);
    }
    if(take_word("break")) { need(';');
        return node_new(N_BREAK,C_VOID,0,0,0,0); }
    if(take_word("continue")) { need(';');
        return node_new(N_CONTINUE,C_VOID,0,0,0,0); }
    if(take_word("__asm__") || take_word("asm")) return asm_statement();
    if(take_word("goto")) {
        if(current_token()->k!=T_ID) fatal("goto needs a label name");
        int name=current_token()->v;
        position++;
        need(';');
        return node_new(N_GOTO,C_VOID,0,0,0,name);
    }
    if(take(';')) return 0;
    int n=node_new(N_EXPR,C_VOID,expression(1),0,0,0);
    need(';');
    return n;
}
static void compile_unit(void)
{
    type_count=0;
    symbol_count=param_count=node_count=1;
    position=0;
    new_type(TY_VOID,0,0,0,0,0);
    new_type(TY_CHAR,1,0,0,0,0);
    new_type(TY_CHAR,1,0,0,0,Q_UNSIGNED);
    new_type(TY_SHORT,2,0,0,0,0);
    new_type(TY_SHORT,2,0,0,0,Q_UNSIGNED);
    new_type(TY_INT,4,0,0,0,0);
    new_type(TY_INT,4,0,0,0,Q_UNSIGNED);
    /* 入口与最终 main 调用由后端 finish_image 统一生成，函数偏移从
     * 预留的 24 字节之后算起。绝对地址仍须等代码/数据布局确定后回填。 */
    code_used=24;
    copy(map_text,"SCSYM1MIO\n",sizeof(map_text));
    map_used=length(map_text);
    while(current_token()->k) {
        int storage=0,base=declaration_spec(&storage);
        if(base<0) fatal("top-level declaration needs a type");
        if(take(';')) continue;
        int function_body=0;
        do {
            int name=0,type=declarator(base,&name,0),s=find_symbol(name,0);
            if(storage&D_TYPEDEF) {
                if(s && symbols[s].kind!=S_TYPEDEF) fatal("typedef conflicts with object");
                if(!s) s=symbol_new(name,type,S_TYPEDEF,0,0);
            } else {
                int kind=types[type].kind==TY_FUNC?S_FUNCTION:S_GLOBAL;
                if(s && symbols[s].kind!=kind) fatal("declaration kind conflicts with previous name");
                if(!s) s=symbol_new(name,type,kind,0,0);
                symbols[s].type=type;
                if(kind==S_FUNCTION && current_token()->k=='{') {
                    if(symbols[s].defined) fatal("function is already defined");
                    function_symbol=s;
                    scope_depth=1;
                    local_bytes=0;
                    node_count=1;
                    int index=0;
                    for(int p=types[type].fields;p;p=parameters[p].next) {
                        if(!parameters[p].name) fatal("function definition parameter needs a name");
                        symbol_new(parameters[p].name,parameters[p].type,S_PARAM,8+4*index++,1);
                    }
                    int body=statement();
                    symbols[s].defined=1;
                    emit_function(s,body);
                    function_body=1;
                    scope_close(1);
                    scope_depth=0;
                    function_symbol=0;
                    node_count=1;
                    break;
                }
                if(kind==S_GLOBAL) {
                    int init=0;
                    if(take('=')) init=initialize(type,s,0,1);
                    if(!(storage&D_EXTERN) || init) define_global(s,init);
                } else if(take('=')) fatal("function cannot have initializer");
            }
        } while(take(','));
        /* 函数体已经吃掉自己的 }，其后不能强制要求分号；普通声明必须。 */
        if(!function_body) need(';');
        node_count=1;
    }
}

/* =====================================================================
 * mio：SCCC 的原生 x86 后端。这里产生真正的 32 位指令，不解释语法树。
 * 表达式结果在 EAX，暂存使用机器栈；ECX/EDX 是临时寄存器。每个函数
 * 统一保存 EBX/ESI/EDI/EBP，即使内联汇编约束选用保留寄存器也遵守
 * cdecl。绝对地址等代码/数据/BSS 布局全部确定后回填；磁盘保存的只有
 * 代码与初始化数据，BSS 交给 SCX 加载器清零。
 *
 * 分支采用 rel32，故前向/后向跳转不受 8 位位移限制。break/continue/
 * return 链表临时借用跳转位移字段，补齐时立即变成真实位移，不另设
 * 固定数量很小的“每循环 break 数”数组。
 * ===================================================================== */
#define F_REL 1
#define F_ABS 2
#define F_DATA 3
static int return_jumps,control_depth;
static int break_jumps[64],continue_jumps[64],control_loop[64];
static int label_names[128],label_targets[128],label_jumps[128],label_count;
static int label_find(int name)
{
    for(int i=0;i<label_count;i++) if(label_names[i]==name) return i;
    if(label_count>=128) fatal("function label capacity exceeded");
    int i=label_count++;
    label_names[i]=name;
    label_targets[i]=-1;
    label_jumps[i]=-1;
    return i;
}
static void byte(int value)
{
    if(code_used>=CODE_MAX) fatal("machine code capacity exceeded");
    code[36+code_used++]=(u8)value;
}
static void dword(u32 value) { for(int i=0;i<4;i++) { byte((int)(value&255));
        value>>=8; } }
static void put32(u8 *out,u32 value)
{ for(int i=0;i<4;i++) { out[i]=(u8)value;
        value>>=8; } }
static u32 get32(u8 *in) { return (u32)in[0]|((u32)in[1]<<8)|((u32)in[2]<<16)|((u32)in[3]<<24); }
static void fixed(int kind,int symbol,int addend,int offset)
{
    if(fix_count>=FIX_MAX) fatal("relocation capacity exceeded");
    Fix *f=fixes+fix_count++;
    f->kind=kind;
    f->symbol=symbol;
    f->addend=addend;
    f->offset=offset;
}
static void immediate(int reg,u32 value) { byte(0xB8+reg);
    dword(value); }
static void move_reg(int dst,int src) { byte(0x89);
    byte(0xC0+src*8+dst); }
static void stack_add(int n) { if(n) { byte(0x81);
        byte(0xC4);
        dword((u32)n); } }
static void memory(int reg,int base,int displacement)
{
    /* 总用 disp32，避免正负边界的多套编码；ESP 的 SIB 必须显式写。
     * base=-1 为绝对地址形式，调用者可以把位移四字节留给重定位。 */
    if(base<0) { byte(reg*8+5);
        dword((u32)displacement); }
    else { byte(0x80+reg*8+base);
        if(base==4) byte(0x24);
        dword((u32)displacement); }
}
static int jump(int condition)
{
    if(condition<0) byte(0xE9);
    else { byte(0x0F);
        byte(0x80+condition); }
    int at=code_used;
    dword(0);
    return at;
}
static void patch(int at,int destination) { put32(code+36+at,(u32)(destination-at-4)); }
static void jump_link(int *head)
{ int at=jump(-1);
    put32(code+36+at,(u32)*head);
    *head=at; }
static void patch_chain(int head,int destination)
{
    while(head>=0) { int next=(int)get32(code+36+head);
        patch(head,destination);
        head=next; }
}
static void test_eax(void) { byte(0x85);
    byte(0xC0); }
static void bool_result(int condition) { byte(0x0F);
    byte(0x90+condition);
    byte(0xC0);
    byte(0x0F);
    byte(0xB6);
    byte(0xC0); }
static void cast_value(int type)
{
    Type *t=types+type;
    if(t->kind==TY_CHAR || t->kind==TY_SHORT) {
        byte(0x0F);
        byte((t->flags&Q_UNSIGNED)?(t->size==1?0xB6:0xB7):(t->size==1?0xBE:0xBF));
        byte(0xC0);
    }
}
static void load_value(int type)
{
    Type *t=types+type;
    if(t->kind==TY_ARRAY || t->kind==TY_FUNC || t->kind==TY_STRUCT || t->kind==TY_UNION) return;
    if(t->size==1 || t->size==2) { byte(0x0F);
        byte((t->flags&Q_UNSIGNED)?(t->size==1?0xB6:0xB7):(t->size==1?0xBE:0xBF));
        byte(0x00); }
    else if(t->size==4) { byte(0x8B);
        byte(0x00); }
    else fatal("cannot load incomplete object");
}
static void store_value(int type)
{
    Type *t=types+type;
    /* EDX 为目的地址，EAX 为值。聚合值本身是来源地址；按实际大小
     * 复制，包括尾部 1/2 字节，不能用 size/4 丢掉末尾成员。 */
    if(t->kind==TY_STRUCT || t->kind==TY_UNION) {
        for(int i=0;i<t->size;) {
            if(i+4<=t->size) { byte(0x8B);
                memory(1,0,i);
                byte(0x89);
                memory(1,2,i);
                i+=4; }
            else { byte(0x8A);
                memory(1,0,i);
                byte(0x88);
                memory(1,2,i);
                i++; }
        }
        move_reg(0,2);
        return;
    }
    cast_value(type);
    if(t->size==2) byte(0x66);
    byte(t->size==1?0x88:0x89);
    byte(0x02);
}
static void symbol_address(int symbol,int extra)
{
    Symbol *s=symbols+symbol;
    if(s->kind==S_LOCAL || s->kind==S_PARAM) { byte(0x8D);
        memory(0,5,s->value+extra); }
    else {
        byte(0xB8);
        fixed(F_ABS,symbol,extra,code_used);
        dword(0);
    }
}
static void emit_expression(int node);
static void address(int node)
{
    Node *n=nodes+node;
    if(n->kind==N_SYMBOL) symbol_address(n->value,0);
    else if(n->kind==N_DEREF) emit_expression(n->a);
    else if(n->kind==N_STRING) { byte(0xB8);
        fixed(F_DATA,0,n->value,code_used);
        dword(0); }
    else fatal("code generation needs an addressable object");
}
static int compound_operator(int op)
{
    if(op==T_ADDSET) return '+';
    if(op==T_SUBSET) return '-';
    if(op==T_MULSET) return '*';
    if(op==T_DIVSET) return '/';
    if(op==T_MODSET) return '%';
    if(op==T_ANDSET) return '&';
    if(op==T_ORSET) return '|';
    if(op==T_XORSET) return '^';
    if(op==T_SHLSET) return T_SHL;
    if(op==T_SHRSET) return T_SHR;
    return op;
}
static void scale(int reg,int n)
{ if(n!=1) { if(n<=0) fatal("arithmetic needs complete pointer element");
        byte(0x69);
        byte(0xC0+reg*9);
        dword((u32)n); } }
static void operation(int op,int left_type,int right_type)
{
    int a=promoted(left_type),b=promoted(right_type),pa=types[a].kind==TY_PTR,pb=types[b].kind==TY_PTR;
    int unsigned_math=((types[a].flags|types[b].flags)&Q_UNSIGNED) || pa || pb;
    if((op=='+' || op=='-') && pa && !pb) scale(1,types[types[a].base].size);
    if(op=='+' && !pa && pb) scale(0,types[types[b].base].size);
    if(op=='+') { byte(0x01);
        byte(0xC8); }
    else if(op=='-') {
        byte(0x29);
        byte(0xC8);
        if(pa && pb) { int size=types[types[a].base].size;
            if(size!=1) { immediate(1,(u32)size);
                byte(0x99);
                byte(0xF7);
                byte(0xF9); }
        }
    } else if(op=='*') { byte(0x0F);
    byte(0xAF);
    byte(0xC1); }
    else if(op=='/' || op=='%') {
        if(unsigned_math) { byte(0x31);
            byte(0xD2); } else byte(0x99);
        byte(0xF7);
        byte(unsigned_math?0xF1:0xF9);
        if(op=='%') move_reg(0,2);
    } else if(op=='&') { byte(0x21);
    byte(0xC8); }
    else if(op=='|') { byte(0x09);
        byte(0xC8); }
    else if(op=='^') { byte(0x31);
        byte(0xC8); }
    else if(op==T_SHL || op==T_SHR) { byte(0xD3);
        byte(op==T_SHL?0xE0:unsigned_math?0xE8:0xF8); }
    else {
        byte(0x39);
        byte(0xC8);
        int cc=op==T_EQ?4:op==T_NE?5:op=='<'?(unsigned_math?2:12):op=='>'?(unsigned_math?7:15):op==T_LE?(unsigned_math?6:14):op==T_GE?(unsigned_math?3:13):-1;
        if(cc<0) fatal("unsupported binary operator");
        bool_result(cc);
    }
}
static void emit_expression(int node)
{
    if(!node) { immediate(0,0);
        return; }
    Node *n=nodes+node;
    error_position=n->where;
    int kind=n->kind;
    if(kind==N_CONST) immediate(0,(u32)n->value);
    else if(kind==N_SYMBOL || kind==N_DEREF || kind==N_STRING) { address(node);
        load_value(n->type); }
    else if(kind==N_ADDR) address(n->a);
    else if(kind==N_CAST) { emit_expression(n->a);
        cast_value(n->type); }
    else if(kind==N_COMMA) { emit_expression(n->a);
        emit_expression(n->b); }
    else if(kind==N_UNARY) {
        emit_expression(n->a);
        if(n->value=='-') { byte(0xF7);
            byte(0xD8); }
        else if(n->value=='~') { byte(0xF7);
            byte(0xD0); }
        else if(n->value=='!') { test_eax();
            bool_result(4); }
    } else if(kind==N_BINARY) {
        emit_expression(n->a);
        if(n->value==T_AND || n->value==T_OR) {
            test_eax();
            int end=jump(n->value==T_AND?4:5);
            emit_expression(n->b);
            test_eax();
            bool_result(5);
            int done=jump(-1);
            patch(end,code_used);
            immediate(0,n->value==T_OR);
            patch(done,code_used);
        } else {
            byte(0x50);
            emit_expression(n->b);
            move_reg(1,0);
            byte(0x58);
            operation(n->value,nodes[n->a].type,nodes[n->b].type);
        }
    } else if(kind==N_COND) {
        emit_expression(n->a);
        test_eax();
        int no=jump(4);
        emit_expression(n->b);
        int done=jump(-1);
        patch(no,code_used);
        emit_expression(n->c);
        patch(done,code_used);
    } else if(kind==N_ASSIGN) {
        address(n->a);
        byte(0x50);
        if(n->value=='=') emit_expression(n->b);
        else {
            load_value(n->type);
            byte(0x50);
            emit_expression(n->b);
            move_reg(1,0);
            byte(0x58);
            operation(compound_operator(n->value),n->type,nodes[n->b].type);
        }
        byte(0x5A);
        store_value(n->type);
    } else if(kind==N_INC) {
        address(n->a);
        byte(0x50);
        load_value(n->type);
        if(n->c) byte(0x50);
        int type=promoted(n->type),delta=n->value;
        if(types[type].kind==TY_PTR) delta*=types[types[type].base].size;
        byte(0x05);
        dword((u32)delta);
        if(n->c) { byte(0x8B);
            memory(2,4,4);
            store_value(n->type);
            byte(0x58);
            stack_add(4); }
        else { byte(0x5A);
            store_value(n->type); }
    } else if(kind==N_CALL) {
        /* 参数树按源码顺序倒链，恰好对应 cdecl 从右往左压栈。
         * 间接目标先入栈，求参数不会破坏函数指针；CALL 后一并平衡。 */
        int direct=nodes[n->a].kind==N_SYMBOL && symbols[nodes[n->a].value].kind==S_FUNCTION;
        if(!direct) { emit_expression(n->a);
            byte(0x50); }
        for(int arg=n->b;arg;arg=nodes[arg].b) { emit_expression(nodes[arg].a);
            byte(0x50); }
        if(direct) { byte(0xE8);
            fixed(F_REL,nodes[n->a].value,0,code_used);
            dword(0); }
        else { byte(0x8B);
            memory(0,4,n->value*4);
            byte(0xFF);
            byte(0xD0); }
        stack_add(n->value*4+(direct?0:4));
        cast_value(n->type);
    } else fatal("unsupported value node");
}
/* 编译期计算与运行时生成分别实现并互相测试，不能用宿主 signed
 * 溢出行为定义目标整数语义。加减乘在 u32 环上运算，比较/除法再依据
 * 类型选择有符号；条件与逻辑短路不访问未选中的除零表达式。 */
static u32 constant_value(int node)
{
    Node *n=nodes+node;
    error_position=n->where;
    if(n->kind==N_CONST) return (u32)n->value;
    if(n->kind==N_CAST) {
        u32 v=constant_value(n->a);
        Type *t=types+n->type;
        if(t->size==1) return (t->flags&Q_UNSIGNED)?v&255:(u32)(int)(char)v;
        if(t->size==2) return (t->flags&Q_UNSIGNED)?v&65535:(u32)(int)(short)v;
        return v;
    }
    if(n->kind==N_UNARY) { u32 v=constant_value(n->a);
        return n->value=='-'?0u-v:n->value=='~'?~v:n->value=='!'?!v:v; }
    if(n->kind==N_COND) return constant_value(constant_value(n->a)?n->b:n->c);
    if(n->kind==N_COMMA) { constant_value(n->a);
        return constant_value(n->b); }
    if(n->kind!=N_BINARY) fatal("initializer requires a constant expression");
    u32 a=constant_value(n->a),b;
    int op=n->value;
    if(op==T_AND && !a) return 0;
    if(op==T_OR && a) return 1;
    b=constant_value(n->b);
    int uns=(types[promoted(nodes[n->a].type)].flags|types[promoted(nodes[n->b].type)].flags)&Q_UNSIGNED;
    if(op=='+') return a+b;
    if(op=='-') return a-b;
    if(op=='*') return a*b;
    if(op=='/' || op=='%') {
        if(!b) fatal("division by zero in constant expression");
        if(uns) return op=='/'?a/b:a%b;
        if(a==0x80000000u && b==0xFFFFFFFFu) fatal("signed constant division overflow");
        return op=='/'?(u32)((int)a/(int)b):(u32)((int)a%(int)b);
    }
    if(op=='&') return a&b;
    if(op=='|') return a|b;
    if(op=='^') return a^b;
    if(op==T_SHL) return a<<(b&31);
    if(op==T_SHR) return uns?a>>(b&31):(u32)((int)a>>(b&31));
    if(op==T_EQ) return a==b;
    if(op==T_NE) return a!=b;
    if(op=='<') return uns?a<b:(int)a<(int)b;
    if(op=='>') return uns?a>b:(int)a>(int)b;
    if(op==T_LE) return uns?a<=b:(int)a<=(int)b;
    if(op==T_GE) return uns?a>=b:(int)a>=(int)b;
    if(op==T_AND) return a && b;
    if(op==T_OR) return a || b;
    fatal("unsupported constant operator");
    return 0;
}
static int constant_address_mode(int node,int *symbol,int *kind,int *addend,int lvalue_mode)
{
    Node *n=nodes+node;
    if(n->kind==N_CAST) return constant_address_mode(n->a,symbol,kind,addend,lvalue_mode);
    if(n->kind==N_ADDR) return constant_address_mode(n->a,symbol,kind,addend,1);
    if(n->kind==N_DEREF && lvalue_mode) return constant_address_mode(n->a,symbol,kind,addend,0);
    if(n->kind==N_STRING) { *kind=F_DATA;
        *symbol=0;
        *addend+=n->value;
        return 1; }
    if(n->kind==N_SYMBOL && (symbols[n->value].kind==S_FUNCTION || (symbols[n->value].kind==S_GLOBAL && (lvalue_mode || types[n->type].kind==TY_ARRAY)))) {
        *kind=F_ABS;
        *symbol=n->value;
        return 1;
    }
    if(n->kind==N_BINARY && (n->value=='+' || n->value=='-')) {
        if(constant_address_mode(n->a,symbol,kind,addend,0)) {
            int type=promoted(nodes[n->a].type),stride=types[type].kind==TY_PTR?types[types[type].base].size:1;
            *addend+=(int)constant_value(n->b)*stride*(n->value=='-'?-1:1);
            return 1;
        }
        if(n->value=='+' && constant_address_mode(n->b,symbol,kind,addend,0)) {
            int type=promoted(nodes[n->b].type),stride=types[type].kind==TY_PTR?types[types[type].base].size:1;
            *addend+=(int)constant_value(n->a)*stride;
            return 1;
        }
    }
    return 0;
}
static void global_init(int type,int offset,int expression)
{
    int symbol=0,kind=0,addend=0;
    if(types[type].kind==TY_STRUCT || types[type].kind==TY_UNION) fatal("global aggregate copy is not a constant initializer");
    if(types[type].size==4 && constant_address_mode(expression,&symbol,&kind,&addend,0)) {
        fixed(kind,symbol,addend,-offset-1);
        return;
    }
    u32 value=constant_value(expression);
    for(int i=0;i<types[type].size;i++) { data[offset+i]=(u8)value;
        value>>=8; }
}
static void define_global(int symbol,int initializer)
{
    Symbol *s=symbols+symbol;
    int size=types[s->type].size;
    if(!size || types[s->type].kind==TY_FUNC) fatal("global object needs a complete type");
    if(s->defined) { if(initializer) fatal("global is already defined");
        return; }
    if(initializer) {
        data_used=(data_used+3)&~3;
        if(size>DATA_MAX-data_used) fatal("initialized global data capacity exceeded");
        s->aux=1;
        s->value=data_used;
        data_used+=size;
        for(int i=0;i<size;i++) data[s->value+i]=0;
        for(int n=initializer;n;n=nodes[n].next) if(!nodes[n].value) global_init(nodes[n].type,s->value+nodes[n].b,nodes[n].c);
    } else {
        bss_used=(bss_used+3)&~3;
        if(size>0x3D0000-bss_used) fatal("BSS capacity exceeded");
        s->aux=0;
        s->value=bss_used;
        bss_used+=size;
    }
    s->defined=1;
}
/* 内联汇编只接受明确的固定寄存器约束；未知约束必须诊断，不偷偷
 * 当成普通函数。模板采用常用 AT&T 写法，%% 转义寄存器，也支持 %0
 * 引用约束编号。内存形式是 disp(base)，足够直接表达 syscall 桥。
 */
typedef struct { int kind,reg,base,value; } AsmOperand;
static int asm_register(const char *name)
{
    const char *names[]={"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
    for(int i=0;i<8;i++) if(equal(name,names[i])) return i;
    fatal("asm needs a 32-bit register");
    return 0;
}
static int constraint_register(int text)
{
    char *p=spelling(text);
    if(*p=='=' || *p=='+') p++;
    int r=*p=='a'?0:*p=='c'?1:*p=='d'?2:*p=='b'?3:*p=='S'?6:*p=='D'?7:-1;
    if(r<0 || p[1]) fatal("asm requires fixed a/b/c/d/S/D constraints");
    return r;
}
static void asm_space(char **p) { while(**p==' ' || **p=='\t' || **p=='\r') (*p)++; }
static int asm_integer(char **p)
{
    int sign=1;
    if(**p=='-') { sign=-1;
        (*p)++; } else if(**p=='+') (*p)++;
    int base=10;
    if((*p)[0]=='0' && ((*p)[1]=='x' || (*p)[1]=='X')) { base=16;
        *p+=2; }
    u32 value=0;
    int n=0;
    for(;;) {
        int c=**p,d=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:99;
        if(d>=base) break;
        value=value*(u32)base+(u32)d;
        (*p)++;
        n++;
    }
    if(!n) fatal("asm immediate needs an integer");
    return sign<0?(int)(0u-value):(int)value;
}
static int asm_reg_token(char **p,int *registers,int count)
{
    if(**p!='%') fatal("asm register needs percent prefix");
    (*p)++;
    if(**p=='%') (*p)++;
    if(**p>='0' && **p<='9') {
        int i=*(*p)++-'0';
        if(i>=count) fatal("asm operand index exceeds constraints");
        return registers[i];
    }
    char name[8];
    int n=0;
    while(**p>='a' && **p<='z') { if(n>=7) fatal("asm register too long");
        name[n++]=*(*p)++; }
    name[n]=0;
    return asm_register(name);
}
static void asm_operand(char **p,AsmOperand *o,int *registers,int count)
{
    asm_space(p);
    o->kind=0;
    o->reg=0;
    o->base=-1;
    o->value=0;
    if(**p=='$') { (*p)++;
        o->kind=1;
        o->value=asm_integer(p);
        return; }
    if(**p=='%') { o->kind=2;
        o->reg=asm_reg_token(p,registers,count);
        return; }
    o->kind=3;
    if(**p!='(') o->value=asm_integer(p);
    if(**p=='(') {
        (*p)++;
        o->base=asm_reg_token(p,registers,count);
        asm_space(p);
        if(**p!=')') fatal("asm supports disp(base) memory operands");
        (*p)++;
    }
}
static void asm_rm(int reg,AsmOperand *operand)
{
    if(operand->kind==2) byte(0xC0+reg*8+operand->reg);
    else if(operand->kind==3) memory(reg,operand->base,operand->value);
    else fatal("asm instruction needs register or memory");
}
static void asm_template(int string,int *registers,int count)
{
    char *p=(char *)(data+nodes[string].value);
    while(*p) {
        asm_space(&p);
        if(*p==';' || *p=='\n') { p++;
            continue; }
        char name[12];
        int n=0;
        /* UD2 的数字属于指令名，不能只读字母后误诊成未知的 ud。
         * 指令名仍在本子集白名单内，允许数字并不等于接受任意汇编。 */
        while((*p>='a' && *p<='z') || (*p>='0' && *p<='9')) { if(n>=11) fatal("asm mnemonic too long");
            name[n++]=*p++; } name[n]=0;
        if(!n) fatal("asm needs an instruction");
        if(equal(name,"movl")) copy(name,"mov",12);
        if(equal(name,"pushl")) copy(name,"push",12);
        if(equal(name,"popl")) copy(name,"pop",12);
        if(equal(name,"xorl")) copy(name,"xor",12);
        if(equal(name,"divl")) copy(name,"div",12);
        if(equal(name,"idivl")) copy(name,"idiv",12);
        if(equal(name,"ud2")) { byte(0x0F);
            byte(0x0B); }
        else if(equal(name,"nop")) byte(0x90);
        else if(equal(name,"int3")) byte(0xCC);
        else if(equal(name,"cld")) byte(0xFC);
        else if(equal(name,"int")) { AsmOperand a;
            asm_operand(&p,&a,registers,count);
            if(a.kind!=1 || a.value<0 || a.value>255) fatal("int needs an immediate byte");
            byte(0xCD);
            byte(a.value); }
        else if(equal(name,"push") || equal(name,"pop") || equal(name,"div") || equal(name,"idiv")) {
            AsmOperand a;
            asm_operand(&p,&a,registers,count);
            if(equal(name,"push") && a.kind==1) { byte(0x68);
                dword((u32)a.value); }
            else if(a.kind==2 && (equal(name,"push") || equal(name,"pop"))) byte((equal(name,"push")?0x50:0x58)+a.reg);
            else { byte(0xF7);
                asm_rm(equal(name,"div")?6:7,&a); }
        } else if(equal(name,"mov") || equal(name,"lea") || equal(name,"xor") || equal(name,"add") || equal(name,"sub") || equal(name,"and") || equal(name,"or") || equal(name,"cmp") || equal(name,"test")) {
            AsmOperand a,b;
            asm_operand(&p,&a,registers,count);
            asm_space(&p);
            if(*p++!=',') fatal("asm binary instruction needs comma");
            asm_operand(&p,&b,registers,count);
            if(equal(name,"mov")) {
                if(a.kind==1 && b.kind==2) immediate(b.reg,(u32)a.value);
                else if(a.kind==1) { byte(0xC7);
                    asm_rm(0,&b);
                    dword((u32)a.value); }
                else if(a.kind==2) { byte(0x89);
                    asm_rm(a.reg,&b); }
                else if(b.kind==2) { byte(0x8B);
                    asm_rm(b.reg,&a); }
                else fatal("mov cannot use two memory operands");
            } else if(equal(name,"lea")) {
                if(a.kind!=3 || b.kind!=2) fatal("lea needs memory then register");
                byte(0x8D);
                asm_rm(b.reg,&a);
            } else {
                int group=equal(name,"add")?0:equal(name,"or")?1:equal(name,"and")?4:equal(name,"sub")?5:equal(name,"xor")?6:equal(name,"cmp")?7:-1;
                if(a.kind==1 && group>=0) { byte(0x81);
                    asm_rm(group,&b);
                    dword((u32)a.value); }
                else if(a.kind==2) { byte(equal(name,"test")?0x85:group*8+1);
                    asm_rm(a.reg,&b); }
                else fatal("asm arithmetic source needs register or immediate");
            }
        } else fatal("unsupported inline asm instruction");
        asm_space(&p);
        if(*p && *p!=';' && *p!='\n') fatal("extra text after asm instruction");
    }
}
static void emit_asm(int node)
{
    Node *n=nodes+node;
    int regs[16],values[16],out_nodes[8],out_regs[8],inputs=0,outputs=0,count=0;
    for(int a=n->b;a;a=nodes[a].next) {
        if(outputs>=8) fatal("too many asm outputs");
        int r=constraint_register(nodes[a].value);
        regs[count++]=r;
        out_nodes[outputs]=nodes[a].a;
        out_regs[outputs++]=r;
        address(nodes[a].a);
        byte(0x50);
        if(spelling(nodes[a].value)[0]=='+') { values[inputs]=nodes[a].a;
            regs[8+inputs++]=r; }
    }
    for(int a=n->c;a;a=nodes[a].next) {
        if(inputs>=8 || count>=8) fatal("too many asm inputs");
        int r=constraint_register(nodes[a].value);
        regs[count++]=r;
        values[inputs]=nodes[a].a;
        regs[8+inputs++]=r;
    }
    for(int i=0;i<inputs;i++) { emit_expression(values[i]);
        byte(0x50); }
    for(int i=inputs-1;i>=0;i--) byte(0x58+regs[8+i]);
    asm_template(n->a,regs,count);
    for(int i=0;i<outputs;i++) byte(0x50+out_regs[i]);
    for(int i=0;i<outputs;i++) {
        byte(0x8B);
        memory(0,4,4*(outputs-1-i));
        byte(0x8B);
        memory(2,4,4*(outputs*2-1-i));
        store_value(nodes[out_nodes[i]].type);
    }
    stack_add(outputs*8);
}
static void control_begin(int loop)
{
    if(control_depth>=64) fatal("control nesting exceeds 64");
    break_jumps[control_depth]=continue_jumps[control_depth]=-1;
    control_loop[control_depth++]=loop;
}
static void control_end(int end,int continuation)
{
    control_depth--;
    patch_chain(break_jumps[control_depth],end);
    patch_chain(continue_jumps[control_depth],continuation);
}
static void emit_statement(int node)
{
    if(!node) return;
    Node *n=nodes+node;
    error_position=n->where;
    int kind=n->kind;
    if(kind==N_BLOCK) for(int a=n->a;a;a=nodes[a].next) emit_statement(a);
    else if(kind==N_EXPR) emit_expression(n->a);
    else if(kind==N_INIT) {
        symbol_address(n->a,n->b);
        byte(0x50);
        if(n->value) {
            /* 花括号对象清零用一次短循环，不为几十 KB 数组生成几万条
             * MOV。初始化过程不调用其他函数，ECX/EDX 可安全作为计数。 */
            byte(0x5A);
            immediate(1,(u32)types[n->type].size);
            immediate(0,0);
            int start=code_used;
            byte(0x88);
            byte(0x02);
            byte(0x42);
            byte(0x49);
            int again=jump(5);
            patch(again,start);
        } else { emit_expression(n->c);
        byte(0x5A);
        store_value(n->type); }
    } else if(kind==N_IF) {
        emit_expression(n->a);
        test_eax();
        int otherwise=jump(4);
        emit_statement(n->b);
        if(n->c) { int end=jump(-1);
            patch(otherwise,code_used);
            emit_statement(n->c);
            patch(end,code_used); }
        else patch(otherwise,code_used);
    } else if(kind==N_WHILE || kind==N_DO || kind==N_FOR) {
        control_begin(1);
        int start=code_used,exit=-1;
        if(kind!=N_DO && n->a) { emit_expression(n->a);
            test_eax();
            exit=jump(4); }
        emit_statement(n->b);
        int continuation=code_used;
        if(kind==N_FOR && n->c) emit_expression(n->c);
        if(kind==N_DO) { emit_expression(n->a);
            test_eax();
            int again=jump(5);
            patch(again,start); }
        else { int again=jump(-1);
            patch(again,start); }
        if(exit>=0) patch(exit,code_used);
        control_end(code_used,kind==N_WHILE?start:continuation);
    } else if(kind==N_SWITCH) {
        emit_expression(n->a);
        int dispatch=jump(-1);
        control_begin(0);
        emit_statement(n->b);
        int skip=jump(-1);
        patch(dispatch,code_used);
        int fallback=-1;
        for(int c=n->c;c;c=nodes[c].b) {
            if(nodes[c].type) fallback=nodes[c].c;
            else { byte(0x3D);
                dword((u32)nodes[c].value);
                int match=jump(4);
                patch(match,nodes[c].c); }
        }
        int tail=jump(-1);
        patch(tail,fallback<0?code_used:fallback);
        patch(skip,code_used);
        control_end(code_used,code_used);
    } else if(kind==N_CASE) { n->c=code_used;
    emit_statement(n->a); }
    else if(kind==N_BREAK) {
        if(!control_depth) fatal("break outside loop/switch");
        jump_link(break_jumps+control_depth-1);
    } else if(kind==N_CONTINUE) {
        int depth=control_depth-1;
        while(depth>=0 && !control_loop[depth]) depth--;
        if(depth<0) fatal("continue outside loop");
        jump_link(continue_jumps+depth);
    } else if(kind==N_RETURN) {
        emit_expression(n->a);
        cast_value(n->type);
        jump_link(&return_jumps);
    } else if(kind==N_LABEL) {
        int label=label_find(n->value);
        if(label_targets[label]>=0) fatal("duplicate function label");
        label_targets[label]=code_used;
        emit_statement(n->a);
    } else if(kind==N_GOTO) jump_link(label_jumps+label_find(n->value));
    else if(kind==N_ASM) emit_asm(node);
    else fatal("unsupported statement node");
}
static void hex_text(char *out,u32 value)
{
    const char *digits="0123456789ABCDEF";
    for(int i=7;i>=0;i--) { out[i]=digits[value&15];
        value>>=4; } out[8]=0;
}
static void map_function(int symbol,int address_value,int where)
{
    char row[160],hex[9],line[12];
    hex_text(hex,(u32)address_value);
    decimal(line,where&65535);
    copy(row,hex,sizeof(row));
    append(row," ",sizeof(row));
    append(row,spelling(symbols[symbol].name),sizeof(row));
    append(row," ",sizeof(row));
    int file=((u32)where>>16)&32767;
    if(file<source_count) append(row,sources[file].path,sizeof(row));
    append(row,":",sizeof(row));
    append(row,line,sizeof(row));
    append(row,"\n",sizeof(row));
    int size=length(row);
    if(map_used+size>=(int)sizeof(map_text)) fatal("symbol map capacity exceeded");
    for(int i=0;i<size;i++) map_text[map_used++]=row[i];
    map_text[map_used]=0;
}
static void emit_function(int symbol,int body)
{
    symbols[symbol].value=code_used;
    map_function(symbol,BASE+code_used,nodes[body].where);
    return_jumps=-1;
    control_depth=0;
    label_count=0;
    byte(0x55);
    move_reg(5,4);
    byte(0x81);
    byte(0xEC);
    dword((u32)local_bytes);
    byte(0x53);
    byte(0x56);
    byte(0x57);
    emit_statement(body);
    immediate(0,0);
    patch_chain(return_jumps,code_used);
    for(int i=0;i<label_count;i++) {
        if(label_targets[i]<0) fatal("goto target label is undefined");
        patch_chain(label_jumps[i],label_targets[i]);
    }
    byte(0x5F);
    byte(0x5E);
    byte(0x5B);
    byte(0xC9);
    byte(0xC3);
}
static void finish_image(const char *output)
{
    int main_symbol=find_symbol(intern("main",4),0);
    if(!main_symbol || symbols[main_symbol].kind!=S_FUNCTION || !symbols[main_symbol].defined) fatal("main function is missing");
    if(types[symbols[main_symbol].type].count!=0) fatal("SandCore main must have no parameters; use sc_args");
    int text_end=(code_used+3)&~3;
    if(data_used>CODE_MAX-text_end) fatal("SCX initialized image capacity exceeded");
    int load=text_end+data_used;
    if(load+bss_used>0x3D0000) fatal("SCX code/data/BSS exceed private address region");
    for(int i=code_used;i<text_end;i++) code[36+i]=0;
    for(int i=0;i<data_used;i++) code[36+text_end+i]=data[i];
    for(int i=0;i<fix_count;i++) {
        Fix *f=fixes+i;
        u32 value=0;
        if(f->kind==F_DATA) value=BASE+text_end+f->addend;
        else {
            Symbol *s=symbols+f->symbol;
            if(!s->defined) { error_position=0;
                fatal("referenced external symbol has no definition"); }
            if(s->kind==S_FUNCTION) value=BASE+s->value;
            else value=BASE+(s->aux?text_end:load)+s->value;
            value+=(u32)f->addend;
            if(f->kind==F_REL) value-=BASE+f->offset+4;
        }
        int offset=f->offset<0?text_end-f->offset-1:f->offset;
        put32(code+36+offset,value);
    }
    /* 对象地址也入符号表，图形调试器和验收可以读取实际目标数据，
     * 不把宿主 ELF 地址套到 SCCC 生成的另一个布局上。OBJECT 明示
     * 没有保存对象声明行号，不伪造源码位置。 */
    for(int i=1;i<symbol_count;i++) if(symbols[i].kind==S_GLOBAL && symbols[i].defined) {
        char row[160],hex[9];
        u32 address=BASE+(symbols[i].aux?text_end:load)+symbols[i].value;
        hex_text(hex,address);
        copy(row,hex,sizeof(row));
        append(row," ",sizeof(row));
        append(row,spelling(symbols[i].name),sizeof(row));
        append(row," OBJECT\n",sizeof(row));
        int n=length(row);
        if(map_used+n>=(int)sizeof(map_text)) fatal("symbol map capacity exceeded");
        for(int j=0;j<n;j++) map_text[map_used++]=row[j];
        map_text[map_used]=0;
    }
    /* 固定入口直接 CALL main，再把返回值交给 EXIT。它与宿主 start.asm
     * 的约定一致，不需要附带宿主对象文件或库才能运行生成的程序。 */
    for(int i=0;i<24;i++) code[36+i]=0x90;
    code[36]=0xE8;
    put32(code+37,(u32)(symbols[main_symbol].value-5));
    code[41]=0x89;
    code[42]=0xC3;
    code[43]=0xB8;
    put32(code+44,0);
    code[48]=0xCD;
    code[49]=0x7C;
    code[50]=0xEB;
    code[51]=0xFE;
    const char *magic="SCX1MIO";
    for(int i=0;i<8;i++) code[i]=(u8)magic[i];
    put32(code+8,0);
    put32(code+12,(u32)load);
    put32(code+16,(u32)bss_used);
    put32(code+20,131072);
    put32(code+24,0);
    put32(code+28,BASE);
    put32(code+32,0x004F494D);
    if(sc_write(output,code,36+load)!=36+load) fatal("output write failed (possibly protected path)");
    char report[128],value[12];
    decimal(value,load);
    copy(report,"SCCC OK load=",sizeof(report));
    append(report,value,sizeof(report));
    append(report," bss=",sizeof(report));
    decimal(value,bss_used);
    append(report,value,sizeof(report));
    append(report," / mio\n",sizeof(report));
    sc_write(log_path,report,length(report));
    sc_puts(report);
    /* 文本符号表有 MIO 魔数；调试器可按函数名/来源展示，但 CPU 断点
     * 本身始终使用真实 EIP，不依赖符号表才能单步或读内存。 */
    sc_write(map_path,map_text,map_used);
}



int main(int argc,char **argv) {
    if(argc!=3) return 2;
    copy(log_path,"build/c-check/error.log",64);
    copy(map_path,"build/c-check/output.map",64);
    preprocess(argv[1]); compile_unit(); finish_image(argv[2]);
    fprintf(stderr,"stats tokens=%d types=%d symbols=%d fixes=%d source=%d pool=%d\n",
        token_count,type_count,symbol_count,fix_count,source_used,pool_used);
    return 0;
}
