#include <stddef.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "SC32API.H"

/* 普通SDK使用者：不包含库正文、SCAPI或私有ROOT机器接口。 */
extern int StdlibGuardSetStream(FILE *stream);
extern int StdlibGuardRegister(void);
extern int StdlibGuardNarrow(unsigned char a,signed char b,unsigned short c,
                            short d,bool flag,const char *text,void (*callback)(int));
extern bool StdlibGuardFalse(void);
extern bool StdlibGuardTrue(void);

#if 0xffffffffU < 1
#error Unsigned preprocessing comparisons must preserve their domain
#endif
#ifdef __SCCC__
/* 此项针对SCCC条件类型合并；GCC把负分支转unsigned的诊断不是
 * 客体语言风险，不能为引导探针全局降低-Werror。 */
#if ((1 ? -1 : 0u) >> 1) != (~0u >> 1)
#error SCCC conditional preprocessing types must merge unsigned
#endif
#endif
#if (0x80000000UL >> 31) != 1 || 4294967295u != 0xffffffffU || 010 != 8
#error Integer preprocessing literals or shifts are incorrect
#endif

typedef struct {
    char tag;
    bool flags[3];
    short count;
    void *pointer;
    int tail[3];
} ProbeLayout;
typedef char probe_layout_24[(sizeof(ProbeLayout)==24)?1:-1];
typedef char probe_pointer_8[(offsetof(ProbeLayout,pointer)==8)?1:-1];
typedef char probe_nested_20[(offsetof(ProbeLayout,tail[2])==20)?1:-1];
static bool global_bool=256;
static bool global_bools[4]={0,256,0x10000,-3};
static ProbeLayout global_layout={'G',{0,256,0x10000},-1,NULL,{3,5,7}};
static const ProbeLayout readonly_layout={'R',{0,256,0x10000},2,NULL,{11,13,17}};
static int failures,log_failed,narrow_value,va_cursor_calls;
static FILE *lifecycle_events;

#define PROBE_HOME "/HOME/M10STD"
#define PROBE_STDIO PROBE_HOME "/STDIO.DAT"
#define PROBE_RETURN PROBE_HOME "/RETURN.LOG"
#define PROBE_EXIT PROBE_HOME "/EXIT.LOG"
#define PROBE_SHARED PROBE_HOME "/SHARED.RES"
#define PROBE_RECURSIVE PROBE_HOME "/RECURSE.LOG"

static int check(const char *name,int okay)
{
    if(printf("STDLIB %s %s\n",okay?"PASS":"FAIL",name)<0)log_failed=1;
    if(!okay)failures++;
    return okay;
}

static int finish(const char *mode)
{
    int okay=!failures && !log_failed;
    if(printf("STDLIB_%s_%s failures=%d\n",mode,okay?"PASS":"FAIL",failures)<0)okay=0;
    if(fflush(stdout))okay=0;
    return okay?0:91;
}

static int directory_ready(void)
{
    ScStatus status=ScPathCreate(PROBE_HOME,SC32_PATH_DIRECTORY,63);
    return status==0 || status==SC32_E_EXISTS;
}

static int integer_compare(const void *left,const void *right)
{
    int a=*(const int *)left,b=*(const int *)right;
    return (a>b)-(a<b);
}

static int read_integer(const int *value)
{
    return value?*value:-1;
}

static int read_flags(const bool *values)
{
    return values[0]+values[1]+values[2];
}

static bool return_bool(int value)
{
    return value;
}

static void narrow_callback(int value)
{
    narrow_value=value;
}

static va_list *va_cursor(va_list *value)
{
    va_cursor_calls++;
    return value;
}

static int variadic_check(int last,...)
{
    va_list original,copy;
    va_start(original,last);
    va_copy(copy,original);
    va_cursor_calls=0;
    int first=va_arg(*va_cursor(&copy),int);
    const char *text=va_arg(copy,const char *);
    int promoted_byte=va_arg(copy,int);
    int promoted_bool=va_arg(copy,int);
    unsigned int maximum=va_arg(copy,unsigned int);
    int untouched=va_arg(original,int);
    const char *original_text=va_arg(original,const char *);
    va_end(copy);
    va_end(original);
    return last==77 && first==11 && untouched==11 && text==original_text &&
           strcmp(text,"va")==0 && promoted_byte==250 && promoted_bool==1 &&
           maximum==UINT_MAX && va_cursor_calls==1;
}

