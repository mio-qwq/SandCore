#ifndef SANDCORE_SCLJUMPPRIVATE_H
#define SANDCORE_SCLJUMPPRIVATE_H
#include "SCLDR.H"

/* 仅ROOT使用的真实i386控制点。字段偏移与GCC naked / SCCC两个
 * 私有intrinsic共同固定；不把continuation公布成setjmp或kernel地址。 */
typedef struct {
    ScU32 caller_sp,caller_bp,caller_bx,caller_si,caller_di,landing_pc;
    ScU32 termination_top,finish,context,status,reason,armed;
} ScRootJumpI386;
typedef char sc_root_jump_is_48[(sizeof(ScRootJumpI386)==48)?1:-1];
#define SC_ROOT_TERMINATION_BYTES 131072u

#ifdef __SCCC__
#ifndef __SCCC_ROOTTERM__
#error ROOT termination requires real SCCC root run and unwind intrinsics
#endif
#endif

int __scl_root_run(ScRootJumpI386 *jump,int (*entry)(void),
                   int (*finish)(void *context),void *context);
#ifdef __GNUC__
void __scl_root_unwind(ScRootJumpI386 *jump) __attribute__((noreturn));
#else
void __scl_root_unwind(ScRootJumpI386 *jump);
#endif
#endif
