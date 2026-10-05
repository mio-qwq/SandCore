#include "../SCTEXT.H"
static int run(int fd,const char *name)
{(void)name;text_reader r={fd,{0},0,0};u32 offset=0;u8 bytes[16];int n,c,result=0;
    for(;;){n=0;while(n<16 && (c=text_next(&r))>=0)bytes[n++]=(u8)c;if(!n){if(c!=-256)result=1;break;}
        text_hex(1,offset,8);cli_text(1,"  ");for(int i=0;i<16;i++){if(i<n)text_hex(1,bytes[i],2);else cli_text(1,"  ");cli_text(1,i==7?"  ":" ");}
        cli_text(1," |");for(int i=0;i<n;i++){u8 b=bytes[i]>=32 && bytes[i]<127?bytes[i]:'.';if(cli_write(1,&b,1)<0)result=1;}cli_text(1,"|\n");offset+=(u32)n;if(result || n<16){if(c!=-256)result=1;break;}}
    text_hex(1,offset,8);cli_text(1,"\n");return result;}
int main(void){if(cli_parse()<0)return 2;int at=0;if(cli_argc && equal(cli_argv[0],"-C"))at++;return text_files(at,run);}
