#ifndef SANDCORE_CORE_SIGNATURE_H
#define SANDCORE_CORE_SIGNATURE_H
#include "core_image.h"

#define CORE_IMAGE_EXTENSION 2u
#define CORE_SIGNATURE_BYTES 64u
#define CORE_EXTENSION_MEMORY_MAX 0x01000000u
#define CORE_EXTENSION_CPU_SSE2 1u

/* 只包含验签入口；公钥由用户认可文件在构建时固定，缺失则全部拒绝。
 * 不读取盘上可替换的信任库，也没有签署/私钥/开发免验签接口。 */
int core_signature_available(void);
int core_signature_check(const u8 *file,u32 bytes);
void core_signature_key_id(u8 out[32]);
#endif
