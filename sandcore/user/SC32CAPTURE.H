#ifndef SANDCORE_SC32CAPTURE_H
#define SANDCORE_SC32CAPTURE_H
#include "SC32API.H"
#define SC32_CAPTURE_MIN_ABI_MINOR 4u
#define SC32_CAPTURE_MIN_LIBRARY_VERSION 6u
#define SC32_FILE_STATE_NATIVE_ENDPOINT 0x00200000u
/* 一次取得受管job和现代FILE。stdout/stderr各有独立动态native引用，
 * 不消耗父旧16fd；stdin为EOF，取消/领取沿ScJob，不用PID猜身份。 */
typedef struct {
    ScU32 size,version;
    ScJob job;
    ScFile output,error;
    ScU32 reserved[3];
}
ScCaptureInfo;
ScStatus ScProcessSpawnCapture(const char *command,ScCaptureInfo *out);
#endif
