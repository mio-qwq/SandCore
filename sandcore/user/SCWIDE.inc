#ifndef SANDCORE_WIDE_INTEGER_IMPLEMENTED
#define SANDCORE_WIDE_INTEGER_IMPLEMENTED
#include "SCWIDE.H"
static void scw_add(ScwInt *out,const ScwInt *a,const ScwInt *b)
{
    u32 low=a->low+b->low,high=(u32)a->high+(u32)b->high+(low<a->low);
    out->low=low;out->high=(int)high;
}
static void scw_sub(ScwInt *out,const ScwInt *a,const ScwInt *b)
{
    u32 low=a->low-b->low,high=(u32)a->high-(u32)b->high-(a->low<b->low);
    out->low=low;out->high=(int)high;
}
static void scw_neg(ScwInt *out,const ScwInt *a)
{
    u32 low=0u-a->low,high=~(u32)a->high+(a->low==0);
    out->low=low;out->high=(int)high;
}
static int scw_cmp_signed(const ScwInt *a,const ScwInt *b)
{
    if(a->high!=b->high)return a->high<b->high?-1:1;
    return a->low==b->low?0:a->low<b->low?-1:1;
}
static int scw_cmp_unsigned(const ScwInt *a,const ScwInt *b)
{
    if(a->high!=b->high)return (u32)a->high<(u32)b->high?-1:1;
    return a->low==b->low?0:a->low<b->low?-1:1;
}
static void scw_mul32(ScwInt *out,int a,int b)
{
#if defined(__GNUC__) || defined(__SCCC_WIDE__)
    u32 low=(u32)a;int high;
    __asm__ __volatile__("imull %%ecx" : "+a"(low),"=d"(high) : "c"(b) : "cc");
    out->low=low;out->high=high;
#else
    /* 16位肢体四次乘法：每个中间量都按u32计算。t的最大值是
     * 0xFFFF*0xFFFF+0xFFFF，仍<=UINT_MAX，不发生隐藏有符号
     * 溢出。先算完整无符号幅值，再整体二补码恢复符号；INT_MIN
     * 的幅值也用0u-(u32)a取得，绝不对INT_MIN做有符号取负。 */
    u32 aa=a<0?0u-(u32)a:(u32)a,bb=b<0?0u-(u32)b:(u32)b;
    u32 al=aa&65535u,ah=aa>>16,bl=bb&65535u,bh=bb>>16;
    u32 w0=al*bl,t=ah*bl+(w0>>16),middle=t&65535u,high=t>>16;
    t=al*bh+middle;out->low=(t<<16)|(w0&65535u);
    out->high=(int)(ah*bh+high+(t>>16));
    if((a<0)!=(b<0))scw_neg(out,out);
#endif
}
static void scw_add_product(ScwInt *out,int a,int b)
{
#if defined(__GNUC__) || defined(__SCCC_WIDE__)
    u32 low=(u32)a;int high;
    /* MUL的完整高字加上ADD低字的真实CF；高字二补码按模2^32
     * 累加，不靠C有符号溢出推断进位。ESI来自既有S约束，模板
     * 写入调用者自己的8B对象；memory与cc都明确声明。编译器
     * 必须保存callee的ESI，不能为几何加速破坏普通C调用约定。 */
    __asm__ __volatile__("imull %%ecx; addl %%eax, (%3); adcl %%edx, 4(%3)"
        : "+a"(low),"=d"(high) : "c"(b),"S"(out) : "memory","cc");
#else
    ScwInt product;scw_mul32(&product,a,b);scw_add(out,out,&product);
#endif
}
static void scw_dot3(ScwInt *out,int ax,int ay,int az,int bx,int by,int bz)
{
    scw_mul32(out,ax,bx);scw_add_product(out,ay,by);scw_add_product(out,az,bz);
}
static void scw_dot3v(ScwInt *out,const int *a,const int *b)
{
#if defined(__GNUC__) || defined(__SCCC_WIDE__)
    /* mio：实测精确车体的大量射线停在宽点积及其三个乘积callee。
     * 每个向量已经连续存储，却先把六个标量压栈，再调用乘法和两次
     * 累加；SCCC没有内联优化，固定开销在每个三角形/主光/反射/阴影
     * 重复。这里保留完整i32乘积与64位进位，只合并数据载入和callee，
     * 不截掉高字、不改变重心/反射，也不请求新处理器能力。
     *
     * 快路第一次就写out，所以必须先排除out的8B区间与任一输入
     * 12B区间相交。地址用u32差值检查两个方向，不使用指针跨对象
     * 相减，也不计算可能回绕的末端地址。有效对象不能跨越地址空间
     * 末端；普通三环调用者本就须提供完整存活对象。输入a与b可以
     * 彼此重叠或相同，因为它们只读。out重叠则先读取全部六标量
     * 再执行原函数，保证第二/第三乘积不读到第一次写出的结果。
     * 本库没有隐藏缓存，返回后不留指针，任务退出无需另清生命周期。 */
    u32 address=(u32)out,left=(u32)a,right=(u32)b;
    if(address-left<12u||left-address<8u||address-right<12u||right-address<8u){
        scw_dot3(out,a[0],a[1],a[2],b[0],b[1],b[2]);return;
    }
    u32 low;int high;
    /* ESI/ECX/EDI只读持有三个地址，EAX/EDX接完整有符号乘积。
     * 每次ADD紧接ADC，CF不能被下一条IMUL覆盖。三个来源在返回
     * 前只读，输出严格只写偏移0/4两字；memory/cc明确告知编译器。
     * 基本i386内存操作数IMUL即可，不能把它与SIMD/FPU特例混淆。 */
    __asm__ __volatile__(
        "movl (%%esi), %%eax; imull (%%ecx); movl %%eax, (%%edi); movl %%edx, 4(%%edi); "
        "movl 4(%%esi), %%eax; imull 4(%%ecx); addl %%eax, (%%edi); adcl %%edx, 4(%%edi); "
        "movl 8(%%esi), %%eax; imull 8(%%ecx); addl %%eax, (%%edi); adcl %%edx, 4(%%edi)"
        : "=a"(low),"=d"(high) : "S"(a),"c"(b),"D"(out) : "memory","cc");
#else
    /* 历史M7不具备宽乘汇编能力，仍编译原纯C完整乘积；所有实参
     * 在进入scw_dot3前读取，部分别名合同也相同，绝不暗改旧程序。 */
    scw_dot3(out,a[0],a[1],a[2],b[0],b[1],b[2]);
#endif
}
static u32 scw_unsigned_divide(const ScwInt *numerator,const ScwInt *denominator)
{
    /* 历史M7对const结构体的声明初始化要求完全相同的内部类型号，
     * 会拒绝这个合法的只读值复制。显式读两个字段兼容旧编译器，
     * 保留const接口，不强制转换为可写指针，也不申请隐藏副本页。 */
    ScwInt n,d;n.low=numerator->low;n.high=numerator->high;
    d.low=denominator->low;d.high=denominator->high;
    if(!d.high){
        /* 此私有函数只接收非零分母。高字>=除数说明商超过u32，
         * 先返回饱和值，避免实际DIV产生#DE。其它情况一次DIV
         * 取得同一完整64/32商；这是基本i386整数能力。 */
        if((u32)n.high>=d.low)return 0xFFFFFFFFu;
        u32 low=n.low,high=(u32)n.high;
        __asm__ __volatile__("div %%ecx" : "+a"(low),"+d"(high) : "c"(d.low) : "cc");
        return low;
    }
    if(scw_cmp_unsigned(&n,&d)<0)return 0;
    /* 分母>=2^32时，任何64位分子的商都小于2^32。对齐最高
     * 有效位后最多32次试减；不把每条普通小三角形都拉进逐位
     * 循环。左移前检查最高位，再比较下一值，避免模64回绕。
     * 高字右移一律无符号，不能让负int的SAR补1污染除数。 */
    u32 bit=1,result=0;
    while(!((u32)d.high&0x80000000u)){
        ScwInt next;next.low=d.low<<1;next.high=(int)(((u32)d.high<<1)|(d.low>>31));
        if(scw_cmp_unsigned(&next,&n)>0)break;
        d=next;bit<<=1;
    }
    while(bit){
        if(scw_cmp_unsigned(&n,&d)>=0){scw_sub(&n,&n,&d);result|=bit;}
        d.low=(d.low>>1)|((u32)d.high<<31);d.high=(int)((u32)d.high>>1);bit>>=1;
    }
    return result;
}
static int scw_ratio16(int *out,const ScwInt *numerator,const ScwInt *denominator)
{
    *out=0;if(!denominator->low&&!denominator->high)return SCW_DIVZERO;
    ScwInt n,d;n.low=numerator->low;n.high=numerator->high;
    d.low=denominator->low;d.high=denominator->high;
    int negative=(n.high<0)!=(d.high<0);
    if(n.high<0)scw_neg(&n,&n);if(d.high<0)scw_neg(&d,&d);
    if((u32)n.high>65535u)return SCW_RANGE;
    n.high=(int)(((u32)n.high<<16)|(n.low>>16));n.low<<=16;
    u32 quotient=scw_unsigned_divide(&n,&d);
    if(quotient>2147483647u){*out=negative?-2147483647:2147483647;return SCW_SATURATED;}
    *out=negative?-(int)quotient:(int)quotient;return SCW_OK;
}
#endif
