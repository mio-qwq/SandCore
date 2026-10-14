#include "SC32API.H"

/* 插件数据必须真正留在SCL只读段，SCX自己的const不代表页只读。 */
const ScCapabilities readonly_capabilities={sizeof(ScCapabilities),1,0,0,0,0,0,0,0,0,0,0,0,0,{0,0}};
const ScProcessRef readonly_self={sizeof(ScProcessRef),1,0,0};
const char readonly_bytes[]="ROINPUT";

int check_cdecl(int a,int b,int c,int d,int e)
{
#ifdef __GNUC__
    /* SDK宿主库在4B入口上自行建立强对齐，不能要求SCCC碰巧对齐。 */
    volatile int values[16] __attribute__((aligned(16)));
    volatile ScU32 address=(ScU32)values;
    if(address&15u)return -1;
#else
    volatile int values[16];
#endif
    values[0]=a;values[3]=b;values[7]=c;values[11]=d;values[15]=e;
    return values[0]+values[3]+values[7]+values[11]+values[15];
}
