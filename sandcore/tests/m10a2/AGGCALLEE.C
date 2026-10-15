#include "AGGABI.H"
#include <stdarg.h>
AggPair AggPairStep(AggPair input,ScU32 tag)
{
    input.lo+=tag;
    input.hi^=tag;
    return input;
}
AggRect AggRectStep(ScU32 before,AggRect input,ScU32 after)
{
    input.x+=(ScI32)before;
    input.y-=(ScI32)after;
    input.width+=(ScI32)(before+after);
    input.height-=(ScI32)(before^after);
    return input;
}
AggBlock AggBlockStep(AggBlock input,ScU32 tag)
{
    for(ScU32 i=0;i<8;i++)input.lane[i]+=tag+i;
    return input;
}
AggMixed AggMixedStep(AggMixed input,ScU32 tag)
{
    input.lead++;
    input.truth=!input.truth;
    input.small++;
    input.value+=tag;
    input.tail^=tag;
    return input;
}
AggUnion AggUnionStep(AggUnion input,ScU32 tag)
{
    input.words[0]+=tag;
    input.words[1]^=tag;
    return input;
}
AggPair AggForward(AggCallback callback,AggPair input,ScU32 tag)
{
    /* caller/callee工具链交叉时，回调也必须遵循同一hidden-sret栈合同。 */
    AggPair candidate=callback(input,tag);
    return tag&1u?AggPairStep(candidate,2u):candidate;
}
ScU32 AggScalar(ScU32 before,ScU32 after)
{
    return before*17u+after;
}
AggPair AggVariadic(ScU32 count,AggPair input,...)
{
    /* 最后一个named参数占8B，va_start不能仍把它当成一个4B槽。 */
    va_list arguments;va_start(arguments,input);
    for(ScU32 i=0;i<count;i++){
        ScU32 value=va_arg(arguments,ScU32);
        input.lo+=value;input.hi^=value;
    }
    va_end(arguments);
    return input;
}
ScU32 AggStackSample(void)
{
    /* 只比较同一函数两次的栈位置，不把生命周期已结束的局部地址解引用。 */
    ScU32 local=0;
    return (ScU32)&local;
}
