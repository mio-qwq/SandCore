#ifndef SANDCORE_SC32_BROKER_H
#define SANDCORE_SC32_BROKER_H
#include "io.h"
#include "../user/sc32/SC32BROKERWIRE.H"

int sc32_broker_init(void);
int sc32_broker_available(void);
int sc32_broker_kernel_close(int pid,u32 handle);
int sc32_broker_identity(int pid,ScBrokerIdentity *out);
int sc32_broker_drop_endpoint(int owner,u32 endpoint,u32 session,ScBrokerIdentity *out);
int sc32_broker_drop_connect(int source,int owner,u32 endpoint,Sc32BrokerToken *out);
int sc32_broker_call(int pid,u32 operation,u32 request,u32 output);
void sc32_broker_poll(void); /* task0 IF=0；通知/GC各有32项总预算。 */
int sc32_broker_pending(void);
int sc32_broker_stop_step(int pid); /* 本PID/gen链，每步32句柄、映射16页。 */
void sc32_broker_session_discard(u32 session); /* 在session释放/注销时撤当前clip。 */
/* task.c在相同IF=0陷入内调用check后立即task_wait_install，不跨用户态。
 * check=0表示*events已ready；1表示待睡，负值是稳定SC32错误。
 * 等待使用旧mode0事件返回，不保存request/out或其它用户指针。 */
int sc32_broker_wait_check(int pid,u32 request,u32 output,u32 *timeout,u32 *mask,u32 *events);

/* 分页真实增量正文在kernel/paging_shared.inc；草稿留档不参与构建。
 * reserve仅标记本人60000000窗口的PTE，bind/unbind各限16页，不free
 * 共享PF；对象最后ref才在broker GC归还帧。private free不接这些VA。 */
u32 paging_shared_reserve(u32 pd,u32 bytes);
int paging_shared_bind(u32 pd,u32 base,u32 index,const u32 *frames,u32 count,u32 access);
int paging_shared_unbind(u32 pd,u32 base,u32 index,u32 count);
int paging_shared_release(u32 pd,u32 base,u32 bytes);

#endif
