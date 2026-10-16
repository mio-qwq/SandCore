#include "CODEC.H"
#include "../imageapi/SC32IMAGEWIRE.H"
/* 这是旧IMAGE.SCX系统后端唯一私有桥。新普通应用不复制软中断或寄存器ABI；
 * 服务继续使用原启动桥，不把SCL依赖环或公共SCAPI改动带进解码核心。 */
int codec_source_claim(u32 ticket,Sc32ImageSource *out)
{
    Sc32ImageTicket input={sizeof(input),1,ticket,0};
    out->size=sizeof(*out);
    out->version=1;
    return sc_call(0x300,SC32_I_CLAIM,(int)&input,(int)out,0,0,0);
}
int codec_source_read(u32 ticket,u32 offset,void *data,u32 bytes)
{
    Sc32ImageTransfer input={sizeof(input),1,ticket,offset,(u32)data,bytes};
    return sc_call(0x300,SC32_I_READ,(int)&input,0,0,0,0);
}
