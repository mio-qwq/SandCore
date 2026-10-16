#ifndef SANDCORE_SC32DEBUGWIRE_H
#define SANDCORE_SC32DEBUGWIRE_H
#include "../SC32DEBUG.H"
/* 本头仅内核/SC32API/ROOT私有实现使用，不安装到普通INC。 */
#define SC32_D2_FIRST 0x900u
#define SC32_D2_LAUNCH 0x900u
#define SC32_D2_QUERY 0x901u
#define SC32_D2_CONTROL 0x902u
#define SC32_D2_READ 0x903u
#define SC32_D2_ADD 0x904u
#define SC32_D2_REMOVE 0x905u
#define SC32_D2_BREAKS 0x906u
#define SC32_D2_MODULES 0x907u
#define SC32_D2_SEGMENTS 0x908u
#define SC32_D2_SYMBOLS 0x909u
#define SC32_D2_REGISTER 0x910u
#define SC32_D2_UNREGISTER 0x911u
#define SC32_D2_READY 0x912u
#define SC32_D2_END 0x920u
#define SC32_D2_ERROR_BASE 0x1200u
typedef struct {
    ScU32 size,version; ScDebugRef target;
    ScU32 stop,action,address,bytes,flags,data,readable,id,generation;
    ScU32 after,epoch,rows,capacity,module,module_generation;
} Sc32DebugRequest;
typedef struct {
    ScU32 size,version,base,metadata,metadata_bytes,path,path_bytes,flags;
    ScU32 module,module_generation,reserved[2];
} Sc32DebugRegistration;
typedef struct { ScU32 size,version,module,generation; } Sc32DebugRegistrationResult;
#endif