static void compiler_checks(void)
{
    bool local=256;
    int number=23;
    /* 运行值的低8位为零，能抓住pointer转bool先截字节的错误；
     * 不解引用该地址，也不依赖编译器对已知对象地址的折叠。 */
    volatile uintptr_t pointer_bits=0x100u;
    void *pointer_value=(void *)pointer_bits;
    local=0x10000;
    check("bool-full-scalar",sizeof(bool)==1 && local==1 && return_bool(256)==1 &&
          (bool)pointer_value==1 && (bool)NULL==0 && global_bool==1);
    check("bool-storage",sizeof(global_bools)==4 && global_bools[0]==0 &&
          global_bools[1]==1 && global_bools[2]==1 && global_bools[3]==1 &&
          global_layout.flags[1]==1 && global_layout.flags[2]==1 &&
          read_flags(readonly_layout.flags)==2);
    check("offsetof-ice",sizeof(probe_layout_24)==1 && sizeof(probe_pointer_8)==1 &&
          sizeof(probe_nested_20)==1 && offsetof(ProbeLayout,flags[2])==3);
    const int *constant=&readonly_layout.tail[1];
    check("pointer-cv-null",read_integer(false?NULL:&number)==23 &&
          read_integer(true?&number:constant)==23 && read_integer(false?&number:constant)==13);
    int a=0x123,b=0x1ff,c=0x12345,d=0xffff,flag=256;
    narrow_value=0;
    check("narrow-prototype",StdlibGuardNarrow(a,b,c,d,flag,"CV",narrow_callback)==1 && narrow_value==7);
    bool from_false=StdlibGuardFalse(),from_true=StdlibGuardTrue();
    check("external-bool-al",from_false==0 && from_true==1 &&
          (int)StdlibGuardFalse()==0 && (int)StdlibGuardTrue()==1);
    check("varargs-copy-promotions-once",variadic_check(77,11,"va",(unsigned char)250,(bool)256,UINT_MAX));
    /* volatile使宿主不能提前删掉assert分支，清单保留实际辅助导入。 */
    volatile int assertion=0;
    assert(++assertion==1);
    check("assert-once",assertion==1);
}

static void allocator_checks(void)
{
    const size_t sizes[8]={1,15,16,17,63,257,4097,40000};
    unsigned char *blocks[8];
    int okay=1;
    for(unsigned int i=0;i<8;i++){
        blocks[i]=malloc(sizes[i]);
        if(!blocks[i] || ((uintptr_t)blocks[i]&15u)){okay=0;continue;}
        memset(blocks[i],(int)(i+31),sizes[i]);
    }
    for(unsigned int i=0;i<8;i++){
        if(blocks[i])for(size_t j=0;j<sizes[i];j++)if(blocks[i][j]!=i+31u)okay=0;
    }
    check("allocator-align-isolation",okay);
    for(unsigned int i=8;i;i--)free(blocks[i-1]);
    free(NULL);

    unsigned char *a=malloc(1024),*b=malloc(1024),*guard=malloc(1024);
    if(a && b && guard){
        uintptr_t start=(uintptr_t)a;
        free(a);
        void *again=malloc(1024);
        check("allocator-reuse",(uintptr_t)again==start);
        free(again);
        free(b);
        void *merged=malloc(1800);
        /* 第三活块隔住尾空闲区；这项验证当前分配器的邻接合并，
         * 不把ISO C的地址复用选择宣称为额外公共ABI。 */
        check("allocator-adjacent-coalesce",(uintptr_t)merged==start);
        free(merged);
        free(guard);
    }else{
        check("allocator-adjacent-resources",0);
        free(a);free(b);free(guard);
    }

    errno=0;
    void *overflow=calloc(SIZE_MAX,2);
    check("calloc-overflow",overflow==NULL && errno==ENOMEM);
    free(overflow);
    unsigned char *zero=calloc(64,3);
    okay=zero!=NULL;
    if(zero)for(size_t i=0;i<192;i++)if(zero[i])okay=0;
    check("calloc-reused-zero",okay);
    free(zero);

    unsigned char *old=malloc(257);
    if(!check("realloc-source",old!=NULL))return;
    for(size_t i=0;i<257;i++)old[i]=(unsigned char)(i^0x5a);
    errno=0;
    void *failed=realloc(old,SIZE_MAX);
    okay=failed==NULL && errno==ENOMEM;
    if(!failed){
        for(size_t i=0;i<257;i++)if(old[i]!=(unsigned char)(i^0x5a))okay=0;
        old[256]=0x7d;
        okay=okay && old[256]==0x7d;
    }
    check("realloc-failure-keeps-old",okay);
    if(failed){free(failed);return;}
    unsigned char *small=realloc(old,64);
    if(!check("realloc-shrink",small!=NULL)){free(old);return;}
    okay=((uintptr_t)small&15u)==0;
    for(size_t i=0;i<64;i++)if(small[i]!=(unsigned char)(i^0x5a))okay=0;
    unsigned char *large=realloc(small,4097);
    if(large){
        okay=okay && (((uintptr_t)large&15u)==0);
        for(size_t i=0;i<64;i++)if(large[i]!=(unsigned char)(i^0x5a))okay=0;
    }else okay=0;
    check("realloc-grow-prefix",okay);
    free(large?large:small);
}

