#include "../SCTEXT.H"
static u32 minimum=4;static int offsets;
static int run(int fd,const char *name)
{
    (void)name;char *line=sc_alloc(65536);if(!line)return 1;u32 size=0,position=0,start=0;int overflowing=0,result=0,c;text_reader r={fd,{0},0,0};
    while((c=text_next(&r))>=0 || c==-256){
        if(c>=32 && c<=126){if(!size)start=position;if(size==65536){if(!overflowing){if(offsets){text_hex(1,start,8);cli_text(1," ");}if(cli_write(1,line,size)<0){result=1;break;}overflowing=1;}
                u8 byte=(u8)c;if(cli_write(1,&byte,1)<0){result=1;break;}}
            else line[size++]=(char)c;}
        else {if(size>=minimum){if(!overflowing){if(offsets){text_hex(1,start,8);cli_text(1," ");}if(cli_write(1,line,size)<0){result=1;break;}}cli_text(1,"\n");}size=0;overflowing=0;}
        if(c==-256)break;position++;
    }sc_free(line);return result || (c<0 && c!=-256);
}
int main(void){if(cli_parse()<0)return 2;int at=0;while(at<cli_argc){if(equal(cli_argv[at],"-n")){if(++at==cli_argc || text_number(cli_argv[at++],&minimum)<0 || !minimum || minimum>65536)return 2;}
    else if(equal(cli_argv[at],"-t")){if(++at==cli_argc || !equal(cli_argv[at++],"x"))return 2;offsets=1;}else break;}return text_files(at,run);}
