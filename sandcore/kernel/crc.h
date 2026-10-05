#ifndef SANDCORE_CRC_H
#define SANDCORE_CRC_H
#include "io.h"
/* 标准CRC-32/ISO-HDLC；参数为上一块的最终CRC，首块传0。
 * 只检测传输/磁盘损坏，不能当模块签名或身份认证。 */
u32 crc32_update(u32 crc,const void *data,u32 bytes);
#endif
