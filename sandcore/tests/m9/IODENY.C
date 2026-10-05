#include "SCIO.H"
static void fault(u32 *context)
{
    cli_text(1,context[12]==13?"PASS UART GP\n":"FAIL UART unexpected exception\n");sc_call(0,context[12]==13?0:1,0,0,0,0,0);for(;;)sc_yield();
}
int main(void)
{
    cli_text(1,"BEFORE UART IO\n");
    if(sc_call(9,13,(int)fault,0,0,0,0)<0)return 2;
    /* 真正的三环OUT应触发GP并结束本任务；后面的正文不得执行。 */
    __asm__ __volatile__("mov $0x3f8,%%edx; mov $0x41,%%eax; outb %%al,%%dx":::"eax","edx","memory");
    cli_text(1,"FAIL UART IO executed\n");return 99;
}
