#ifndef SANDCORE_SC32PATH_H
#define SANDCORE_SC32PATH_H
#include "SC32API.H"

/* 独立增量接口；旧PathStat/Metadata的粗错误与旧SCAPI字节不改变。
 * 不取得FILE、不要求正文R，只按本人真实META/祖先遍历权限观察。
 * 成功输出既有48B ScPathInfo；失败不改out，不截断路径或伪造代数。 */
#define SC32_PATH_QUERY_MIN_ABI_MINOR 3u
#define SC32_PATH_QUERY_MIN_LIBRARY_VERSION 5u
#define SC32_CAP_PATH_QUERY_HI 4u
ScStatus ScPathQuery(const char *path,ScPathInfo *out);

#endif
