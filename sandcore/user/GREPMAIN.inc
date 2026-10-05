#include "SCRE.H"
static int invert,numbers,counted,quiet,fold,fixed,matched_any,multi;
static const char *pattern;
static int scan(int fd,const char *name)
{
    text_reader reader={fd,{0},0,0};text_line line={0,0,0,0};int n,result=0;u32 number=0,hits=0;
    while((n=text_line_read(&reader,&line))>0){number++;int yes=fixed?text_contains(line.bytes,line.size,pattern,fold):re_matches(line.bytes,line.size);yes^=invert;
        if(!yes)continue;matched_any=1;hits++;if(quiet)break;
        if(!counted){if(multi){cli_text(1,name);cli_text(1,":");}if(numbers){text_unsigned(1,number);cli_text(1,":");}
            if(text_line_write(1,&line)<0){result=2;break;}}}
    if(counted && !quiet){if(multi){cli_text(1,name);cli_text(1,":");}text_unsigned(1,hits);cli_text(1,"\n");}
    text_line_free(&line);return result || n<0?2:0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0,extended=GREP_EXTENDED;fixed=GREP_FIXED;
    while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){
        if(equal(cli_argv[at],"--")){at++;break;}
        if(equal(cli_argv[at],"-e")){if(++at==cli_argc || pattern)return 2;pattern=cli_argv[at++];continue;}
        for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];if(c=='v')invert=1;else if(c=='n')numbers=1;else if(c=='c')counted=1;else if(c=='q')quiet=1;
            else if(c=='i')fold=1;else if(c=='F')fixed=1;else if(c=='E')extended=1;else return 2;}at++;}
    if(!pattern){if(at==cli_argc)return 2;pattern=cli_argv[at++];}if(!fixed && re_compile(pattern,extended,fold)<0)return cli_error("grep: pattern",-1)?2:2;
    multi=cli_argc-at>1;int result=text_files(at,scan);return result?2:matched_any?0:1;
}
