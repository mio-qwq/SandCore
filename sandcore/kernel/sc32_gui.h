#ifndef SANDCORE_SC32_GUI_H
#define SANDCORE_SC32_GUI_H
#include "io.h"
#include "../user/sc32/SC32GUIWIRE.H"

int sc32_gui_init(void);
int sc32_gui_call(int pid,u32 operation,u32 request,u32 output);
void sc32_gui_pump(void); /* task0 IF=0，旧mouse/key消费之后，合成之前。 */
int sc32_gui_stop_step(int pid); /* 退出每次至多32块；不执行用户过程。 */
int sc32_gui_wait_check(int pid,u32 request,u32 output,u32 *timeout);
int sc32_gui_wait_result(int pid,u32 events);

/* 仅WM真实状态提交调用；stamp给事实分配顺序，emit只复制数据。 */
int sc32_gui_prepare(int pid);
int sc32_gui_reserve(int pid,u32 messages);
void sc32_gui_reservation_end(int pid,int committed);
void sc32_gui_stamp(int pid,ScMessage *message);
void sc32_gui_emit(int pid,ScMessage *message);
u32 sc32_gui_attention_head(int pid);
void sc32_gui_attention_set(int pid,u32 head);
void sc32_gui_notify(int pid);
int sc32_gui_routing(void);
#endif
