/* =====================================================================
 * mio：SandAsm，运行在 ring3 的两遍汇编器，输出 SCX1MIO 可执行文件。
 * 它直接读写 SandFS，不调用 NASM，也不借内核提供“汇编服务”。
 * 第一遍记录标签 RVA 并测量指令长度；第二遍已知所有地址，编码立即数
 * 与相对跳转。支持范围刻意明示，错误须带行号，不能默默生成坏机器码。
 * 输入/输出/符号表固定容量，所有写字节都经过 emit，溢出即停止。
 * ===================================================================== */
#include "api.h"
#define LIMIT 8192
#define SYMBOLS 64
/* source 永远保留原文供第二遍读取；output 前 36B 是容器头，at 只计
 * 后面的机器码，因此标签 RVA 不受 SCX 头长影响。符号表属于当前任务，
 * 同时运行两个 SandAsm 时，静态数组也由各自页表隔离，不共享解析状态。 */
static char source[LIMIT+1],args[128];
static u8 output[LIMIT+36];
static struct { char name[32]; u32 offset; } symbols[SYMBOLS];
static int nsym,pass,at,error,line_number,error_line;
static const char *why;
static char *cursor;

/* 只记录第一个错误。解析器为保证调用路径简单，报错后仍可能完成当前
 * 分支的少量动作；后续错误不得覆盖根因，assemble 会在下一次循环退出，
 * main 也绝不在 error 非零时调用 FSWRITE，半成品始终只留在私有内存。 */
static void fail(const char *s)
{ if(!error) { error=1; error_line=line_number; why=s; } }
static void space(void) { while(*cursor==' ' || *cursor=='\t') cursor++; }
static int identifier(char *out)
{
    space(); int n=0;
    while((*cursor>='a'&&*cursor<='z') || (*cursor>='A'&&*cursor<='Z')
       || (*cursor>='0'&&*cursor<='9') || *cursor=='_') {
        /* 错误路径同样要终止字符串：reg/statement 的调用者仍可能比较
         * 临时名字。只置 error 而漏 NUL，会让后续比较越过栈上 32B 数组。 */
        if(n>=31) { out[n]=0; fail("symbol too long"); return 0; }
        out[n++]=*cursor++;
    }
    out[n]=0;
    return n;
}
static int symbol(const char *name)
{ for(int i=0;i<nsym;i++) if(equal(name,symbols[i].name)) return i; return -1; }
/* 两遍必须经过相同的长度计数路径：第一遍只加 at，第二遍才落字节。
 * 若把容量检查只放在第二遍，第一遍符号 RVA 就可能超过合法输出域。
 * emit32 显式低字节优先，不借结构体强转，SCX 与 x86 均使用小端序。 */
static void emit(u32 b)
{
    if(at>=LIMIT) { fail("output exceeds 8192 bytes"); return; }
    if(pass==2) output[36+at]=(u8)b;
    at++;
}
static void emit32(u32 v)
{ for(int i=0;i<4;i++) { emit(v&255); v>>=8; } }

/* 数字支持十进制与 0x 十六进制；标签表示 base+RVA 的绝对虚址。
 * 第一遍遇到前向标签以零占位，但第二遍必须真正查到标签才能生成。
 * 数字运算用无符号，负立即数按 32 位补码编码，拒绝超出 32 位范围。 */
static u32 value(void)
{
    space(); int neg=0;
    if(*cursor=='-') { neg=1; cursor++; }
    if(*cursor>='0'&&*cursor<='9') {
        u32 v=0,base=10; int count=0;
        if(cursor[0]=='0' && cursor[1]=='x') { base=16; cursor+=2; }
        for(;;) {
            int d=*cursor>='0'&&*cursor<='9' ? *cursor-'0' :
                  *cursor>='a'&&*cursor<='f' ? *cursor-'a'+10 :
                  *cursor>='A'&&*cursor<='F' ? *cursor-'A'+10 : -1;
            if(d<0 || (u32)d>=base) break;
            if(v>(0xffffffffu-(u32)d)/base) fail("integer overflow");
            v=v*base+(u32)d; cursor++; count++;
        }
        if(!count) fail("missing number");
        return neg ? 0u-v : v;
    }
    char name[32];
    if(!identifier(name)) { fail("missing operand"); return 0; }
    if(neg) fail("negative symbol unsupported");
    int idx=symbol(name);
    if(idx<0) { if(pass==2) fail("undefined symbol"); return 0; }
    return 0x400000u+symbols[idx].offset;
}
/* 返回值就是 x86 编码的三位寄存器号，顺序不能按名字字母排序。
 * 例如 eax=000、ecx=001、ebx=011；B8+reg 与 ModR/M 共用这张表。
 * 出错时返回零只为了完成语法分支，error 会阻止任何文件输出。 */
