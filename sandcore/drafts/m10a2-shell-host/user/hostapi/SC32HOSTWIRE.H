#ifndef SANDCORE_SC32HOSTWIRE_H
#define SANDCORE_SC32HOSTWIRE_H
#include "../SC32HOST.H"

/* 此 selector 位于新 SYS_SC32 gate 内，不借旧 SPAWN2 flags 扩语义。 */
#define SC32_H_LAUNCH 0x800u
#define SC32_H_QUERY 0x801u
#define SC32_H_PREPARE 0x802u
#define SC32_H_ACTIVATE 0x803u
#define SC32_H_CONNECT 0x804u
#define SC32_H_WORKSPACE 0x805u
#define SC32_H_WINDOWS 0x806u
#define SC32_H_WINDOW_ACTION 0x807u
#define SC32_H_RESTART 0x808u
#define SC32_H_DEACTIVATE 0x809u
#define SC32_H_WINDOW_AT 0x80au
#define SC32_H_OPERATION 0x1000u
typedef struct {
    ScU32 size,version,path,path_bytes,arguments,argument_bytes,flags,reserved;
} Sc32HostLaunch;
typedef struct {
    ScU32 size,version,host_epoch,value,flags,reserved[3];
} Sc32HostControl;
typedef struct {
    ScU32 size,version,host_epoch,after,expected_epoch,rows,capacity,reserved;
} Sc32HostWindows;
#endif
