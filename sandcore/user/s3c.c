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
 * 【工作区为什么地址固定】没有 libc/malloc；大Source/Token/Pool/Code
 * 区在首次PP前一次从本人sc_alloc取得，之后不realloc，递归Lexer与
 * 解析器借用指针稳定。成功的诊断/map/正文完成后释放；fatal由kernel
 * 退出清所有本人页。语法树逐函数复用，符号/类型/重定位保留到TU结束。
 * 递归局部缓冲保持小，宏参数与 include 行缓冲放 BSS，避免 128KB
 * 用户栈被深层展开耗尽。文档：C.md、FS.md、SYSCALL.md。
 * ===================================================================== */
#include "SCAPI.H"
#ifdef SCCC_CLI
#include "COMPILECLI.inc"
#else
#include "COMPILEUI.inc"
#endif

#define TOKEN_MAX 262144
#define POOL_MAX 2097152
/* 真实SCUI已超过324KiB且controls/dialog继续增长。1MiB是完整TU
 * 源及每文件NUL预算，不是单文件/最终SCX容量，也不边读边搬arena。 */
#define SOURCE_MAX 1048576
#define IDENT_MAX 4096
#define FILE_MAX 64
#define LINE_MAX 1024
#define DEPTH_MAX 16
/* 当前Broker+Font/Popup聚合已有382项含预定义宏，SCUI近32个include。
 * 此两表只按当时实际规模留裕量；后续symbol诊断的扩容另见下方。 */
#define MACRO_MAX 512
#define MACRO_BODY_MAX 8192
#define MACRO_ARGS 8
#define MACRO_ARG_TOKENS 128
#define TYPE_MAX 4096
/* host03已解析生成真实aggregate夹具，完整SCUI在ui_edit:1531累计
 * 符号触6144。字段/局部符号须存活至各引用完成，给完整TU留16384；
 * Symbol和两个按ID旁表一起扩，BSS增加409600B，其它容量不动。 */
#define SYMBOL_MAX 16384
#define PARAM_MAX 4096
#define NODE_MAX 8192
#define FIX_MAX 12288
#define CODE_MAX 262000
#define SCL_CODE_MAX 1048576
#define DATA_MAX 131072
static int module_output;
static int shared_output;
static int dynamic_output;
static int object_output;
static void sco_line(int where);
static void finish_sco(const char *output);
#define BASE ((module_output||shared_output)?0u:0x400000u)

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
enum { TY_VOID,TY_CHAR,TY_SHORT,TY_INT,TY_PTR,TY_ARRAY,TY_FUNC,TY_STRUCT,TY_UNION,TY_BOOL };
typedef struct { int kind,size,base,count,fields,flags; } Type;
enum { S_GLOBAL,S_LOCAL,S_PARAM,S_FUNCTION,S_TYPEDEF,S_ENUM,S_FIELD,S_TAG };
typedef struct { int name,type,kind,value,scope,defined,next,aux; } Symbol;
typedef struct { int name,type,next; } Parameter;
enum {
    N_CONST,N_SYMBOL,N_STRING,N_DEREF,N_ADDR,N_CAST,N_BINARY,N_UNARY,N_ASSIGN,
    N_INC,N_CALL,N_COND,N_COMMA,N_BLOCK,N_EXPR,N_IF,N_WHILE,N_DO,N_FOR,
    N_RETURN,N_BREAK,N_CONTINUE,N_SWITCH,N_CASE,N_ASM,N_ARG,N_INIT,N_LABEL,N_GOTO,
    N_VA_START,N_VA_ARG,N_VA_COPY,N_VA_END
};
typedef struct { int kind,type,a,b,c,value,where,next; } Node;
typedef struct { int offset,kind,symbol,addend; } Fix;

static char *pool,*source_arena;
static int pool_used,source_used,identifiers[IDENT_MAX],identifier_count;
static Source sources[FILE_MAX];
static int source_count;
static Tok *tokens;
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
/* const/volatile副本仍属于同一聚合身份；前向声明完成后同步副本。 */
static int aggregate_identity[TYPE_MAX];
/* 每个原类型最多三个CV副本；小链复用避免头/成员访问反复消耗类型槽。 */
static int qualifier_origin[TYPE_MAX],qualifier_next[TYPE_MAX];
static Symbol symbols[SYMBOL_MAX];
static Parameter parameters[PARAM_MAX];
static Node nodes[NODE_MAX];
static Fix fixes[FIX_MAX];
static int type_count,symbol_count,param_count,node_count,fix_count;
static int function_symbol,scope_depth,local_bytes;
static int function_name_literal,function_name_bytes;
static int root_adapter_symbols[2],root_adapter_used[2];
static u8 *code,data[DATA_MAX];
static int code_capacity;
static int code_used,data_used,bss_used;
static int scl_symbol_storage[SYMBOL_MAX],scl_symbol_sizes[SYMBOL_MAX];
/* SCUI现约495函数：函数行约26KiB，导出/导入行另加，32KiB已无
 * 正式输入裕量。保留固定128KiB及sizeof共同检查，行格式/顺序不变。
 * 此小区仍在BSS；四个大区移堆后自身映像仍须独立检查3d窗口。 */
