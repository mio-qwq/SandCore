#include "core_signature.h"
#include "crypto.h"
#include "core_public_key.h"
#include "../third_party/monocypher/src/optional/monocypher-ed25519.h"

static int initialized,valid;
static int canonical_point(const u8 point[32])
{
    /* Edwards编码的符号位不属于y。拒绝y>=2^255-19，统一线编码；
     * 上游接受部分非规范点，本映像合同刻意收紧至标准生成器的编码。 */
    for(int i=31;i>=0;i--){u8 v=point[i];if(i==31)v&=127;
        u8 limit=i==0?237:i==31?127:255;if(v<limit)return 1;if(v>limit)return 0;}
    return 0;
}
static int usable_point(const u8 point[32])
{
    if(!canonical_point(point))return 0;
    /* 若公钥属于低阶子群，R=单位点/S=0会对任意正文通过余因子方程。
     * 用这一公开的退化验签拒绝低阶键/随机量，不引入另一份曲线实现。
     * 非曲线输入会在随后真实验签中被上游拒绝。这里只处理公开数据。 */
    const u8 identity_signature[64]={1};
    return crypto_ed25519_check(identity_signature,point,0,0)!=0;
}
int core_signature_available(void)
{
    if(!initialized){initialized=1;valid=CORE_PUBLIC_KEY_CONFIGURED && usable_point(core_public_key);}
    return valid;
}
int core_signature_check(const u8 *file,u32 bytes)
{
    if(!file || bytes<CORE_IMAGE_HEADER+CORE_SIGNATURE_BYTES || !core_signature_available())return -1;
    const u8 *signature=file+bytes-CORE_SIGNATURE_BYTES;
    if(!usable_point(signature))return -1;
    return crypto_ed25519_check(signature,core_public_key,file,bytes-CORE_SIGNATURE_BYTES);
}
void core_signature_key_id(u8 out[32])
{if(core_signature_available())sha256(core_public_key,32,out);else crypto_zero(out,32);}
