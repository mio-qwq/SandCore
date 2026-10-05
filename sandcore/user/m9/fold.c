#include "../SCTEXT.H"
static u32 width=80;static int byte_mode,spaces;
static u32 scalar_bytes(const char *s,u32 left,u32 *columns)
{
    u8 c=(u8)*s;u32 n=c<128?1:c>=0xC2&&c<=0xDF?2:c>=0xE0&&c<=0xEF?3:c>=0xF0&&c<=0xF4?4:1;
    if(n>left)n=1;u32 value=n==1?c:c&((1u<<(7-n))-1);for(u32 i=1;i<n;i++){u8 next=(u8)s[i];if((next&0xC0)!=0x80){n=1;value=c;break;}value=(value<<6)|(next&63);}
    *columns=value>=0x2E80?2:1;return n;
}
static int run(int fd,const char *name)
{
    (void)name;text_reader reader={fd,{0},0,0};text_line line={0};int n,r=0;
    while((n=text_line_read(&reader,&line))>0){u32 begin=0;while(begin<line.size){u32 at=begin,column=0,blank=0;
            while(at<line.size){u32 delta=1,take=byte_mode?1:scalar_bytes(line.bytes+at,line.size-at,&delta);u8 c=(u8)line.bytes[at];u32 next=column+delta;
                if(!byte_mode){if(c=='\t')next=(column+8)&~7u;else if(c=='\b')next=column?column-1:0;else if(c=='\r')next=0;}
                if(next>width && at>begin)break;column=next;at+=take;if(c==' ' || c=='\t')blank=at;}
            if(at<line.size && spaces && blank>begin)at=blank;
            if(cli_write(1,line.bytes+begin,at-begin)<0){r=1;break;}if(at<line.size && cli_text(1,"\n")<0){r=1;break;}begin=at;}
        if(r)break;if(line.newline && cli_text(1,"\n")<0){r=1;break;}sc_yield();}
    text_line_free(&line);return r || n<0;
}
int main(void)
{if(cli_parse()<0)return 2;int at=0;for(;at<cli_argc;at++){char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(p[0]!='-' || !p[1])break;
    if(equal(p,"-w")){if(++at==cli_argc || text_number(cli_argv[at],&width)<0 || !width || width>65536)return 2;}
    else for(int j=1;p[j];j++){if(p[j]=='b')byte_mode=1;else if(p[j]=='s')spaces=1;else return 2;}}return text_files(at,run);}
