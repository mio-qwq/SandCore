#include "SC32PRIVATE.H"

static ScI32 sc32_call(ScI32 number,ScI32 a,ScI32 b,ScI32 c,ScI32 d,ScI32 e,ScI32 f)
{
    /* 桥既可由GCC引导，也可由SCCC已有的固定约束汇编生成。
     * 参数指针最后才覆盖；四个cdecl保留寄存器成对保存/恢复。 */
    ScI32 arguments[7]={number,a,b,c,d,e,f};
    ScI32 result;ScI32 *p=arguments;
    __asm__ __volatile__(
        "push %%ebp; push %%ebx; push %%esi; push %%edi; "
        "mov 16(%%ecx),%%esi; mov 20(%%ecx),%%edi; mov 24(%%ecx),%%ebp; "
        "mov 4(%%ecx),%%ebx; mov 12(%%ecx),%%edx; mov (%%ecx),%%eax; "
        "mov 8(%%ecx),%%ecx; int $0x7c; "
        "pop %%edi; pop %%esi; pop %%ebx; pop %%ebp"
        :"=a"(result),"+c"(p)::"edx","cc","memory");
    return result;
}
