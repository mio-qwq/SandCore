#ifndef SANDCORE_SC32DROP2_H
#define SANDCORE_SC32DROP2_H
#include "SC32DROP.H"

/* 最后UP可能排在应用尚未处理的MOVE之后。独立入口只消费内核实际
 * 路由过的本人capture释放凭证，不能把当前鼠标位置或伪造消息当授权。
 * 旧ScDropAt及SC32DROP.H记录保持；需要SC32API ABI1.5/library7。 */
enum {SC32_DROP_RELEASE_ABI_MINOR=5,SC32_DROP_RELEASE_LIBRARY_VERSION=7};
typedef struct {
    ScU32 size,version,source_window,source_generation;
    ScI32 screen_x,screen_y;
    ScU32 actions,flags,input_epoch,sequence_lo,sequence_hi,reserved;
} ScDropReleasePoint;
ScStatus ScDropAtRelease(const ScDropReleasePoint *point,ScDropInfo *out);
#endif
