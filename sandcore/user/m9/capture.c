#include "../SCIO.H"
static void word(u8 *p,u32 value){for(int i=0;i<4;i++)p[i]=(u8)(value>>(i*8));}
int main(void)
{
    if(cli_parse()<0)return 2;
    if(!cli_argc){char list[4096];int n=sc_capture_list(list,sizeof(list));return n<0?cli_error("capture",n):cli_write(1,list,(u32)n)<0?1:0;}
    int handle;if(cli_argc!=2 || cli_integer(cli_argv[0],&handle)<0)return 2;
    u32 info[8];int token=sc_capture_open(handle,info);if(token<0)return cli_error("capture",token);
    int fd=sc_stream_open(cli_argv[1],2,info[6]+122);if(fd<0){sc_capture_close((u32)token);return cli_error("capture",fd);}
    /* BMP V4原生BGRA/top-down保留透明度；不扩大尺寸，不依赖宿主截图。 */
    u8 header[122];for(int i=0;i<122;i++)header[i]=0;header[0]='B';header[1]='M';word(header+2,info[6]+122);word(header+10,122);
    word(header+14,108);word(header+18,info[2]);word(header+22,0u-info[3]);header[26]=1;header[28]=32;
    word(header+30,3);word(header+34,info[6]);word(header+54,0x00FF0000u);word(header+58,0x0000FF00u);
    word(header+62,0x000000FFu);word(header+66,0xFF000000u);word(header+70,0x73524742u);
    int result=cli_write(fd,header,sizeof(header));u8 buffer[4096];u32 offset=0;
    while(result>=0 && offset<info[6]){result=sc_capture_read((u32)token,offset,buffer,sizeof(buffer));
        if(result<=0){result=-1;break;}offset+=(u32)result;result=cli_write(fd,buffer,(u32)result);}
    sc_capture_close((u32)token);if(result>=0)result=sc_stream_close(fd,1);else sc_stream_close(fd,0);
    return result<0?cli_error("capture",result):0;
}
