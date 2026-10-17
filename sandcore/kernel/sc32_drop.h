#ifndef SANDCORE_SC32_DROP_H
#define SANDCORE_SC32_DROP_H
#include "io.h"
#include "../user/dropapi/SC32DROPWIRE.H"

int sc32_drop_init(void);
int sc32_drop_available(void);
int sc32_drop_call(int pid,u32 operation,u32 request,u32 output);
void sc32_drop_poll(void);
int sc32_drop_pending(void);
int sc32_drop_stop_step(int pid);
void sc32_drop_session_discard(u32 session);
void sc32_drop_window_closed(u32 window);
void sc32_drop_window_changed(u32 window);
u32 sc32_drop_registration(u32 window);
#endif
