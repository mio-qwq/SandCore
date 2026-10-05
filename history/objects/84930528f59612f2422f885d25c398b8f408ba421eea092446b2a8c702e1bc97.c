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


int main(int argc,char **argv) {
    if(argc!=2) return 2;
    copy(log_path,"build/pp-check/error.log",64);
    preprocess(argv[1]);
    for(int i=0;i<token_count;i++) {
        char text[1024]; token_text(tokens+i,text,sizeof(text));
        printf("%s\n",text);
    }
    return 0;
}
