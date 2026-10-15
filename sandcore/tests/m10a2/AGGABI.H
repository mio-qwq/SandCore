#ifndef M10A2_AGGABI_H
#define M10A2_AGGABI_H
#include <SCTYPES.H>
typedef struct {ScU32 lo,hi;} AggPair;
typedef struct {ScI32 x,y,width,height;} AggRect;
typedef struct {ScU32 lane[8];} AggBlock;
typedef struct {unsigned char lead;_Bool truth;unsigned short small;ScU32 value,tail;} AggMixed;
typedef union {AggPair pair;ScU32 words[2];} AggUnion;
typedef AggPair (*AggCallback)(AggPair,ScU32);
AggPair AggPairStep(AggPair,ScU32);
AggRect AggRectStep(ScU32,AggRect,ScU32);
AggBlock AggBlockStep(AggBlock,ScU32);
AggMixed AggMixedStep(AggMixed,ScU32);
AggUnion AggUnionStep(AggUnion,ScU32);
AggPair AggForward(AggCallback,AggPair,ScU32);
AggPair AggVariadic(ScU32,AggPair,...);
ScU32 AggScalar(ScU32,ScU32);
ScU32 AggStackSample(void);
#endif
