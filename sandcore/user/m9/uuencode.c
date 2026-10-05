#include "../SCUU.H"
int main(void)
{
    if(cli_parse()<0)return 2;int at=0,base64=0;if(cli_argc && equal(cli_argv[0],"-m")){base64=1;at++;}if(cli_argc-at<1 || cli_argc-at>2)return 2;
    const char *name=cli_argv[cli_argc-1];if(!uu_safe_name(name))return 2;int fd=cli_argc-at==2?cli_input(at):0;if(fd<0)return 1;
    int result=0;if(cli_text(1,base64?"begin-base64 644 ":"begin 644 ")<0 || cli_text(1,name)<0 || cli_text(1,"\n")<0){result=1;goto done;}
    text_reader reader={fd,{0},0,0};int c=-256;
    for(;;){u8 input[45];int n=0;while(n<45 && (c=text_next(&reader))>=0)input[n++]=(u8)c;if(!n)break;char out[64];int used=0;
        if(!base64)out[used++]=uu_char((u32)n);for(int i=0;i<n;i+=3){u32 left=(u32)(n-i),value=(u32)input[i]<<16;if(left>1)value|=(u32)input[i+1]<<8;if(left>2)value|=input[i+2];
            if(base64){out[used++]=uu_base64[(value>>18)&63];out[used++]=uu_base64[(value>>12)&63];out[used++]=left>1?uu_base64[(value>>6)&63]:'=';out[used++]=left>2?uu_base64[value&63]:'=';}
            else {out[used++]=uu_char(value>>18);out[used++]=uu_char(value>>12);out[used++]=uu_char(value>>6);out[used++]=uu_char(value);}}
        out[used++]='\n';if(cli_write(1,out,(u32)used)<0){result=1;break;}if(n<45)break;sc_yield();}
    if(c<0 && c!=-256)result=1;if(!result && cli_text(1,base64?"====\n":"`\nend\n")<0)result=1;
done:
    if(fd)sc_stream_close(fd,0);return result;
}
