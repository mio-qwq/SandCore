#ifndef SANDCORE_MEMORY_SOURCE_IMPLEMENTATION
#define SANDCORE_MEMORY_SOURCE_IMPLEMENTATION
#include "SCMEM.H"
/* mio：预生成只能减少计算，若缓存搬运仍逐像素用低效表达式栈，
 * 界面和游戏仍会卡。这里提供通用批处理，而非给单个游戏写替身。
 * 每个函数均保留原始目的地址作为返回值，ASM更新的指针只用于尾部。
 * REP自身可被CPU逐元素抢占；DF始终为0，避免把内核中断处理带入
 * 逆序状态。+S/+D/+c是已有固定寄存器约束，含ASM的函数按cdecl
 * 保存EBX/ESI/EDI；没有新增可变的线程/FPU/SIMD上下文。 */
static void *sc_mem_copy(void *destination,const void *source,u32 bytes)
{
    if(!bytes||destination==source)return destination;
    u8 *out=(u8 *)destination;const u8 *in=(const u8 *)source;
#if !defined(__SCCC__) || defined(__SCCC_REP__)
    // i386允许非对齐字符串访问；四字节主体和不足四字节的尾部
    // 精确覆盖bytes，不为了快而写到后面的另一对象或护栏。
    u32 words=bytes>>2;
    __asm__ __volatile__("cld; rep movsl" : "+D"(out),"+S"(in),"+c"(words) :: "memory","cc");
    u32 tail=bytes&3u;
    if(tail)__asm__ __volatile__("cld; rep movsb" : "+D"(out),"+S"(in),"+c"(tail) :: "memory","cc");
#else
    for(u32 i=0;i<bytes;i++)out[i]=in[i];
#endif
    return destination;
}
static void *sc_mem_move(void *destination,const void *source,u32 bytes)
{
    if(!bytes||destination==source)return destination;
    // 以无符号地址差判定真正的后向重叠，不构造source+bytes溢出。
    // 向较低地址移动可安全走前向REP；较高且重叠时逐字节逆序。
    // 逆序罕见编辑路径保持正确比STD更重要，DF=1可能遇到PIT中断。
    u8 *out=(u8 *)destination;const u8 *in=(const u8 *)source;
    if((u32)out>(u32)in&&(u32)out-(u32)in<bytes){
        for(u32 i=bytes;i;i--)out[i-1]=in[i-1];
        return destination;
    }
    return sc_mem_copy(destination,source,bytes);
}
static void *sc_mem_fill(void *destination,int value,u32 bytes)
{
    if(!bytes)return destination;
    u8 *out=(u8 *)destination;
#if !defined(__SCCC__) || defined(__SCCC_REP__)
    u32 count=bytes;
    __asm__ __volatile__("cld; rep stosb" : "+D"(out),"+c"(count) : "a"((u32)(u8)value) : "memory","cc");
#else
    for(u32 i=0;i<bytes;i++)out[i]=(u8)value;
#endif
    return destination;
}
static u32 *sc_mem_fill32(u32 *destination,u32 value,u32 words)
{
    if(!words)return destination;
#if !defined(__SCCC__) || defined(__SCCC_REP__)
    u32 *out=destination,count=words;
    __asm__ __volatile__("cld; rep stosl" : "+D"(out),"+c"(count) : "a"(value) : "memory","cc");
#else
    for(u32 i=0;i<words;i++)destination[i]=value;
#endif
    return destination;
}
#endif