static char log_path[64],map_path[64],diagnostic[512],map_text[131072];
static char icon_path[64];
static int map_used;
/* 后端会在函数语法树完成后立即消费它；前置原型保证宿主与 SCCC
 * 自身采用同一套声明，不依赖宿主编译器的隐式函数扩展。 */
static void emit_function(int symbol,int body);
static void global_init(int type,int offset,int expression);
static void define_global(int symbol,int initializer);
static u32 constant_value(int expression);
static int constant_address_mode(int node,int *symbol,int *kind,int *addend,int lvalue_mode);
static int sclc_type_same(int a,int b,int depth,int parameter);
static int standard_builtin(void);
static int standard_value_conversion(int target,int value);
static void standard_bool_source(int value);
static int standard_conditional_type(int yes,int no);
static int standard_default_conversion(int value);
static int standard_undeclared(int name);
static int standard_root_name(int name);
static void standard_root_declaration(int symbol,int type,int kind);
static void emit_standard_adapters(void);
static void map_function(int symbol,int address_value,int where);
static void compile_unit(void);
static void finish_image(const char *output);
static void scl_literal_new(int offset,int size);
static void scl_global_new(int symbol,int size);
static void scl_declare(int symbol,int storage);
static int scl_object_const(int type);
static int scl_declaration_type(int previous,int type);
static void scl_manifest_load(const char *path,const char *output);
static void dynamic_manifest_load(const char *path,const char *output);
static int scl_path_same(const char *a,const char *b);
static void finish_scl(const char *output);
static void finish_dynamic(const char *output);

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
#ifndef SCCC_CLI
    sc_puts(diagnostic);
