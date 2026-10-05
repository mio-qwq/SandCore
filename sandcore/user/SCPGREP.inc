#include "SCPGREP.H"
#include "SCRE.H"
#include "SCTEXT.H"
static int process_match_main(int kill_mode)
{
    if(cli_parse()<1)return 2;int at=0,exact=0,invert=0,count_only=0,list=0,uid_set=0,uid=0;const char *separator="\n";
    for(;at<cli_argc;at++){const char *arg=cli_argv[at];if(equal(arg,"--")){at++;break;}if(arg[0]!='-' || !arg[1])break;
        if(equal(arg,"-u")){if(++at==cli_argc || cli_integer(cli_argv[at],&uid)<0 || uid<-1)return 2;uid_set=1;}
        else if(equal(arg,"-d")){if(kill_mode || ++at==cli_argc)return 2;separator=cli_argv[at];}
        else for(int j=1;arg[j];j++){if(arg[j]=='x')exact=1;else if(arg[j]=='v')invert=1;else if(arg[j]=='c')count_only=1;
            else if(arg[j]=='l')list=1;else return 2;}}
    if(cli_argc-at!=1)return 2;re_program pattern={0};if(re_program_compile(&pattern,cli_argv[at],1,0)<0)return 2;
    u32 table[520],self[8];if(sc_process_self(self)<0 || sc_processes2(table,520)<0){re_program_free(&pattern);return 1;}int targets[32],used=0;u32 generations[32];
    for(u32 i=1;i<table[1];i++){u32 *row=table+8+i*table[2];if(!row[1] || row[1]==2 || row[0]==self[1] || (uid_set && (int)row[3]!=uid))continue;
        char *name=(char *)(row+8);if(!*name)continue;u32 begin=0,end=0;int match=re_program_search(&pattern,name,(u32)length(name),0,&begin,&end);
        if(exact)match=match && !begin && end==(u32)length(name);if(invert)match=!match;if(match){targets[used]=(int)row[0];generations[used++]=row[2];}}
    re_program_free(&pattern);
    int failures=0;
    for(int i=0;i<used;i++){int pid=targets[i];u32 *row=table+8+pid*table[2];
        if(kill_mode){
            /* 此CLI只请求结束，权限仍由KILL2核对。没有Unix通用信号
             * ABI，不把任意-signal选项默默解释成同一种强制结束。 */
            if(sc_kill_generation(pid,generations[i])<0)failures=1;}
        else if(!count_only){if(i)cli_text(1,separator);cli_number(1,pid);if(list){cli_text(1," ");cli_text(1,(char *)(row+8));}}}
    if(count_only){text_unsigned(1,(u32)used);cli_text(1,"\n");}else if(!kill_mode && used)cli_text(1,"\n");return !used || failures?1:0;
}
