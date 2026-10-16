#ifndef SANDCORE_SC32AUTH_H
#define SANDCORE_SC32AUTH_H
#include "SC32API.H"
/* 凭据只用于本次同步Begin，库不复制/保存明文。两段均为NUL结尾
 * 字节串，用户名1..31B、密码0..128B，不允许嵌入NUL；GUI输入为UTF-8。 */
typedef struct {ScU32 size,version;const char *username,*password;ScU32 username_bytes,password_bytes,flags,reserved;} ScAuthLoginDesc;
typedef struct {ScU32 size,version,id,owner_generation;} ScAuthTicket;
enum {SC32_AUTH_PENDING=1,SC32_AUTH_READY=2,SC32_AUTH_DENIED=3,SC32_AUTH_PROGRESS=1};
typedef struct {ScU32 size,version,state,flags,iterations_done,iterations_total;ScI32 uid,cause;ScU32 reserved[4];} ScAuthLoginInfo;
ScStatus ScAuthLoginBegin(const ScAuthLoginDesc *,ScAuthTicket *out);
/* 返回0且state=DENIED是实际认证拒绝；陈旧/过期/数据库变化返回错误。
 * 仅PENDING的真实PBKDF计数置PROGRESS，完成后不伪造100%。 */
ScStatus ScAuthLoginQuery(const ScAuthTicket *,ScAuthLoginInfo *out);
/* 只激活内核创建的新正常会话；不改变调用者UID/SID。成功消费票据。 */
ScStatus ScAuthLoginActivate(const ScAuthTicket *);
ScStatus ScAuthLoginCancel(const ScAuthTicket *);
#endif
