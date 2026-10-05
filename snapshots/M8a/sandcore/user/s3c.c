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
#include "SCAPI.H"
#include "COMPILEUI.inc"

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
static char icon_path[64];
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
    build_view_result(diagnostic,1);
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

#include "s3c_lex.inc"
#include "s3c_pp.inc"
#include "s3c_parse.inc"
#include "s3c_emit.inc"

int main(void)
{
    char args[128];
    sc_args(args,sizeof(args));
    char *p=args;
    char *input=token(&p),*output=token(&p),*icon=token(&p);
    build_view_begin("SCCC / mio",input,output);
    if(!*input || !*output) {
        sc_puts("s3c source.c output.scx [icon.scb]\n");
        build_view_result("s3c source.c output.scx [icon.scb]",1);
        build_view_wait();
        return 1;
    }
    copy(log_path,output,sizeof(log_path));
    if(length(log_path)>59) copy(log_path,"HOME/S3C",sizeof(log_path));
    copy(map_path,log_path,sizeof(map_path));
    append(log_path,".log",sizeof(log_path));
    append(map_path,".map",sizeof(map_path));
    if(length(icon)>63 || *token(&p))fatal("expected source, output and optional SCB2 icon");
    copy(icon_path,icon,sizeof(icon_path));
    sc_puts("SCCC: preprocess -> C -> x86\n");
    if(build_view_poll("Preprocessing source and includes",0,0,1))fatal("compilation cancelled");
    preprocess(input);
    if(build_view_poll("Parsing C and generating x86",0,token_count,1))fatal("compilation cancelled");
    compile_unit();
    if(build_view_poll("Linking and writing SCX",1,1,1))fatal("compilation cancelled");
    finish_image(output);
    build_view_result("Compilation complete",0);
    return 0;
}
