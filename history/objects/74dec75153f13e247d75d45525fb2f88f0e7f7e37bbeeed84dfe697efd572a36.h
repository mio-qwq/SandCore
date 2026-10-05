#ifndef SANDCORE_WIDE_INTEGER_LIBRARY_H
#define SANDCORE_WIDE_INTEGER_LIBRARY_H
#include "SCAPI.H"
/* =====================================================================
 * mio：双32位整数源库，完整乘积/进位及有界Q16比值，不依赖long long。
 *
 * SCCC的C子集只有32位整数，但i386本来就能用EDX:EAX保存64位乘积。
 * 精确三角形的三次乘积必须先保留完整值，再比较重心分子/分母；
 * 不能提前除256，让舍入误差把三角形外的像素误判成命中。
 * 本库也可用于时间/统计、整数几何与其它明确需要宽累加的用户程序，
 * 不拥有场景、窗口、堆、文件或内核状态。
 *
 * ScwInt固定8B：low是低32位，high是高32位二补码；按有符号解释
 * 用cmp_signed，按无符号64位解释用cmp_unsigned。add/sub/neg与
 * dot3是模2^64算术，完整i32*i32乘积本身总可精确表示。允许输出
 * 与任一输入是同一对象；调用者提供存活的8B对象，不隐含分配。
 *
 * ratio16对有符号N/D执行向零截断的(N*65536)/D；分子幅值必须
 * <=2^48-1，分母可为任意非零64位整数。结果超出±INT_MAX时
 * 饱和并返回SCW_SATURATED，范围/除零错误写0并返回负错误。
 * 不把无法左移的任意64位分子暗称已正确计算。精确几何的坐标/
 * 方向合同可以证明落在此范围，其它调用者也须遵守该前提。
 *
 * GCC或__SCCC_WIDE__选基本整数IMUL/ADD/ADC快路；历史M7和旧
 * G2没有该能力宏，仍用有证明的16位肢体C路径，结果完全相同。
 * 这是源库/API的新增能力，不改变SCAPI.H任何调用号或缓冲布局。
 * ===================================================================== */
typedef struct {u32 low;int high;} ScwInt;
#define SCW_OK 0
#define SCW_SATURATED 1
#define SCW_RANGE -1
#define SCW_DIVZERO -2
static void scw_mul32(ScwInt *out,int a,int b);
static void scw_add(ScwInt *out,const ScwInt *a,const ScwInt *b);
static void scw_sub(ScwInt *out,const ScwInt *a,const ScwInt *b);
static void scw_neg(ScwInt *out,const ScwInt *a);
static int scw_cmp_signed(const ScwInt *a,const ScwInt *b);
static int scw_cmp_unsigned(const ScwInt *a,const ScwInt *b);
static void scw_dot3(ScwInt *out,int ax,int ay,int az,int bx,int by,int bz);
static int scw_ratio16(int *out,const ScwInt *numerator,const ScwInt *denominator);
#endif
