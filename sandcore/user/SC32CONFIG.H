#ifndef SANDCORE_SC32CONFIG_H
#define SANDCORE_SC32CONFIG_H
#include "SC32API.H"
enum {
    SC32_CONFIG_USER=0,SC32_CONFIG_ENVIRONMENT=1,
    SC32_CONFIG_VALIDATE=0,SC32_CONFIG_SAVE=1,SC32_CONFIG_RELOAD=2,
    SC32_CONFIG_EXPECT_ABSENT=1,SC32_CONFIG_EXPECT_EXACT=2,
    SC32_CONFIG_VALIDATED=1,SC32_CONFIG_DURABLE=2,SC32_CONFIG_ACTIVE=4,
    SC32_CONFIG_UNCERTAIN=8,SC32_CONFIG_CREDENTIALS_UNCHANGED=16,
    SC32_CHECK_MENU=0,SC32_CHECK_SHORTCUT=1,SC32_CHECK_THEME=2,
    SC32_CHECK_DISPLAY=3,SC32_CHECK_WALLPAPER=4,
    SC32_CONFIG_MIN_ABI_MINOR=4,SC32_CONFIG_MIN_LIBRARY_VERSION=6
};
typedef struct {
    ScU32 size,version,kind,action;const char *candidate;
    ScU32 flags,expected_candidate_revision,expected_target_revision,reserved[8];
} ScConfigurationDesc;
typedef struct {
    ScU32 size,version,kind,action;ScStatus status;ScI32 raw;
    ScU32 flags,session,candidate_revision,target_revision,reserved[2];
} ScConfigurationResult;
/* kind仅USER/ENV，固定目标由真实本人会话决定。SAVE必须且只能
 * EXPECT_ABSENT或EXACT；候选需非零exact revision。RELOAD不传候选，
 * 仍可带EXACT固定目标条件。VALIDATE不写盘/不激活。
 * 输出映射/头错误不改out；实际操作失败可返回完整Result，只有
 * DURABLE/ACTIVE标记才分别证明写盘/运行配置更新，UNCERTAIN不得重放。 */
ScStatus ScConfigurationApply(const ScConfigurationDesc *,ScConfigurationResult *out);
/* 旧消费格式的纯检查；不会写盘、切模式或更改主题代数。 */
ScStatus ScConfigurationCheck(ScU32 kind,const char *path);
/* 仅沿旧0x24的会话/权限及旧菜单重载，原SCAPI字节不变。 */
ScStatus ScDesktopReload(void);
#endif
