#include "SC32API.H"
#include <stdio.h>
/* 只共享声明；另一个.c必须先独立编成SCO。 */
extern int ProjectStep(int value);
extern int ProjectValue;
static int private_value=3;
static int result[4];
int main(void)
{
    result[1]=ProjectStep(private_value);
    printf("SCO project result=%d shared=%d\n",result[1],ProjectValue);
    return result[1]==15 && ProjectValue==7?0:1;
}
