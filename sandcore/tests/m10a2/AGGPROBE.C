#include "AGGABI.H"
#include <stdio.h>
static ScU32 checks,failures;
static void check(int okay,const char *name)
{
    checks++;
    if(!okay){failures++;printf("AGGABI FAIL %s\n",name);}
}
static AggPair callback(AggPair input,ScU32 tag)
{
    input.lo+=tag*3u;
    input.hi^=tag+4u;
    return input;
}
int main(void)
{
    check(sizeof(AggPair)==8 && sizeof(AggRect)==16 && sizeof(AggBlock)==32
        && sizeof(AggMixed)==12 && sizeof(AggUnion)==8,"layout");
    ScU32 stack_before=AggStackSample();
    AggPair pair={10u,0x12345678u};
    AggPair first=AggPairStep(pair,7u);
    check(first.lo==17u && first.hi==(0x12345678u^7u),"pair-return");
    check(pair.lo==10u && pair.hi==0x12345678u,"by-value-input-isolation");
    AggPair nested=AggPairStep(AggPairStep(pair,7u),9u);
    check(nested.lo==26u && nested.hi==(0x12345678u^7u^9u),"nested-return-slots");
    AggRect rect={-5,9,100,70};
    AggRect changed=AggRectStep(3u,rect,4u);
    check(changed.x==-2 && changed.y==5 && changed.width==107 && changed.height==63,"adjacent-scalars-and-rect");
    check(AggRectStep(3u,rect,4u).width==107,"return-member");
    check(rect.x==-5 && rect.y==9 && rect.width==100 && rect.height==70,"rect-input-isolation");
    AggBlock block={{0u,1u,2u,3u,4u,5u,6u,7u}};
    AggBlock result=AggBlockStep(block,11u);
    for(ScU32 i=0;i<8;i++)check(result.lane[i]==11u+i*2u && block.lane[i]==i,"large-return-and-isolation");
    AggMixed mixed={0x51u,1,32767u,100u,0x1234u};
    AggMixed narrow=AggMixedStep(mixed,5u);
    check(narrow.lead==0x52u && !narrow.truth && narrow.small==32768u
        && narrow.value==105u && narrow.tail==(0x1234u^5u),"mixed-one-two-four-byte-fields");
    check(mixed.lead==0x51u && mixed.truth && mixed.small==32767u && mixed.value==100u,"mixed-input-isolation");
    AggUnion united;united.words[0]=10u;united.words[1]=0x87654321u;
    AggUnion union_result=AggUnionStep(united,13u);
    check(union_result.pair.lo==23u && union_result.pair.hi==(0x87654321u^13u),"union-return");
    check(united.words[0]==10u && united.words[1]==0x87654321u,"union-input-isolation");
    AggPair forwarded=AggForward(callback,pair,5u);
    check(forwarded.lo==27u && forwarded.hi==(0x12345678u^9u^2u),"cross-toolchain-callback-and-conditional-return");
    AggPair even=AggForward(callback,pair,4u);
    check(even.lo==22u && even.hi==(0x12345678u^8u),"alternate-aggregate-return");
    AggPair varied=AggVariadic(2u,pair,3u,5u);
    check(varied.lo==18u && varied.hi==(0x12345678u^3u^5u),"last-named-aggregate-va-start");
    for(ScU32 i=0;i<32;i++){
        AggPair cycle=AggPairStep(pair,i);
        check(cycle.lo==10u+i && cycle.hi==(0x12345678u^i)
            && AggScalar(i,3u)==i*17u+3u,"repeated-sret-stack-and-next-scalar-call");
    }
    /* 两次都以相同局部初始化路径采样，不能混入check已压入的右侧参数。 */
    ScU32 stack_after=AggStackSample();
    printf("AGGABI STACK before=%08x after=%08x delta=%d\n",stack_before,stack_after,(ScI32)(stack_after-stack_before));
    check(stack_before==stack_after,"exact-caller-stack-balance");
    printf("AGGABI %s checks=%u failures=%u\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
