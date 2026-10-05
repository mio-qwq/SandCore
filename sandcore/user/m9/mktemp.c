#include "../SCIO.H"
int main(void)
{
    if(cli_parse()<0)return 2;int directory=0,quiet=0,at=0,temporary=0;const char *parent=0,*name=0;
    for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(equal(p,"-d"))directory=1;else if(equal(p,"-q"))quiet=1;
        else if(equal(p,"-t"))temporary=1;else if(equal(p,"-p")){if(++at==cli_argc)return 2;parent=cli_argv[at];}else if(p[0]=='-'&&p[1])return 2;else break;}
    if(cli_argc-at>1)return 2;name=at<cli_argc?cli_argv[at]:"tmp.XXXXXXXX";char environment[256];if(temporary || parent || at==cli_argc){
        if(!parent){parent=sc_user(4,"TMPDIR",environment,sizeof(environment))>0?environment:"/tmp";}for(int i=0;name[i];i++)if(name[i]=='/')return 2;}
    char candidate[64];int n=0;if(parent){for(int i=0;parent[i];i++){if(n==62)return 2;candidate[n++]=parent[i];}if(n && candidate[n-1]!='/')candidate[n++]='/';}
    for(int i=0;name[i];i++){if(n==63)return 2;candidate[n++]=name[i];}candidate[n]=0;int start=n;while(start && candidate[start-1]=='X')start--;
    if(n-start<6 || n-start>16)return 2;u32 self[8];if(sc_process_self(self)<0)return 1;u32 random=sc_tick()^(self[1]*0x9e3779b9u)^self[2];if(!random)random=0x8a5cd789u;
    const char *digits="abcdefghijklmnopqrstuvwxyz0123456789";for(int attempt=0;attempt<256;attempt++){for(int i=start;i<n;i++){random^=random<<13;random^=random>>17;random^=random<<5;candidate[i]=digits[random%36];}
        int result=sc_create_ex(candidate,directory?2:1,3);if(result==0){if(cli_text(1,candidate)<0 || cli_text(1,"\n")<0)return 1;return 0;}
        if(result!=-8 && result!=-6){if(!quiet)cli_error("mktemp",result);return 1;}sc_yield();}
    if(!quiet)cli_text(2,"mktemp: exhausted exclusive names\n");return 1;
}
