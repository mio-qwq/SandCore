#ifndef SANDCORE_CRYPTO_H
#define SANDCORE_CRYPTO_H
#include "io.h"
/* M9：无libc/浮点的SHA-256/HMAC。密码派生拆成小批，主循环轮询，
 * 不在一次陷入中跑完大量迭代而冻结GUI/串口/其它任务。 */
typedef struct {u32 h[8],lo,hi,used;u8 block[64];} sha256_t;
typedef struct {sha256_t inner,outer;} hmac256_t;
typedef struct {hmac256_t key;u8 u[32],value[32];u32 done,total;} pbkdf256_t;
void crypto_zero(void *data,u32 bytes);
int crypto_equal(const u8 *a,const u8 *b,u32 bytes);
void sha256_init(sha256_t *s);
void sha256_update(sha256_t *s,const void *data,u32 bytes);
void sha256_final(sha256_t *s,u8 out[32]);
void sha256(const void *data,u32 bytes,u8 out[32]);
void hmac256_init(hmac256_t *s,const void *key,u32 bytes);
void hmac256(const hmac256_t *s,const void *data,u32 bytes,u8 out[32]);
int pbkdf256_init(pbkdf256_t *s,const void *password,u32 bytes,const u8 salt[16],u32 iterations);
int pbkdf256_step(pbkdf256_t *s,u32 budget);
#endif
