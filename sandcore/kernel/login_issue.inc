#include "crypto.h"
#include "login_program_issue.h"
/* 与固定SHELL相同：只检查本次已经读入的实际SCX暂存，随后用同一
 * 字节建pd。无通配、无用户文件名证明、无签名私钥或开发信任入口。 */
static int login_issue_verify(const u8 *image,u32 bytes)
{
    if(!image || bytes!=login_issued_bytes)return -5;
    u8 digest[32];
    sha256(image,bytes,digest);
    int accepted=crypto_equal(digest,login_issued_sha256,32);
    crypto_zero(digest,sizeof(digest));
    return accepted?0:-5;
}
