#ifndef SANDCORE_MANAGEMENT_H
#define SANDCORE_MANAGEMENT_H
#include "io.h"
void management_init(void);
void management_poll(void);
int management_console_write(const void *bytes,u32 length);
int management_console_write_atomic(const void *bytes,u32 length);
int management_connected(void);
#endif
