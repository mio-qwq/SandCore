#ifndef SANDCORE_D2DECODE_H
#define SANDCORE_D2DECODE_H
#include "SCTYPES.H"
typedef struct {
    ScU32 address,length,target,branch,known,readable;
    char bytes[48],text[256];
} D2Instruction;
void D2Decode(const unsigned char *bytes,const unsigned char *readable,ScU32 count,ScU32 address,D2Instruction *out);
#endif
