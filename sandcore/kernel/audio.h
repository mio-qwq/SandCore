#ifndef SANDCORE_AUDIO_H
#define SANDCORE_AUDIO_H
#include "io.h"
void audio_init(void);
void audio_irq(u32 irq);
void audio_poll(void);
/* 启动阶段在发布管理Shell前等待默认音效及已提交DMA真正排空。 */
int audio_busy(void);
void audio_stop_owner(int pid);
int audio_open(int pid,u32 rate,u32 channels);
int audio_submit(int pid,u32 token,const void *samples,u32 frames);
int audio_control(int pid,u32 token,u32 command,u32 value);
int audio_status(int pid,u32 token,u32 out[16]);
void audio_info(u32 out[16]);
int audio_effect(int kind);
int audio_power(int pid,int restart);
#endif
