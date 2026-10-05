#include "../SCFILE.H"
static int at,error;
static int protected_path(const char *path,const char *prefix)
{int n=length(prefix);if(length(path)<n)return 0;for(int i=0;i<n;i++){char c=path[i];if(c>='a'&&c<='z')c-=32;if(c!=prefix[i])return 0;}return !path[n] || path[n]=='/';}
static int expression(void);
static int predicate(void)
{
    if(at==cli_argc){error=1;return 0;}char *s=cli_argv[at++];if(equal(s,"!"))return !predicate();
    if(equal(s,"(")){int n=expression();if(at==cli_argc || !equal(cli_argv[at++],")"))error=1;return n;}
    if(equal(s,"-n") || equal(s,"-z")){if(at==cli_argc){error=1;return 0;}int n=*cli_argv[at++]!=0;return s[1]=='n'?n:!n;}
    if(equal(s,"-e")||equal(s,"-f")||equal(s,"-d")||equal(s,"-s")||equal(s,"-r")||equal(s,"-w")){
        if(at==cli_argc){error=1;return 0;}char path[64];u32 meta[8];if(cli_path(cli_argv[at++],path)<0 || sc_fsmeta(path,meta)<0)return 0;
        if(s[1]=='e')return 1;if(s[1]=='f')return meta[1]==1;if(s[1]=='d')return meta[1]==2;if(s[1]=='s')return meta[2]!=0;
        u32 who[8];if(sc_auth_info(who)<0)return 0;
        if(s[1]=='w' && (protected_path(path,"SYS/CORE") || protected_path(path,"SYS/AUTH")))return 0;
        if(s[1]=='w' && protected_path(path,"SYS/MOD") && !who[4])return 0;
        if((int)who[1]<=0)return 1;u32 shift=who[1]==meta[3]?0:who[2]==meta[4]?2:4;return (meta[5]>>shift)&(s[1]=='r'?1:2)?1:0;
    }
    if(at+1<cli_argc){char *op=cli_argv[at];int relation=equal(op,"=")||equal(op,"==")||equal(op,"!=")||equal(op,"-eq")||equal(op,"-ne")||equal(op,"-lt")||equal(op,"-le")||equal(op,"-gt")||equal(op,"-ge");
        if(relation){at++;char *right=cli_argv[at++];if(op[0]!='-')return equal(s,right)^(op[0]=='!');int x,y;if(cli_integer(s,&x)<0 || cli_integer(right,&y)<0){error=1;return 0;}
            return equal(op,"-eq")?x==y:equal(op,"-ne")?x!=y:equal(op,"-lt")?x<y:equal(op,"-le")?x<=y:equal(op,"-gt")?x>y:x>=y;}}
    return *s!=0;
}
static int conjunction(void){int result=predicate();while(at<cli_argc && equal(cli_argv[at],"-a")){at++;int right=predicate();result=result&&right;}return result;}
static int expression(void){int result=conjunction();while(at<cli_argc && equal(cli_argv[at],"-o")){at++;int right=conjunction();result=result||right;}return result;}
int main(void){if(cli_parse()<0)return 2;
#ifdef TEST_BRACKET
    if(!cli_argc || !equal(cli_argv[cli_argc-1],"]"))return 2;cli_argc--;
#endif
    if(!cli_argc)return 1;int n=expression();return error || at!=cli_argc?2:n?0:1;}
