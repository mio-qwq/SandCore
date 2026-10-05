#ifndef RTC_H
#define RTC_H
#include "io.h"
/* 固定32B只读日历快照，调用失败不输出半次更新。 */
int rtc_snapshot(u32 out[8]);
#endif
