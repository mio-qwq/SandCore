#ifndef RESEARCHDESK_RDSTATS_H
#define RESEARCHDESK_RDSTATS_H
#include "SCTYPES.H"
/* 插件只有普通Ring3计算权；调用者持有输入/输出，不交出文件或窗口句柄。 */
typedef struct {
    ScU32 size,version,count,sum_lo,sum_hi,positive,negative,zero;
    ScI32 minimum,maximum;
    ScU32 reserved[6];
}
RdStatsState;
typedef struct {
    ScU32 size,version,count,positive,negative,zero;
    ScI32 minimum,maximum,mean;
    ScU32 reserved[3];
    char sum[24];
}
RdStatsResult;
typedef ScStatus (*RdStatsBeginProc)(RdStatsState *);
typedef ScStatus (*RdStatsPushProc)(RdStatsState *,const ScI32 *,ScU32);
typedef ScStatus (*RdStatsFinishProc)(const RdStatsState *,RdStatsResult *);
ScStatus RdStatsBegin(RdStatsState *);
ScStatus RdStatsPush(RdStatsState *,const ScI32 *,ScU32);
ScStatus RdStatsFinish(const RdStatsState *,RdStatsResult *);
#endif
