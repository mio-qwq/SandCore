#ifndef SANDCORE_RINGDEBUG_H
#define SANDCORE_RINGDEBUG_H
#include "io.h"
void ringdebug_init(void);
void ringdebug_poll(void);
void ringdebug_irq(u32 *frame);
int ringdebug_exception(u32 vector,u32 *frame);
#endif
