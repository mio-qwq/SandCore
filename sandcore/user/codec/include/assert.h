#ifndef SANDCORE_CODEC_ASSERT_H
#define SANDCORE_CODEC_ASSERT_H
#include <stdlib.h>
/* mio：第三方内部不变量失效只终止本三环服务，不能调用内核panic。
 * 不在零环打印第三方表达式/路径，客户端收到统一坏数据错误。 */
#ifdef NDEBUG
#define assert(condition) ((void)0)
#else
#define assert(condition) ((condition)?(void)0:abort())
#endif
#endif
