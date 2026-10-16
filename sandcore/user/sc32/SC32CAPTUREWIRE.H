#ifndef SANDCORE_SC32CAPTUREWIRE_H
#define SANDCORE_SC32CAPTUREWIRE_H
#include "../SC32CAPTURE.H"
#define SC32_F_CAPTURE 0x20bu
#define SC32_FILE_NATIVE_ENDPOINT SC32_FILE_STATE_NATIVE_ENDPOINT
typedef struct {
    ScU32 size,version,command,flags,reserved[4];
}
Sc32CaptureRequest;
#endif
