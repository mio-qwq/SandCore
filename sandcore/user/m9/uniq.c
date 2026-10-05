#include "../SCTEXT.H"
static int counted,duplicates,unique;
static int output(text_line *line,u32 count)
{if((duplicates && count==1)||(unique && count!=1))return 0;if(counted){text_unsigned(1,count);cli_text(1," ");}return text_line_write(1,line);}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){
        for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];if(c=='c')counted=1;else if(c=='d')duplicates=1;else if(c=='u')unique=1;else return 2;}at++;}
    if(cli_argc-at>1)return 2;int fd=cli_input(at);if(fd<0)return 1;text_reader r={fd,{0},0,0};text_line previous={0,0,0,0},line={0,0,0,0};u32 count=0;int n,result=0;
    while((n=text_line_read(&r,&line))>0){if(count && !text_compare(previous.bytes,previous.size,line.bytes,line.size,0)){count++;continue;}
        if(count && output(&previous,count)<0){result=1;break;}text_line swap=previous;previous=line;line=swap;count=1;}
    if(!result && n>=0 && count && output(&previous,count)<0)result=1;text_line_free(&line);text_line_free(&previous);if(fd)sc_stream_close(fd,0);return result || n<0;
}
