#ifndef SANDCORE_SC32IMAGEWIRE_H
#define SANDCORE_SC32IMAGEWIRE_H
#include "../SC32IMAGE.H"
/* 仅SC32库/服务实现使用；普通应用只见typed公开头。 */
enum {SC32_I_BEGIN=0xa00,SC32_I_WRITE=0xa01,SC32_I_SEAL=0xa02,SC32_I_CLAIM=0xa03,SC32_I_READ=0xa04};
typedef struct {ScU32 size,version,bytes,reserved;
} Sc32ImageBegin;
typedef struct {ScU32 size,version,ticket,reserved;
} Sc32ImageTicket;
typedef struct {ScU32 size,version,ticket,offset,address,bytes;
} Sc32ImageTransfer;
typedef struct {ScU32 size,version,kind,bytes;
    char path[64];
    ScU32 reserved[4];
} Sc32ImageSource;
#endif
