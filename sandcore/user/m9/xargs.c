#include "../SCEXEC.H"
#include "../SCTEXT.H"
static text_reader input={0,{0},0,0};static int nul,delimiter=-1,no_empty,verbose,max_args=32,parallel=1,failed,abort_input,seen;
static const char *replacement;static const char *program="echo";static char **initial;static int initial_count,null_input;
static u32 tickets[8];static int jobs;
static void result(int code)
{if(code==255){failed=124;abort_input=1;}else if(code==130){failed=125;abort_input=1;}else if(code && !failed)failed=123;}
static void reap(int wait_one)
{for(;;){for(int i=0;i<jobs;i++){u32 state[8];int n=sc_wait2(tickets[i],state);if(n<=0){if(n<0)result(1);else result((int)state[2]);tickets[i--]=tickets[--jobs];if(wait_one)return;}}
    if(!wait_one || !jobs)return;sc_yield();}}
static int launch(char **args,int count)
{char command[1024];if(exec_command(command,program,args,count)<0)return -1;if(verbose){cli_text(2,command);cli_text(2,"\n");}
    if(jobs==parallel)reap(1);if(abort_input)return 0;int fds[3]={null_input,1,2};int job=sc_spawn2(command,fds,0);
    if(job<0){failed=job==-2?127:126;abort_input=1;return -1;}tickets[jobs++]=(u32)job;reap(0);seen=1;return 0;}
static int next_word(char out[1024])
{
    int used=0,started=0;char quote=0;for(;;){int value=text_next(&input);if(value<0){if(value!=-256 || quote)return -1;out[used]=0;return started?1:0;}
        char c=(char)value;if(nul || delimiter>=0){if(value==(nul?0:delimiter)){out[used]=0;return 1;}started=1;}
        else {if(!quote && (c==' '||c=='\t'||c=='\n'||c=='\r')){if(!started)continue;out[used]=0;return 1;}
            started=1;if(c=='\''||c=='"'){if(!quote){quote=c;continue;}if(quote==c){quote=0;continue;}}
            if(c=='\\' && quote!='\''){value=text_next(&input);if(value<0)return -1;c=(char)value;}
            if(!c)return -1;}
        if(used==1023 || !c)return -1;out[used++]=c;
    }
}
static int substitute(const char *text,const char *item,char out[1024])
{int n=length(replacement),used=0;for(int i=0;text[i];){int same=1;for(int j=0;j<n;j++)if(!text[i+j] || text[i+j]!=replacement[j]){same=0;break;}
    if(same){int bytes=length(item);if(used+bytes>=1024)return -1;for(int j=0;j<bytes;j++)out[used++]=item[j];i+=n;}else {if(used==1023)return -1;out[used++]=text[i++];}}
    out[used]=0;return 0;}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;for(;at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1];at++){
        char *option=cli_argv[at];if(equal(option,"--")){at++;break;}if(equal(option,"-0"))nul=1;else if(equal(option,"-r"))no_empty=1;else if(equal(option,"-t"))verbose=1;
        else if(equal(option,"-n")||equal(option,"-P")||equal(option,"-I")||equal(option,"-d")){
            if(++at==cli_argc)return 2;if(option[1]=='I'){replacement=cli_argv[at];if(!*replacement)return 2;}
            else if(option[1]=='d'){if(length(cli_argv[at])!=1)return 2;delimiter=(u8)cli_argv[at][0];}
            else {int n;if(cli_integer(cli_argv[at],&n)<0 || n<1 || n>(option[1]=='n'?32:8))return 2;if(option[1]=='n')max_args=n;else parallel=n;}}
        else return 2;
    }
    if(at<cli_argc){program=cli_argv[at++];initial=cli_argv+at;initial_count=cli_argc-at;}if(initial_count>=32)return 2;if(max_args>32-initial_count)max_args=32-initial_count;
    null_input=sc_stream_open("/DEV/NULL",4,0);if(null_input<0)return 1;char item[1024],*args[32];int r=0;
    if(replacement){text_line line={0,0,0,0};char *arena=sc_alloc(32768);if(!arena){sc_stream_close(null_input,0);return 1;}
        while(!abort_input && (r=text_line_read(&input,&line))>0){if(!line.size)continue;if(line.size>1023){r=-1;break;}
            for(u32 i=0;i<line.size;i++){if(!line.bytes[i]){r=-1;break;}item[i]=line.bytes[i];}if(r<0)break;item[line.size]=0;
            for(int i=0;i<initial_count;i++){args[i]=arena+i*1024;if(substitute(initial[i],item,args[i])<0){r=-1;break;}}if(r<0 || launch(args,initial_count)<0){r=-1;break;}}
        sc_free(arena);text_line_free(&line);
    }else {char arena[2048];int used=0,count=0;for(int i=0;i<initial_count;i++)args[i]=initial[i];
        while(!abort_input && (r=next_word(item))>0){int n=length(item);if(n+1>2048-used || count==max_args){if(!count || launch(args,initial_count+count)<0){r=-1;break;}used=count=0;if(abort_input)break;}
            if(n+1>2048){r=-1;break;}args[initial_count+count++]=arena+used;copy(arena+used,item,n+1);used+=n+1;
            char command[1024];if(exec_command(command,program,args,initial_count+count)<0){count--;
                if(!count || launch(args,initial_count+count)<0){r=-1;break;}used=count=0;args[initial_count+count++]=arena;copy(arena,item,n+1);used=n+1;
                if(exec_command(command,program,args,initial_count+count)<0){r=-1;break;}}
        }
        if(r>=0 && !abort_input && (count || (!seen && !no_empty)))if(launch(args,initial_count+count)<0)r=-1;
    }
    while(jobs)reap(1);sc_stream_close(null_input,0);return failed?failed:r<0?1:0;
}
