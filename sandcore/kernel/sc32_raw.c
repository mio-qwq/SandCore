#include "sc32_raw.h"
#include "timer.h"
#include "wm.h"
#include "mouse.h"

#define RAW_CAPACITY 256u
static Sc32RawEvent raw_events[RAW_CAPACITY];
static volatile u32 raw_head,raw_tail;
static u32 raw_lost_lo,raw_lost_hi;
static int raw_enabled;
static u32 raw_mouse_buttons;
typedef char raw_record_keeps_wire[(sizeof(Sc32RawEvent)==64)?1:-1];

int sc32_raw_active(void){return raw_enabled;}
void sc32_raw_start(void)
{
    if(raw_enabled)return;
    raw_head=raw_tail=0;raw_lost_lo=raw_lost_hi=0;raw_mouse_buttons=mouse_buttons();raw_enabled=1;
}
int sc32_raw_peek(Sc32RawEvent *event)
{
    if(!event || raw_head==raw_tail)return 0;
    *event=raw_events[raw_tail];return 1;
}
void sc32_raw_pop(void)
{if(raw_head!=raw_tail)raw_tail=(raw_tail+1u)&(RAW_CAPACITY-1u);}
void sc32_raw_loss(u32 out[2])
{out[0]=raw_lost_lo;out[1]=raw_lost_hi;}

static void append(Sc32RawEvent *event)
{
    if(!raw_enabled)return;
    event->tick=sc_ticks;event->epoch=wm_sc32_route_epoch();event->session=wm_sc32_session_id();
    /* 只合并相邻、同会话/资格/修饰/按钮的运动。键和按钮边沿天然
     * 成为屏障；拖尺寸时不为同一批纯运动反复申请整幅新画布。 */
    if(event->kind==SC32_RAW_MOUSE && raw_head!=raw_tail){
        Sc32RawEvent *last=&raw_events[(raw_head+RAW_CAPACITY-1u)&(RAW_CAPACITY-1u)];
        if(last->kind==SC32_RAW_MOUSE && !(last->flags&SC32_RAW_BUTTON_EDGE)
            && !(event->flags&SC32_RAW_BUTTON_EDGE) && last->buttons==event->buttons
            && last->epoch==event->epoch && last->session==event->session
            && last->modifiers==event->modifiers){
            event->flags|=SC32_RAW_COALESCED;*last=*event;return;
        }
    }
    u32 next=(raw_head+1u)&(RAW_CAPACITY-1u);
    if(next==raw_tail){
        /* 不覆写尚未处理的按钮/键事实。GUI收到累计loss后同步输入
         * epoch与capture，不伪造断码，也不把缺口冒充正常事件。 */
        if(++raw_lost_lo==0)raw_lost_hi++;
        return;
    }
    raw_events[raw_head]=*event;raw_head=next;
}
void sc32_raw_key(u32 physical,u32 symbol,u32 text,u32 modifiers,u32 down,u32 repeat,u32 legacy)
{
    Sc32RawEvent event={0};event.kind=SC32_RAW_KEY;event.physical=physical;
    event.symbol=symbol;event.text=text;event.modifiers=modifiers;
    event.down=down;event.repeat=repeat;event.legacy=legacy;
    if(physical==255u)event.flags=SC32_RAW_PULSE;
    append(&event);
}
void sc32_raw_mouse(i32 x,i32 y,u32 buttons,u32 modifiers)
{
    Sc32RawEvent event={0};event.kind=SC32_RAW_MOUSE;
    event.x=x;event.y=y;event.buttons=buttons;event.modifiers=modifiers;
    /* 按下后的同按钮运动不能吞掉首次按下的位置，否则点击会命中
     * 拖动终点。边沿与纯运动分记录，相邻纯运动才可合并。 */
    if(buttons!=raw_mouse_buttons)event.flags=SC32_RAW_BUTTON_EDGE;
    raw_mouse_buttons=buttons;append(&event);
}
void sc32_raw_wheel(i32 x,i32 y,i32 vertical,i32 horizontal,u32 buttons,u32 modifiers)
{
    if(!vertical && !horizontal)return;
    /* 固定64B事实中kind独立解释physical/symbol为有符号轮量；不改
     * KEY/MOUSE字段意义。轮包本身是顺序屏障，不能被普通MOVE合并。 */
    Sc32RawEvent event={0};event.kind=SC32_RAW_WHEEL;event.x=x;event.y=y;
    event.physical=(u32)vertical;event.symbol=(u32)horizontal;
    event.buttons=buttons;event.modifiers=modifiers;append(&event);
}