static int reg(void)
{
    char name[32]; identifier(name);
    const char *names[]={"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
    for(int i=0;i<8;i++) if(equal(name,names[i])) return i;
    fail("expected 32-bit register"); return 0;
}
static void comma(void)
{ space(); if(*cursor!=',') fail("expected comma"); else cursor++; }

/* 语句只有“可选标签 + 一条指令”，不借通用表达式解释器猜测语法。
 * bits/org 是对固定装载契约的断言，不产生数据，也不能把链接地址改掉。
 * 标签仅在第一遍注册，第二遍重复走这一行时不能再次报重定义。
 * 末尾统一检查垃圾文本，防止 mov eax,1 typo 被截成一条成功指令。 */
static void statement(void)
{
    char op[32];
    space(); if(!*cursor || *cursor==';') return;
    if(*cursor=='[') {
        cursor++; identifier(op);
        u32 n=value(); space();
        if(*cursor!=']' || (!equal(op,"bits")&&!equal(op,"org"))
         || (equal(op,"bits")&&n!=32) || (equal(op,"org")&&n!=0x400000u)) fail("invalid bits/org");
        else cursor++;
    } else {
        if(!identifier(op)) { fail("expected instruction"); return; }
        space();
        if(*cursor==':') {
            cursor++;
            if(pass==1) {
                if(symbol(op)>=0) fail("duplicate symbol");
                else if(nsym>=SYMBOLS) fail("too many symbols");
                else { copy(symbols[nsym].name,op,32); symbols[nsym++].offset=(u32)at; }
            }
            space(); if(!*cursor || *cursor==';') return;
            if(!identifier(op)) { fail("expected instruction after label"); return; }
        }
        if(equal(op,"mov")) {
            /* B8..BF 后接 imm32；标签值已转换为 0x400000 + RVA。 */
            int r=reg(); comma(); u32 imm=value(); emit(0xb8u+(u32)r); emit32(imm);
        } else if(equal(op,"xor")) {
            /* 31 /r 的方向是 r/m32 ^= r32：mod=11 表示两个操作数
             * 都是寄存器，reg 三位放源、r/m 三位放目标，不能交换。 */
            int dest=reg(); comma(); int src=reg(); emit(0x31); emit(0xc0|(src<<3)|dest);
        } else if(equal(op,"int")) {
            u32 n=value(); if(n>255) fail("interrupt out of range"); emit(0xcd); emit(n);
        } else if(equal(op,"div")) {
            /* F7 /6：mod=11、reg=110，故第二字节固定前缀 0xF0。 */
            int r=reg(); emit(0xf7); emit(0xf0|r);
        } else if(equal(op,"jmp")) {
            /* E9 已写入并使 at 加一；随后还有 4B 位移，所以此处的
             * 下一条指令地址是 base+at+4。用无符号减法自然得到负偏移
             * 的补码；固定 rel32 保证前向/后向跳转两遍长度完全一致。 */
            u32 target=value(); emit(0xe9); emit32(target-(0x400000u+(u32)at+4));
        } else if(equal(op,"ret")) emit(0xc3);
        else if(equal(op,"nop")) emit(0x90);
        else if(equal(op,"db")) {
            /* 分号在引号内属于数据，只有 statement 最后才识别注释。
             * 当前没有转义解释，因此引号必须真的闭合，字节数值须≤255。 */
            do {
                space();
                if(*cursor=='\'' || *cursor=='"') {
                    char quote=*cursor++;
                    while(*cursor && *cursor!=quote) emit((u8)*cursor++);
                    if(*cursor!=quote) fail("unterminated string"); else cursor++;
                } else { u32 n=value(); if(n>255) fail("byte out of range"); emit(n); }
                space(); if(*cursor!=',') break; cursor++;
            } while(!error);
        } else fail("unsupported instruction");
    }
    space(); if(*cursor && *cursor!=';') fail("unexpected trailing text");
}

/* 每遍都从原始 source 逐行复制到暂存行，不原地破坏源缓冲。
 * 这使第二遍看到完全相同的字符；CRLF/LF 均归一化，错误行号保持一致。
 * 行长上限 255，过长一律报错，不能截断后误当成合法的另一条指令。 */
static void assemble(void)
{
    char line[256];
    for(pass=1;pass<=2 && !error;pass++) {
        const char *p=source; at=0; line_number=0;
        while(*p && !error) {
            int n=0; line_number++;
            while(*p && *p!='\n') {
                if(*p!='\r') {
                    if(n>=255) { fail("line exceeds 255 bytes"); break; }
                    line[n++]=*p;
                }
                p++;
            }
            if(*p=='\n') p++;
            line[n]=0; cursor=line; statement();
        }
    }
}
static void header(u32 offset,u32 value32)
{ for(int i=0;i<4;i++) output[offset+i]=(u8)(value32>>(8*i)); }

int main(void)
{
    int win=sc_open("SandAsm",290,164);
    if(win<0) return 1;
    page(win,"SANDASM / mio","ESC close");
    sc_args(args,sizeof(args)); char *p=args;
    char *input=token(&p),*target=token(&p),*mode=token(&p);
    int ide=equal(mode,"--ide"),code=0;
    char log[64]; copy(log,target,64); append(log,".log",64);
    if(!*input || !*target || (*mode&&!ide) || *token(&p)) {
        sc_text(win,8,38,"asm <source> <output.scx>",PAL_CON_WARN);
        if(!ide) wait_escape();
        return 2;
    }
    int n=sc_read(input,source,LIMIT);
    if(n<0 || n==LIMIT) {
        sc_text(win,8,38,"read failed / source too big",PAL_CON_WARN);
        if(!ide) wait_escape();
        return 3;
    }
    source[n]=0; assemble();
    /* 第二遍结束时 pass 已加到 3，但符号表/at 仍是最终结果。
     * _start 可以省略（默认 RVA 0），却不能只定义在载荷末尾；否则
     * “汇编成功”会产出加载器必定拒绝的文件。先拒绝再写盘，保留旧文件。 */
    int start=symbol("_start");
    if(!error && !at) { line_number=1; fail("empty program"); }
    if(!error && start>=0 && symbols[start].offset>=(u32)at) fail("entry outside payload");
    if(error) {
        char message[80],number[16]; decimal(number,error_line);
        copy(message,"line ",sizeof(message)); append(message,number,sizeof(message));
        append(message,":",sizeof(message));
        sc_text(win,8,38,message,PAL_CON_WARN);
        sc_text(win,8,54,why,PAL_TITLE);
        append(message," ",sizeof(message)); append(message,why,sizeof(message));
        sc_write(log,message,length(message)); code=4;
    } else {
        /* 头长 36 与装载基址由 docs/FS.md 权威定义；载荷紧跟头部，
         * entry 为 RVA，绝不把容器头的 36 字节加进程序内的标签地址。 */
        copy((char *)output,"SCX1MIO",8);
        header(8,start<0?0:symbols[start].offset); header(12,(u32)at);
        header(16,0); header(20,8192); header(24,0); header(28,0x400000);
        copy((char *)output+32,"MIO",4);
        int wrote=sc_write(target,output,at+36);
        code=wrote==at+36?0:5;
        const char *result=code?"Assembler write failed":"Assembler OK / generated SCX";
        sc_write(log,result,length(result));
        sc_text(win,8,38,wrote==at+36?"ASSEMBLED / SCX1MIO":"write failed",wrote==at+36?PAL_CON_TINT:PAL_CON_WARN);
        sc_text(win,8,58,input,PAL_TITLE); sc_text(win,8,76,target,PAL_TITLE);
        char number[16]; decimal(number,at);
        sc_text(win,8,98,number,PAL_CON_TINT); sc_text(win,80,98,"bytes",PAL_TITLE);
    }
    /* CLI 保留结果窗口供 M6 人工查看；IDE 模式以真实退出码完成等待。 */
    if(!ide) wait_escape();
    return code;
}
