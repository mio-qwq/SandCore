#include "SCKERNEL.H"
/* 借服务表真实分配/释放并写标记，指针/BSS需要SCCC生成重定位。 */
static int count;
int skm_main(sc_kernel_api *api)
{
    if(api->version!=1 || api->size<56)return 9;count++;char *memory=api->allocate(4096);if(!memory)return 8;
    memory[0]='0'+count;memory[1]='\n';int result=api->write("/TMP/M9RING0",(u8 *)memory,2);api->release(memory,4096);return result==2?0:7;
}
