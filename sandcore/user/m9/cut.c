#include "../SCTEXT.H"
static const char *spec;static int fields,delimiter='\t',only;
static int run(int fd,const char *name)
{
    (void)name;text_reader r={fd,{0},0,0};text_line l={0,0,0,0};int n,result=0;
    while((n=text_line_read(&r,&l))>0){
        if(!fields){for(u32 i=0;i<l.size;i++)if(text_selected(spec,i+1)>0 && cli_write(1,l.bytes+i,1)<0){result=1;break;}}
        else {int contains=0;for(u32 i=0;i<l.size;i++)if((u8)l.bytes[i]==delimiter)contains=1;
            if(!contains){if(!only && text_line_write(1,&l)<0)result=1;if(result)break;continue;}
            u32 start=0,index=1;int wrote=0;for(u32 i=0;i<=l.size;i++)if(i==l.size || (u8)l.bytes[i]==delimiter){
                if(text_selected(spec,index)>0){u8 d=(u8)delimiter;if(wrote && cli_write(1,&d,1)<0)result=1;if(cli_write(1,l.bytes+start,i-start)<0)result=1;wrote=1;}index++;start=i+1;}}
        if(l.newline && cli_text(1,"\n")<0)result=1;if(result)break;
    }text_line_free(&l);return result || n<0;
}
int main(void){if(cli_parse()<0)return 2;int at=0;while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){
    char *o=cli_argv[at++];if(equal(o,"-s"))only=1;else if(equal(o,"-d")){if(at==cli_argc || length(cli_argv[at])!=1)return 2;delimiter=(u8)cli_argv[at++][0];}
    else if(equal(o,"-f")||equal(o,"-b")||equal(o,"-c")){fields=o[1]=='f';if(at==cli_argc)return 2;spec=cli_argv[at++];}else return 2;}
    if(!spec || text_selected(spec,0)<0)return 2;return text_files(at,run);}
