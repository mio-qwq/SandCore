#include "crypto.h"
#include "shell_host_issue.h"

/* 只核已经读入唯一SCX暂存的真实字节，随后从同一内存映像建pd；
 * 不先hash磁盘再重读另一个版本，不把固定文件名或迁移后的SYSTEM
 * 元数据当发行来源。摘要由正式构建从实际SCSHELL.SCX生成，无通配。 */
static int shell_issue_verify(const u8 *image,u32 bytes)
{
    if(!image || bytes!=shell_host_issued_bytes)return -5;
    u8 digest[32];sha256(image,bytes,digest);
    int accepted=crypto_equal(digest,shell_host_issued_sha256,sizeof(digest));
    crypto_zero(digest,sizeof(digest));return accepted?0:-5;
}
