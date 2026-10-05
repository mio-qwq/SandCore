#ifndef SANDCORE_RANDOM_H
#define SANDCORE_RANDOM_H
#include "io.h"
/* 强熵源：硬件RDRAND，或QEMU外部调试机提供的每次启动fw_cfg种子。
 * 没有强熵时失败关闭，绝不把PIT/磁盘序列/地址当不可预测令牌。 */
void random_init(void);
int random_ready(void);
int random_bytes(void *data,u32 bytes);
#endif
