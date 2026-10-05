#include "../SCTEXT.H"
static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int decode_value(int c)
{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;return c=='+'?62:c=='/'?63:c=='='?64:-1;}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0,decode=0;u32 wrap=76;
    while(at<cli_argc){if(equal(cli_argv[at],"-d")){decode=1;at++;}else if(equal(cli_argv[at],"-w")){if(++at==cli_argc || text_number(cli_argv[at++],&wrap)<0 || wrap>65536)return 2;}else break;}
    if(cli_argc-at>1)return 2;int fd=cli_input(at);if(fd<0)return 1;text_reader r={fd,{0},0,0};int result=0,c;
    if(decode){int group[4],count=0,ended=0;while((c=text_next(&r))>=0){if(c==' '||c=='\n'||c=='\r'||c=='\t')continue;int v=decode_value(c);if(v<0 || ended){result=1;break;}group[count++]=v;
            if(count==4){if(group[0]==64 || group[1]==64 || (group[2]==64 && group[3]!=64) || (group[2]==64 && (group[1]&15)) || (group[3]==64 && group[2]!=64 && (group[2]&3))){result=1;break;}
                u8 bytes[3]={(u8)((group[0]<<2)|(group[1]>>4)),(u8)((group[1]<<4)|(group[2]>>2)),(u8)((group[2]<<6)|group[3])};
                u32 n=group[2]==64?1:group[3]==64?2:3;if(cli_write(1,bytes,n)<0){result=1;break;}ended=n!=3;count=0;}}
        if(count)result=1;
    }else {u8 data[3];u32 column=0;for(;;){int n=0;while(n<3 && (c=text_next(&r))>=0)data[n++]=(u8)c;if(!n)break;
            u8 encoded[4]={(u8)alphabet[data[0]>>2],(u8)alphabet[((data[0]&3)<<4)|(n>1?data[1]>>4:0)],n>1?(u8)alphabet[((data[1]&15)<<2)|(n>2?data[2]>>6:0)]:'=',n>2?(u8)alphabet[data[2]&63]:'='};
            for(int i=0;i<4;i++){if(cli_write(1,encoded+i,1)<0){result=1;break;}column++;if(wrap && column==wrap){if(cli_text(1,"\n")<0)result=1;column=0;}}if(result || n<3)break;}
        if(wrap && column && cli_text(1,"\n")<0)result=1;}
    if(c!=-256 && c<0)result=1;if(fd)sc_stream_close(fd,0);return result;
}
