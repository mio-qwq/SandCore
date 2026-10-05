#include "../SCIO.H"
int main(void){if(cli_parse()<0)return 2;int at=0;while(at<cli_argc){char *p=cli_argv[at];while(*p && *p!='=')p++;if(!*p)break;*p++=0;if(sc_env_set(cli_argv[at++],p,0)<0)return 1;}
    if(at==cli_argc){char text[8192];int n=sc_env_list(text,sizeof(text));return n<0?1:cli_write(1,text,(u32)n)<0;}
    char command[1024];command[0]=0;for(;at<cli_argc;at++){if(length(command)+length(cli_argv[at])*2+4>=1024)return 2;if(*command)append(command," ",1024);
        if(!*command)append(command,cli_argv[at],1024);else {append(command,"\"",1024);for(char *p=cli_argv[at];*p;p++){char s[3]={0,0,0};if(*p=='"'||*p=='\\'){s[0]='\\';s[1]=*p;}else s[0]=*p;append(command,s,1024);}append(command,"\"",1024);}}
    int fds[3]={0,1,2},job=sc_spawn2(command,fds,0);if(job<0)return 1;u32 info[8];int r;while((r=sc_wait2((u32)job,info))>0)sc_yield();return r<0?1:(int)info[2];}