static void string_checks(void)
{
    char overlap[18]="abcdefghijklmnop";
    memmove(overlap+3,overlap,12);
    check("memmove-backward",memcmp(overlap+3,"abcdefghijkl",12)==0 &&
          overlap[0]=='a' && overlap[2]=='c' && overlap[15]=='p' && overlap[16]==0);
    strcpy(overlap,"abcdefghijklmnop");
    memmove(overlap,overlap+3,12);
    check("memmove-forward",memcmp(overlap,"defghijklmno",12)==0 && overlap[15]=='p');
    unsigned char bytes[4]={0,255,128,127};
    check("byte-memory",memchr(bytes,255,4)==bytes+1 && memcmp(bytes+2,bytes+3,1)>0);
    char padded[8];
    memset(padded,0x5a,sizeof(padded));
    strncpy(padded,"xy",5);
    check("strncpy-boundary",padded[0]=='x' && padded[1]=='y' && !padded[2] &&
          !padded[3] && !padded[4] && padded[5]==0x5a);
    const char *periodic="zzabababababababababaz";
    const char *nonperiodic="prefix abracadabra suffix";
    check("strstr-periodic",strstr(periodic,"ababababa")==periodic+2 &&
          strstr("abababababababab","abababac")==NULL);
    check("strstr-nonperiodic",strstr(nonperiodic,"abracadabra")==nonperiodic+7 &&
          strstr(nonperiodic,"")==nonperiodic);
    char long_text[1025];
    memset(long_text,'a',1023);
    long_text[1023]='b';long_text[1024]=0;
    check("strstr-long-tail",strstr(long_text,"aaaaab")==long_text+1018);
}

static void conversion_checks(void)
{
    char *end;
    errno=EIO;
    long signed_value=strtol(" \t-0x80000000tail",&end,0);
    check("strtol-min-end-errno",signed_value==LONG_MIN && strcmp(end,"tail")==0 && errno==EIO);
    unsigned long value=strtoul("0377x",&end,0);
    check("strto-bases",value==255 && *end=='x' && strtoul("101010z",&end,2)==42 &&
          *end=='z' && strtoul("Z!",&end,36)==35 && *end=='!');
    const char *none="  -?";
    errno=0;
    signed_value=strtol(none,&end,10);
    check("strto-no-digit-prefix",signed_value==0 && end==none &&
          strtoul("0xg",&end,0)==0 && *end=='x');
    errno=0;
    value=strtoul("42949672960rest",&end,10);
    check("strtoul-overflow-scan",value==ULONG_MAX && errno==ERANGE && strcmp(end,"rest")==0);
    errno=EIO;
    value=strtoul("-1",&end,10);
    check("strtoul-negative-modulo",value==ULONG_MAX && !*end && errno==EIO);
    errno=0;
    value=strtoul("123",&end,1);
    check("strto-invalid-base",value==0 && errno==EINVAL && *end=='1');
    int values[7]={9,1,5,1,INT_MAX,INT_MIN,0};
    qsort(values,7,sizeof(values[0]),integer_compare);
    int okay=1;
    for(unsigned int i=1;i<7;i++)if(values[i-1]>values[i])okay=0;
    int key=5,missing=8;
    int *found=bsearch(&key,values,7,sizeof(values[0]),integer_compare);
    check("qsort-bsearch",okay && found && *found==5 &&
          bsearch(&missing,values,7,sizeof(values[0]),integer_compare)==NULL);
    check("ctype-byte-eof",isdigit('9') && isalpha('Q') && isblank('\t') &&
          isspace('\n') && isxdigit('f') && !isalpha(255) && !isdigit(EOF) &&
          tolower('Q')=='q' && toupper('q')=='Q' && tolower(EOF)==EOF);
}