#endif
    build_view_result(diagnostic,1);
    sc_exit(1);
    for(;;) sc_yield();
}
static void capacity_number(char *out,u32 value)
{
    /* 文件尺寸来自u32元数据；不能先转int把大非法输入的need写成负数。 */
    char reversed[10];int used=0,at=0;
    do {reversed[used++]=(char)('0'+value%10u);value/=10u;} while(value);
    while(used)out[at++]=reversed[--used];
    out[at]=0;
}
static void capacity_fatal(const char *area,u32 used,u32 need,u32 limit)
{
    char message[192],number[12];
    copy(message,area,sizeof(message));
    append(message," capacity exceeded: used=",sizeof(message));
    capacity_number(number,used);append(message,number,sizeof(message));
    append(message," need=",sizeof(message));capacity_number(number,need);append(message,number,sizeof(message));
    append(message," limit=",sizeof(message));capacity_number(number,limit);append(message,number,sizeof(message));
    fatal(message);
}
static void workspace_release(void)
{
    /* 只有完整finish/map/log之后才走成功释放。source/file的递归Lexer、
     * token池与字符串offset此前都有借用；不在PP/parse内部搬或释放。 */
    if(code){sc_free(code);code=0;}
    if(tokens){sc_free(tokens);tokens=0;}
    if(pool){sc_free(pool);pool=0;}
    if(source_arena){sc_free(source_arena);source_arena=0;}
}
static void workspace_acquire(void)
{
    code_capacity=shared_output?SCL_CODE_MAX:CODE_MAX;
    source_arena=sc_alloc(SOURCE_MAX);
    if(!source_arena)goto failed;
    pool=sc_alloc(POOL_MAX);
    if(!pool)goto failed;
    tokens=sc_alloc((u32)TOKEN_MAX*(u32)sizeof(Tok));
    if(!tokens)goto failed;
    code=sc_alloc((u32)code_capacity+36u);
    if(code)return;
failed:
    /* OOM在第一个失败处停止；既有候选先归还，不再申请后面的更大区。 */
    workspace_release();fatal("compiler workspace allocation failed (source/token/pool/code)");
}
static int text_size(int at) { return *(int *)(pool+at-4); }
static char *spelling(int at) { return pool+at; }
static int store_text(const char *s,int n)
{
    if(n<0)fatal("negative string size");
    if(pool_used>POOL_MAX-13 || n>POOL_MAX-pool_used-13)capacity_fatal("string pool",(u32)pool_used,(u32)n+13u,POOL_MAX);
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
    if(identifier_count>=IDENT_MAX)capacity_fatal("identifiers",(u32)identifier_count,1u,IDENT_MAX);
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
#include "s3c_std_parse.inc"
#include "s3c_emit.inc"
#include "s3c_scl.inc"
#include "s3c_sco.inc"

int main(void)
{
#ifdef SCCC_CLI
    if(cli_parse()<0 || cli_argc<2 || cli_argc>4){
        cli_text(2,"s3c source.c output.scx [icon.scb | --skm]\ns3c source.c output.SCL --scl manifest.json\ns3c source.c output.SCX --dynamic app.json\ns3c source.c output.SCO --object\n");return 2;
    }
    char *input=cli_argv[0],*output=cli_argv[1],*icon=cli_argc>=3?cli_argv[2]:"";
    char *manifest=cli_argc==4?cli_argv[3]:"";
#else
    char args[1024];
    if(sc_arguments2(args,sizeof(args))<0)sc_args(args,sizeof(args));
    char *p=args;
    char *input=token(&p),*output=token(&p),*icon=token(&p);
    char *manifest="";
#endif
    if(equal(icon,"--object")){object_output=1;shared_output=1;icon="";}
    if(equal(icon,"--skm")){module_output=1;icon="";}
    if(equal(icon,"--scl")){
        shared_output=1;icon="";
#ifndef SCCC_CLI
        manifest=token(&p);
#endif
    }
    if(equal(icon,"--dynamic")){
        dynamic_output=1;icon="";
#ifndef SCCC_CLI
        manifest=token(&p);
#endif
    }
#ifdef SCCC_CLI
    if(((shared_output || dynamic_output) && !object_output && cli_argc!=4) || (object_output && cli_argc!=3) || (!(shared_output || dynamic_output) && cli_argc>3)){
        cli_text(2,"--scl and --dynamic require exactly one manifest.json argument\n");return 2;
    }
    char input_path[64],output_path[64],resolved_icon[64];
    if(!(shared_output || dynamic_output)){
        if(sc_resolve(input,input_path,64)<0 || sc_resolve(output,output_path,64)<0)return 2;
        input=input_path;output=output_path;
        if(*icon){if(sc_resolve(icon,resolved_icon,64)<0)return 2;icon=resolved_icon;}
    }
#endif
    char shared_input_path[64],shared_output_path[64],shared_manifest_path[64];
    if(shared_output || dynamic_output){
        /* RESOLVE返回根内无前导斜杠的规范名，不能再次按cwd解析。
         * 两个清单模式的CLI/GUI共用唯一解析点，保留原63B名字容量。 */
        if(sc_resolve(input,shared_input_path,64)<0 || sc_resolve(output,shared_output_path,64)<0
           || (!object_output && sc_resolve(manifest,shared_manifest_path,64)<0))return 2;
        input=shared_input_path;output=shared_output_path;if(!object_output)manifest=shared_manifest_path;
    }
    build_view_begin("SCCC / mio",input,output);
    if(!*input || !*output) {
        sc_puts("s3c source.c output.scx [icon.scb]\n");
        build_view_result("s3c source.c output.scx [icon.scb]",1);
        build_view_wait();
        return 1;
    }
    copy(log_path,output,sizeof(log_path));
    if(length(log_path)>59) copy(log_path,(shared_output || dynamic_output)?"/HOME/S3C":"HOME/S3C",sizeof(log_path));
    copy(map_path,log_path,sizeof(map_path));
    append(log_path,".log",sizeof(log_path));
    append(map_path,".map",sizeof(map_path));
    if((shared_output || dynamic_output) && (scl_path_same(output,input) || scl_path_same(output,manifest)
       || scl_path_same(log_path,input) || scl_path_same(log_path,manifest)
       || scl_path_same(map_path,input) || scl_path_same(map_path,manifest))){
        /* 日志本身也可能是输入的别名，此处不能调用会写日志的fatal。 */
        build_view_result("Dynamic output, log or map overlaps an input",1);return 2;
    }
#ifdef SCCC_CLI
    if(length(icon)>63)fatal("icon path capacity exceeded");
#else
    if(length(icon)>63 || *token(&p))fatal("expected source, output and optional SCB2 icon");
#endif
    copy(icon_path,icon,sizeof(icon_path));
    /* manifest同样intern名字到pool，因此工作区取得必须早于清单；
     * 路径/日志别名检查仍在前面，OOM不会借诊断覆盖输入正文。 */
    workspace_acquire();
    if(shared_output && !object_output){
        if(!*manifest)fatal("SCL manifest is missing");
        if(build_view_poll("Validating SCL manifest",0,0,1))fatal("compilation cancelled");
        scl_manifest_load(manifest,output);
    }
    if(dynamic_output){
        if(!*manifest)fatal("dynamic application manifest is missing");
        if(build_view_poll("Validating application manifest",0,0,1))fatal("compilation cancelled");
        dynamic_manifest_load(manifest,output);
    }
    sc_puts("SCCC: preprocess -> C -> x86\n");
    if(build_view_poll("Preprocessing source and includes",0,0,1))fatal("compilation cancelled");
    preprocess(input);
    if(build_view_poll("Parsing C and generating x86",0,token_count,1))fatal("compilation cancelled");
    compile_unit();
    if(build_view_poll(shared_output?"Linking and writing SCL":"Linking and writing SCX",1,1,1))fatal("compilation cancelled");
    finish_image(output);
    workspace_release();
    build_view_result("Compilation complete",0);
#ifdef SCCC_CLI
    return build_view_cancel?1:0;
#else
    return 0;
#endif
}
