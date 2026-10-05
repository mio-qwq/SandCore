#ifndef SCSIMD_H
#define SCSIMD_H
#include "SCAPI.H"
/* 四路整数向量用指针交换，避免把宿主编译器向量类型假定成SCCC
 * 新语言类型。未对齐加载/保存安全，能力不足保留逐元素路径。 */
static inline int sc_sse2_available(void){u32 info[8];return sc_simd_info(info)==0 && info[3]!=0;}
#ifdef __SCCC__
#define SC_VECTOR_TARGET
#else
#define SC_VECTOR_TARGET __attribute__((target("sse2"),noinline))
#endif
static SC_VECTOR_TARGET void sc_vector_add4(u32 *out,const u32 *a,const u32 *b)
{
        __asm__ __volatile__("movdqu (%%esi),%%xmm0; movdqu (%%edi),%%xmm1; paddd %%xmm1,%%xmm0; movdqu %%xmm0,(%%edx)"
            ::"S"(a),"D"(b),"d"(out):"xmm0","xmm1","memory");
}
static SC_VECTOR_TARGET void sc_vector_xor4(u32 *out,const u32 *a,const u32 *b)
{
        __asm__ __volatile__("movdqu (%%esi),%%xmm0; movdqu (%%edi),%%xmm1; pxor %%xmm1,%%xmm0; movdqu %%xmm0,(%%edx)"
            ::"S"(a),"D"(b),"d"(out):"xmm0","xmm1","memory");
}
static inline void sc_add4(u32 *out,const u32 *a,const u32 *b,int sse2)
{if(sse2)sc_vector_add4(out,a,b);else for(int i=0;i<4;i++)out[i]=a[i]+b[i];}
static inline void sc_xor4(u32 *out,const u32 *a,const u32 *b,int sse2)
{if(sse2)sc_vector_xor4(out,a,b);else for(int i=0;i<4;i++)out[i]=a[i]^b[i];}
#endif