static void format_checks(void)
{
    char text[128];
    const char *expected="-2147483648|4294967295|0x2a|011|Z|abc|0x00001234";
    int length=snprintf(text,sizeof(text),"%d|%u|%#x|%#o|%c|%.3s|%p",
                        INT_MIN,UINT_MAX,42u,9u,'Z',"abcdef",(void *)0x1234);
    check("format-integer-string-pointer",length==(int)strlen(expected) && strcmp(text,expected)==0);
    struct {char data[5];char fence[3];} limited;
    memset(&limited,0x5a,sizeof(limited));
    int count=-1;
    length=snprintf(limited.data,sizeof(limited.data),"AB%06d%nZ",12,&count);
    check("snprintf-truncation-n",length==9 && count==8 && strcmp(limited.data,"AB00")==0 &&
          limited.fence[0]==0x5a && limited.fence[1]==0x5a && limited.fence[2]==0x5a);
    count=-1;
    length=snprintf(NULL,0,"abc%n",&count);
    check("snprintf-zero-capacity",length==3 && count==3);
    length=snprintf(text,sizeof(text),"%*.*d|%-5s|%+.0d",-6,4,12,"xy",0);
    check("format-width-precision",length==14 && strcmp(text,"0012  |xy   |+")==0);
    length=snprintf(text,sizeof(text),"%hhd|%hhu|%hd|%hu|%zu|%td",128,258,65535,65537,(size_t)4,(ptrdiff_t)-4);
    check("format-promoted-narrow",length==16 && strcmp(text,"-128|2|-1|1|4|-4")==0);
    if(printf("STDLIB CORE VALUE %d %s %p\n",INT_MIN,"text",(void *)0x1234)<0)log_failed=1;
}

static int core_mode(void)
{
    compiler_checks();
    allocator_checks();
    string_checks();
    conversion_checks();
    format_checks();
    return finish("CORE");
}

static int file_length_is(unsigned int bytes)
{
    ScPathStatInfo info={sizeof(ScPathStatInfo),1,0,0};
    return ScPathStat(PROBE_STDIO,&info)==0 && info.kind==SC32_PATH_FILE && info.bytes==bytes;
}

static int stdio_mode(void)
{
    if(!check("stdio-directory",directory_ready()))return finish("STDIO");
    FILE *file=fopen(PROBE_STDIO,"w");
    if(!check("mode-w-open",file!=NULL))return finish("STDIO");
    check("mode-w-buffered-position",fwrite("ABCD",2,2,file)==2 && ftell(file)==4);
    check("flush-keeps-file-open",fflush(file)==0 && file_length_is(4) &&
          fflush(file)==0 && ftell(file)==4);
    check("mode-w-close",fclose(file)==0);

    file=fopen(PROBE_STDIO,"rb");
    if(!check("mode-r-open",file!=NULL))return finish("STDIO");
    char read_buffer[16],bytes[16];
    check("read-borrowed-buffer",setvbuf(file,read_buffer,_IOFBF,sizeof(read_buffer))==0);
    check("read-prefetch-logical-position",fread(bytes,1,2,file)==2 && memcmp(bytes,"AB",2)==0 && ftell(file)==2);
    check("ungetc-logical-position",ungetc('Z',file)=='Z' && ftell(file)==1 && fgetc(file)=='Z' && ftell(file)==2);
    fpos_t position={0,0};
    check("fgetpos",fgetpos(file,&position)==0);
    check("seek-cur-logical",fseek(file,0,SEEK_CUR)==0 && ftell(file)==2 && fgetc(file)=='C');
    check("fsetpos",fsetpos(file,&position)==0 && fgetc(file)=='C' && fgetc(file)=='D');
    check("read-eof",fgetc(file)==EOF && feof(file) && !ferror(file));
    clearerr(file);
    check("clearerr",!feof(file) && !ferror(file));
    rewind(file);
    check("rewind",ftell(file)==0 && fgetc(file)=='A');
    check("mode-r-close",fclose(file)==0);

    file=fopen(PROBE_STDIO,"ab");
    if(!check("mode-a-open",file!=NULL))return finish("STDIO");
    check("mode-a-real-end",fwrite("EF",1,2,file)==2 && ftell(file)==6 && fflush(file)==0 && file_length_is(6));
    check("mode-a-close",fclose(file)==0);

    file=fopen(PROBE_STDIO,"r+b");
    if(!check("mode-rplus-open",file!=NULL))return finish("STDIO");
    check("mode-rplus-position-barrier",fgetc(file)=='A' && fseek(file,1,SEEK_CUR)==0 && ftell(file)==2 &&
          fputc('X',file)=='X' && fflush(file)==0 && fseek(file,0,SEEK_SET)==0);
    check("mode-rplus-no-truncate",fread(bytes,1,6,file)==6 && memcmp(bytes,"ABXDEF",6)==0);
    check("mode-rplus-close",fclose(file)==0);

    file=fopen(PROBE_STDIO,"wb+");
    if(!check("mode-wplus-open",file!=NULL))return finish("STDIO");
    check("mode-wplus-truncate",ftell(file)==0 && fwrite("12",1,2,file)==2 &&
          fflush(file)==0 && file_length_is(2) && fseek(file,0,SEEK_SET)==0 && fread(bytes,1,2,file)==2 &&
          memcmp(bytes,"12",2)==0 && fgetc(file)==EOF && feof(file));
    clearerr(file);
    check("eof-write-after-clearerr",fputc('3',file)=='3' && fflush(file)==0 &&
          fseek(file,0,SEEK_SET)==0 && fread(bytes,1,3,file)==3 && memcmp(bytes,"123",3)==0);
    check("mode-wplus-close",fclose(file)==0);

    file=fopen(PROBE_STDIO,"a+b");
    if(!check("mode-aplus-open",file!=NULL))return finish("STDIO");
    check("mode-aplus-read-from-start",ftell(file)==0 && fgetc(file)=='1');
    check("mode-aplus-always-append",fseek(file,0,SEEK_SET)==0 && fputc('4',file)=='4' &&
          fflush(file)==0 && ftell(file)==4 && fseek(file,0,SEEK_SET)==0 &&
          fread(bytes,1,4,file)==4 && memcmp(bytes,"1234",4)==0);
    check("mode-aplus-close",fclose(file)==0);
    return finish("STDIO");
}

