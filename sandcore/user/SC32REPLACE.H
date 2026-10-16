#ifndef SANDCORE_SC32REPLACE_H
#define SANDCORE_SC32REPLACE_H

#include "SC32LEASE.H"

/* ABI1.4 / SC32API.SCL v6新增。旧Publish的候选metadata语义完全保留。
 * 新入口只接受flags=EXPECT_EXACT、final_mode=0与本人exact target pin。
 * 提交时继承目标uid/gid/rw/flags，正文/extent来自已flush的私有候选。
 * 每批复验目标META+WRITE及parent权限；pin不授予chown或写权限。
 * Pending/Cancel/Advance/uncertain/DURABLE与原lease生命周期相同。 */
ScStatus ScFileLeasePublishPreserving(const ScLeasePublishDesc *desc,
                                    ScLeaseInfo *out);

#endif
