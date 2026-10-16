#ifndef SANDCORE_SC32TERMINALWIRE_H
#define SANDCORE_SC32TERMINALWIRE_H
#include "../SC32TERMINAL.H"
enum {
    SC32_T_SPAWN=0x20c,SC32_T_QUERY=0x20d,SC32_T_RESIZE=0x20e,SC32_T_CONTROL=0x20f
};
typedef struct {
    ScU32 size,version,command,columns,rows,flags,reserved[2];
}
Sc32TerminalSpawnRequest;
typedef struct {
    ScU32 size,version,handle,a,b,reserved[3];
}
Sc32TerminalRequest;
#endif