static void lifecycle_a(void)
{
    fputs("APP_A\n",lifecycle_events);
}

static void lifecycle_plus(void)
{
    fputs("APP_PLUS\n",lifecycle_events);
}

static void lifecycle_b(void)
{
    fputs("APP_B\n",lifecycle_events);
    if(atexit(lifecycle_plus))fputs("REGISTER_FAILED\n",lifecycle_events);
}

static int lifecycle_mode(const char *mode,char *main_buffer,size_t capacity)
{
    int explicit_exit=strcmp(mode,"exit")==0;
    const char *path=explicit_exit?PROBE_EXIT:PROBE_RETURN;
    if(!check("life-directory",directory_ready()))return finish("LIFE");
    lifecycle_events=fopen(path,"w");
    if(!check("life-open",lifecycle_events!=NULL))return finish("LIFE");
    /* 数组属于main帧。显式exit由更深的C帧撤离，ROOT另栈收尾期间
     * 不可覆盖它；这里刻意不flush、不fclose，以真实文件证明顺序。 */
    memset(main_buffer,0x5a,capacity);
    if(!check("life-main-buffer",setvbuf(lifecycle_events,main_buffer,_IOFBF,capacity)==0))return finish("LIFE");
    if(!check("life-guard-stream",StdlibGuardSetStream(lifecycle_events)==0))return finish("LIFE");
    if(!check("life-app-a",atexit(lifecycle_a)==0))return finish("LIFE");
    if(!check("life-static-guard-callback",StdlibGuardRegister()==0))return finish("LIFE");
    if(!check("life-app-b",atexit(lifecycle_b)==0))return finish("LIFE");
    if(fprintf(lifecycle_events,"MAIN:%s\n",mode)<0){failures++;return finish("LIFE");}
    ScPathStatInfo info={sizeof(ScPathStatInfo),1,0,0};
    if(!check("life-still-unflushed",ScPathStat(path,&info)==0 && info.bytes==0))return finish("LIFE");
    int code=explicit_exit?38:37;
    if(printf("STDLIB_LIFECYCLE_ARMED %s exit=%d\n",mode,code)<0 || fflush(stdout))return 91;
    if(explicit_exit){
        exit(code);
        /* exit的返回将形成可见失败，而不是继续到正常返回标记。 */
        return 92;
    }
    return code;
}

