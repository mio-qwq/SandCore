#include "../SCTEXT.H"
static u8 delimiters[256];static u32 delimiter_count=1;static int serial_mode;
static int paste_delimiters(const char *text)
{
    delimiter_count=0;while(*text){u8 c=(u8)*text++;if(c=='\\'){if(!*text)return -1;c=(u8)*text++;if(c=='0')c=0;else if(c=='t')c='\t';else if(c=='n')c='\n';else if(c=='r')c='\r';else if(c!='\\')return -1;}
        if(delimiter_count==sizeof(delimiters))return -1;delimiters[delimiter_count++]=c;}if(!delimiter_count){delimiters[0]=0;delimiter_count=1;}return 0;
}
static int paste_delimiter(u32 index)
{u8 c=delimiters[index%delimiter_count];return c?cli_write(1,&c,1)<0?-1:0:0;}
int main(void)
{
    if(cli_parse()<0)return 2;delimiters[0]='\t';int at=0;
    for(;at<cli_argc;at++){const char *option=cli_argv[at];if(equal(option,"--")){at++;break;}if(option[0]!='-' || !option[1])break;
        if(equal(option,"-s")){serial_mode=1;continue;}if(option[1]=='d'){const char *text=option+2;if(!*text){if(++at==cli_argc)return 2;text=cli_argv[at];}if(paste_delimiters(text)<0)return 2;}else return 2;}
    int count=cli_argc-at;if(!count)count=1;if(count>12)return 2;
    text_reader *readers=sc_alloc((u32)count*sizeof(text_reader));text_line *lines=sc_alloc((u32)count*sizeof(text_line));
    if(!readers || !lines){if(readers)sc_free(readers);if(lines)sc_free(lines);return 1;}int opened=0,result=0,stdin_reader=-1;
    for(int i=0;i<count;i++){int fd=at+i<cli_argc?cli_input(at+i):0;if(fd<0){result=cli_error("paste: input",fd);goto done;}
        readers[i]=(text_reader){fd,{0},0,0};lines[i]=(text_line){0,0,0,0};if(!fd && stdin_reader<0)stdin_reader=i;opened++;}
    /* 所有'-'共享同一个缓冲读游标。各列单独预读4096B会把stdin的
     * 后续行藏在第一列的缓冲里，导致paste - -错误地输出空第二列。 */
    if(serial_mode){for(int i=0;i<count;i++){text_reader *r=readers[i].fd?&readers[i]:&readers[stdin_reader];u32 index=0;int n;
            while((n=text_line_read(r,&lines[i]))>0){if(index && paste_delimiter(index-1)<0){result=1;goto done;}
                if(cli_write(1,lines[i].bytes,lines[i].size)<0){result=1;goto done;}index++;}
            if(n<0 || cli_text(1,"\n")<0){result=1;goto done;}}}
    else for(;;){int any=0;for(int i=0;i<count;i++){text_reader *r=readers[i].fd?&readers[i]:&readers[stdin_reader];int n=text_line_read(r,&lines[i]);if(n<0){result=1;goto done;}if(n)any=1;}if(!any)break;
        for(int i=0;i<count;i++){if(i && paste_delimiter((u32)i-1)<0){result=1;goto done;}if(cli_write(1,lines[i].bytes,lines[i].size)<0){result=1;goto done;}}
        if(cli_text(1,"\n")<0){result=1;break;}}
done:
    for(int i=0;i<opened;i++){text_line_free(&lines[i]);if(readers[i].fd)sc_stream_close(readers[i].fd,0);}sc_free(lines);sc_free(readers);return result;
}
