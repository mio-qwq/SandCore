#ifndef SANDCORE_SC32_RAW_H
#define SANDCORE_SC32_RAW_H
#include "io.h"

/* IRQ只写一条固定事实，不分配GUI节点或调用三环过程。键鼠共用FIFO，
 * task0在IF=0消费；数量仅是不可分配的中断突发预算，不是任务上限。 */
#define SC32_RAW_KEY 1u
#define SC32_RAW_MOUSE 2u
#define SC32_RAW_COALESCED 1u
#define SC32_RAW_PULSE 2u
#define SC32_RAW_BUTTON_EDGE 4u
typedef struct {
    u32 kind,tick,epoch,session,physical,symbol,text,modifiers,down,repeat,legacy;
    i32 x,y;u32 buttons,flags,reserved;
} Sc32RawEvent;
int sc32_raw_active(void);
void sc32_raw_start(void); /* 仅旧键/鼠队列均空后由task0 IF=0调用。 */
int sc32_raw_peek(Sc32RawEvent *event);
void sc32_raw_pop(void);
void sc32_raw_loss(u32 out[2]);
void sc32_raw_key(u32 physical,u32 symbol,u32 text,u32 modifiers,u32 down,u32 repeat,u32 legacy);
void sc32_raw_mouse(i32 x,i32 y,u32 buttons,u32 modifiers);
#endif