static int shared_mode(char *main_buffer,size_t capacity)
{
    /* 这模式绝不向标准流写检查日志，否则会改变共享位置及BA证据。
     * 结果走另一普通文件；root同时核退出码、结果文本和原始BA字节。 */
    if(!directory_ready())return 93;
    FILE *result=fopen(PROBE_SHARED,"w");
    if(!result)return 93;
    int okay=setvbuf(stdout,main_buffer,_IOFBF,capacity)==0;
    if(fputs("A",stdout)==EOF)okay=0;
    if(fputs("B",stderr)==EOF)okay=0;
    if(fflush(stdout))okay=0;
    long position=ftell(stdout);
    fpos_t saved={0,0};
    int saved_status=fgetpos(stdout,&saved);
    okay=okay && position==2 && saved_status==0 && saved.position==2;
    if(fprintf(result,"STDLIB_SHARED_%s position=%ld fpos=%u\n",okay?"PASS":"FAIL",position,saved.position)<0)okay=0;
    if(fclose(result))okay=0;
    return okay?0:94;
}

static void recursive_callback(void)
{
    fputs("RECURSIVE_CALLBACK\n",lifecycle_events);
    printf("STDLIB_RECURSIVE_CALLBACK\n");
    fflush(stdout);
    exit(99);
    printf("STDLIB_RECURSIVE_RETURNED\n");
    fflush(stdout);
}

static int recursive_mode(char *main_buffer,size_t capacity)
{
    if(!check("recursive-directory",directory_ready()))return finish("RECURSIVE");
    lifecycle_events=fopen(PROBE_RECURSIVE,"w");
    if(!check("recursive-open",lifecycle_events!=NULL))return finish("RECURSIVE");
    if(!check("recursive-main-buffer",setvbuf(lifecycle_events,main_buffer,_IOFBF,capacity)==0))return finish("RECURSIVE");
    if(!check("recursive-guard-stream",StdlibGuardSetStream(lifecycle_events)==0))return finish("RECURSIVE");
    if(!check("recursive-register",atexit(recursive_callback)==0))return finish("RECURSIVE");
    if(fputs("MAIN_RECURSIVE\n",lifecycle_events)==EOF)return 91;
    /* 主控期望异常1，绝不能把main的43或第二次exit的99视为通过。
     * 此FILE保持缓冲，异常kernel回收不应替用户执行任何fini。 */
    if(printf("STDLIB_RECURSIVE_ARMED\n")<0 || fflush(stdout))return 91;
    return 43;
}

static int input_mode(int pipe_input)
{
    char text[8];
    /* 发出并刷新READY后才读，让主控能在实际空TTY/PIPE上供后续输入。
     * PIPE模式最后必须等writer真关闭才EOF，TTY模式不猜测EOF。 */
    if(printf("STDLIB_INPUT_READY %s\n",pipe_input?"pipe":"tty")<0 || fflush(stdout))return 95;
    char *line=fgets(text,sizeof(text),stdin);
    check("input-line-after-wait",line==text && strcmp(text,"Q\n")==0 && !feof(stdin) && !ferror(stdin));
    if(pipe_input)check("input-real-writer-eof",fgetc(stdin)==EOF && feof(stdin) && !ferror(stdin));
    return finish(pipe_input?"PIPE":"TTY");
}

int main(void)
{
    char mode[64],main_buffer[1024];
    ScU32 length=0;
    if(ScProcessArguments(mode,sizeof(mode),&length))return 90;
    if(!length || !mode[0])strcpy(mode,"core");
    if(strcmp(mode,"core")==0)return core_mode();
    if(strcmp(mode,"stdio")==0)return stdio_mode();
    if(strcmp(mode,"return")==0 || strcmp(mode,"exit")==0)return lifecycle_mode(mode,main_buffer,sizeof(main_buffer));
    if(strcmp(mode,"shared")==0)return shared_mode(main_buffer,sizeof(main_buffer));
    if(strcmp(mode,"recursive")==0)return recursive_mode(main_buffer,sizeof(main_buffer));
    if(strcmp(mode,"init-exit")==0){
        printf("STDLIB_INIT_EXIT_MAIN_RAN\n");
        fflush(stdout);
        return 97;
    }
    if(strcmp(mode,"pipe")==0)return input_mode(1);
    if(strcmp(mode,"tty")==0)return input_mode(0);
    printf("STDLIB_UNKNOWN_MODE %s\n",mode);
    fflush(stdout);
    return 96;
}
