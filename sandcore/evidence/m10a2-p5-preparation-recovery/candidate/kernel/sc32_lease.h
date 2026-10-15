#ifndef SANDCORE_SC32_LEASE_H
#define SANDCORE_SC32_LEASE_H
#include "io.h"
int sc32_lease_call(int pid,u32 operation,u32 request,u32 output);
int sc32_lease_stop_step(int pid);
void sc32_lease_poll(void);
#endif
