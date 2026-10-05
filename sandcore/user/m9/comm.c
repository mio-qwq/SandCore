#include "../SCTEXT.H"
static int omitted[3];
static int emit(int column,text_line *line)
{if(omitted[column])return 0;for(int i=0;i<column;i++)if(!omitted[i] && cli_text(1,"\t")<0)return -1;
    if(cli_write(1,line->bytes,line->size)<0)return -1;return cli_text(1,"\n")<0?-1:0;}
static int next(text_reader *reader,text_line *line,text_line *previous)
{
    if(line->size>previous->capacity){char *fresh=sc_alloc(line->size);if(!fresh)return -4;if(previous->bytes)sc_free(previous->bytes);previous->bytes=fresh;previous->capacity=line->size;}
    for(u32 i=0;i<line->size;i++)previous->bytes[i]=line->bytes[i];previous->size=line->size;
    int n=text_line_read(reader,line);if(n>0 && text_compare(previous->bytes,previous->size,line->bytes,line->size,0)>0)return -2;return n;
}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(p[0]!='-' || !p[1])break;
        for(int j=1;p[j];j++){if(p[j]<'1'||p[j]>'3')return 2;omitted[p[j]-'1']=1;}}
    if(cli_argc-at!=2 || (equal(cli_argv[at],"-") && equal(cli_argv[at+1],"-")))return 2;
    int fd[2]={cli_input(at),cli_input(at+1)},r=0;if(fd[0]<0 || fd[1]<0){r=1;goto done;}
    text_reader readers[2]={{fd[0],{0},0,0},{fd[1],{0},0,0}};text_line lines[2]={{0},{0}},previous[2]={{0},{0}};
    int state[2]={text_line_read(readers,lines),text_line_read(readers+1,lines+1)};
    while(state[0]>0 || state[1]>0){if(state[0]<0 || state[1]<0){r=1;break;}int cmp=!state[0]?1:!state[1]?-1:text_compare(lines[0].bytes,lines[0].size,lines[1].bytes,lines[1].size,0);
        if(emit(cmp<0?0:cmp>0?1:2,lines+(cmp>0))<0){r=1;break;}
        if(cmp<=0)state[0]=next(readers,lines,previous);if(cmp>=0)state[1]=next(readers+1,lines+1,previous+1);sc_yield();}
    if(state[0]<0 || state[1]<0)r=1;for(int i=0;i<2;i++){text_line_free(lines+i);text_line_free(previous+i);}
done:
    for(int i=0;i<2;i++)if(fd[i]>0)sc_stream_close(fd[i],0);if(r)cli_text(2,"comm: read/output/order failure\n");return r;
}
