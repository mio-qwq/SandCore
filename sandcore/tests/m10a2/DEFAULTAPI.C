#include "SC32API.H"
#include <stdio.h>
#include <string.h>

/* 真实default误分类影响了两库未知错误分支；必须在装入新原生库后
 * 调正式API查结果，不能只看反汇编或HOST GCC的本来正确分支。 */
int main(void)
{
    char name[32];ScU32 required=0;
    if(ScErrorName(12345,name,sizeof(name),&required)!=0)return 1;
    if(strcmp(name,"UNKNOWN")!=0)return 2;
    const char *message=strerror(12345);
    if(!message || strcmp(message,"Unknown error")!=0)return 3;
    printf("DEFAULTAPI PASS unknown-system-and-standard-errors\n");
    return 0;
}
