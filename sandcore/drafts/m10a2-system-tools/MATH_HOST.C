#include "CALC2.H"
#include "tools_ui.inc"
#include "calc_engine.inc"
#include <stdint.h>

/* 宿主64位期望仅用于检查真正32位应用算法；不进入三个正式SCX。 */
static ScU32 assertions;
static void require(ScU32 condition,const char *message)
{assertions++;if(!condition){printf("FAIL %s\n",message);exit(1);}}
static void expression(const char *text,ScStatus status,ScI32 value)
{
    CalcResult result=calc_evaluate(text,(ScU32)strlen(text));require(result.status==status,text);
    if(status==0)require(result.bits==(ScU32)value,text);
}
int main(void)
{
    memset(&calc,0,sizeof(calc));
    expression("1+2*3",0,7);expression("(1+2)*3",0,9);expression("7/-2",0,-3);expression("7%-2",0,1);
    expression("-7%2",0,-1);expression("0x7fffffff",0,2147483647);expression("-0x80000000",0,(-2147483647-1));
    expression("-(2147483648)",0,(-2147483647-1));expression("-2147483648+2147483647",0,-1);
    expression("--1 + -(-2)",0,3);expression("0xA*(0xB+1)",0,120);expression("(3-5)*(8/2)",0,-8);
    expression("2147483648",CALC_E_OVERFLOW,0);expression("0xFFFFFFFF",CALC_E_OVERFLOW,0);
    expression("2147483647+1",CALC_E_OVERFLOW,0);expression("-2147483648-1",CALC_E_OVERFLOW,0);
    expression("-2147483648/-1",CALC_E_OVERFLOW,0);expression("-2147483648%-1",CALC_E_OVERFLOW,0);
    expression("--2147483648",CALC_E_OVERFLOW,0);expression("46341*46341",CALC_E_OVERFLOW,0);
    expression("1/0",CALC_E_DIVZERO,0);expression("4%(3-3)",CALC_E_DIVZERO,0);
    expression("0x",CALC_E_SYNTAX,0);expression("1+",CALC_E_SYNTAX,0);expression("(1",CALC_E_SYNTAX,0);
    expression("1 2",CALC_E_SYNTAX,0);expression("",CALC_E_EMPTY,0);
    char nested[200];for(ScU32 i=0;i<70;i++)nested[i]='(';nested[70]='1';for(ScU32 i=0;i<70;i++)nested[71+i]=')';nested[141]=0;
    expression(nested,CALC_E_DEPTH,0);
    ScU32 random=721;
    for(ScU32 i=0;i<3000;i++){
        random=random*1664525u+1013904223u;ScI32 a=(ScI32)random;
        random=random*1664525u+1013904223u;ScI32 b=(ScI32)random;char text[128];
        const char operations[]={'+','-','*','/','%'};
        for(ScU32 op=0;op<5;op++){
            snprintf(text,sizeof(text),"(%d)%c(%d)",a,operations[op],b);CalcResult result=calc_evaluate(text,(ScU32)strlen(text));
            int64_t expected=0;ScStatus error=0;
            if((op==3 || op==4) && !b)error=CALC_E_DIVZERO;
            else if((op==3 || op==4) && a==(-2147483647-1) && b==-1)error=CALC_E_OVERFLOW;
            else {expected=op==0?(int64_t)a+b:op==1?(int64_t)a-b:op==2?(int64_t)a*b:op==3?(int64_t)a/b:(int64_t)a%b;
                if(expected<INT32_MIN || expected>INT32_MAX)error=CALC_E_OVERFLOW;}
            require(result.status==error,"independent int64 arithmetic status");if(!error)require(result.bits==(ScU32)(ScI32)expected,"independent int64 arithmetic result");
        }
        ScU32 value=(ScU32)a,total=(ScU32)b;ScU32 expected=total?(value>=total?100:(ScU32)((uint64_t)value*100/total)):0;
        require(tool_percent(value,total)==expected,"independent uint64 percentage");
    }
    printf("SYSTEM_TOOLS_MATH_HOST assertions=%u arithmetic_pairs=3000 guest_run=false\n",assertions);return 0;
}
