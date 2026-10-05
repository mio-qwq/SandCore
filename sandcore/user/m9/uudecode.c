#include "../SCUU.H"
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;const char *override=0;if(cli_argc && equal(cli_argv[0],"-o")){if(cli_argc<2)return 2;override=cli_argv[1];at=2;}
    if(cli_argc-at>1)return 2;int fd=cli_input(at);if(fd<0)return 1;text_reader reader={fd,{0},0,0};text_line line={0};int n,out=-1,result=1,base64=0,ended=0,zero=0,padded=0;
    u32 total=0,limit=16777216;char setting[256];if(sc_user(4,"DECODE_LIMIT",setting,256)>=0 && (text_number(setting,&limit)<0 || !limit || limit>67108864))goto done;
    while((n=text_line_read(&reader,&line))>0){if(line.size && line.bytes[line.size-1]=='\r')line.size--;u32 prefix=0;
        if(line.size>=6 && !text_compare(line.bytes,6,"begin ",6,0))prefix=6;
        else if(line.size>=13 && !text_compare(line.bytes,13,"begin-base64 ",13,0)){base64=1;prefix=13;}if(!prefix)continue;
        if(line.size<prefix+5 || line.bytes[prefix+3]!=' ')goto done;for(int i=0;i<3;i++)if(line.bytes[prefix+i]<'0' || line.bytes[prefix+i]>'7')goto done;
        u32 bytes=line.size-prefix-4;if(!bytes || bytes>63)goto done;char name[64];for(u32 i=0;i<bytes;i++)name[i]=line.bytes[prefix+4+i];name[bytes]=0;
        if(!override && !uu_safe_name(name))goto done;out=sc_stream_open(override?override:name,2,limit);if(out<0)goto done;break;}
    if(out<0)goto done;
    while((n=text_line_read(&reader,&line))>0){if(line.size && line.bytes[line.size-1]=='\r')line.size--;u8 bytes[49152];u32 used=0;
        if(base64){if(line.size==4 && !text_compare(line.bytes,4,"====",4,0)){ended=1;break;}if(padded || !line.size || (line.size&3))goto done;
            for(u32 i=0;i<line.size;i+=4){int a=uu_b64_value(line.bytes[i]),b=uu_b64_value(line.bytes[i+1]),c=uu_b64_value(line.bytes[i+2]),d=uu_b64_value(line.bytes[i+3]);
                int pc=line.bytes[i+2]=='=',pd=line.bytes[i+3]=='=';if(a<0 || b<0 || (!pc && c<0) || (!pd && d<0) || (pc && !pd) || ((pc || pd) && i+4!=line.size))goto done;
                if((pc && (b&15)) || (!pc && pd && (c&3)))goto done;bytes[used++]=(u8)((a<<2)|(b>>4));if(!pc)bytes[used++]=(u8)((b<<4)|(c>>2));if(!pd)bytes[used++]=(u8)((c<<6)|d);if(pc || pd)padded=1;}}
        else {if(zero){if(line.size!=3 || text_compare(line.bytes,3,"end",3,0))goto done;ended=1;break;}if(!line.size)goto done;
            u8 first=(u8)line.bytes[0];if(first<32 || first>96)goto done;u32 count=(first-32)&63;if(count>45 || line.size!=1+((count+2)/3)*4)goto done;
            if(!count){zero=1;continue;}for(u32 i=1;i<line.size;i+=4){u32 group=0;for(int j=0;j<4;j++){u8 c=(u8)line.bytes[i+j];if(c<32 || c>96)goto done;group=(group<<6)|((c-32)&63);}
                for(int j=0;j<3 && used<count;j++)bytes[used++]=(u8)(group>>(16-j*8));}}
        if(used>limit-total || cli_write(out,bytes,used)<0)goto done;total+=used;sc_yield();}
    if(!ended || sc_stream_close(out,1)<0)goto done;out=-1;result=0;
done:
    if(out>=0)sc_stream_close(out,0);text_line_free(&line);if(fd)sc_stream_close(fd,0);if(result)cli_text(2,"uudecode: invalid/input/output failure; target preserved\n");return result;
}
