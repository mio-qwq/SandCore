#ifndef SCLAUNCH_H
#define SCLAUNCH_H
#include "SCIO.H"
/* CLI间传参必须重新转义，不能把用户文字拼成另一层Shell代码。
 * 内核只解析程序名与参数，不执行元字符；权限/环境由spawn检查。 */
static int launch_argument(char out[1024],int *used,const char *word,int first)
{
    int n=length(word);if(first){if(!n)return -1;for(int i=0;i<n;i++)if((u8)word[i]<=32 || word[i]=='"' || word[i]=='\'')return -1;
        if(n>=1024)return -1;copy(out,word,1024);*used=n;return 0;}
    if(*used+3>=1024)return -1;out[(*used)++]=' ';out[(*used)++]='"';
    for(int i=0;i<n;i++){char c=word[i];if(c=='"' || c=='\\'){if(*used>=1022)return -1;out[(*used)++]='\\';}if(*used>=1022)return -1;out[(*used)++]=c;}
    out[(*used)++]='"';out[*used]=0;return 0;
}
static int launch_words(int first,int fds[3])
{char text[1024];int used=0;for(int i=first;i<cli_argc;i++)if(launch_argument(text,&used,cli_argv[i],i==first)<0)return -1;return sc_spawn2(text,fds,0);}
static int launch_wait(u32 job)
{u32 row[8];int r;while((r=sc_wait2(job,row))>0)sc_yield();return r<0?1:(int)row[2];}
#endif
