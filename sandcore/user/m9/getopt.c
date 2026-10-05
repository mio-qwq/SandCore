#include "../SCIO.H"
typedef struct {char name[64];int argument;} option_name;
static option_name longs[32];static int long_count,quiet,silent,alternative,output_used;static char output[4096];
static int emit(const char *word)
{
    int n=length(word);if(output_used && output_used==4095)return -1;if(output_used)output[output_used++]=' ';
    if(output_used==4095)return -1;output[output_used++]='\'';
    for(int i=0;i<n;i++){char c=word[i];if(c=='\''){if(output_used+4>=4096)return -1;output[output_used++]='\'';output[output_used++]='\\';output[output_used++]='\'';output[output_used++]='\'';}
        else {if(output_used==4095)return -1;output[output_used++]=c;}}
    if(output_used==4095)return -1;output[output_used++]='\'';output[output_used]=0;return 0;
}
static int long_list(const char *p)
{
    while(*p){if(long_count==32)return -1;option_name *o=longs+long_count;int n=0;while(*p && *p!=',' && *p!=':'){if(n==63 || (u8)*p<=32 || *p=='=')return -1;o->name[n++]=*p++;}o->name[n]=0;if(!n)return -1;
        o->argument=0;while(*p==':'){if(o->argument==2)return -1;o->argument++;p++;}if(*p && *p!=',')return -1;
        for(int i=0;i<long_count;i++)if(equal(longs[i].name,o->name))return -1;long_count++;if(*p){p++;if(!*p)return -1;}}
    return 0;
}
static int short_argument(const char *spec,char c)
{for(int i=0;spec[i];i++){if(spec[i]==':' || spec[i]=='+' || spec[i]=='-')continue;if(spec[i]==c)return spec[i+1]==':'?(spec[i+2]==':'?2:1):0;}return -1;}
static int named_option(const char *p,int *equal_at)
{int n=0;while(p[n] && p[n]!='=')n++;*equal_at=p[n]=='='?n:-1;for(int i=0;i<long_count;i++)if(length(longs[i].name)==n){int same=1;for(int j=0;j<n;j++)if(p[j]!=longs[i].name[j])same=0;if(same)return i;}return -1;}
static int option_error(const char *name)
{if(!quiet){cli_text(2,"getopt: invalid/missing option: ");cli_text(2,name);cli_text(2,"\n");}return 1;}
int main(void)
{
    if(cli_parse()<1)return 2;if(equal(cli_argv[0],"-T"))return 4;const char *shorts=0;int at=0;
    if(cli_argv[0][0]!='-'){shorts=cli_argv[at++];}
    else for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"--")){at++;break;}
        if(equal(p,"-o")||equal(p,"--options")){if(++at==cli_argc)return 2;shorts=cli_argv[at];}
        else if(equal(p,"-l")||equal(p,"--longoptions")){if(++at==cli_argc || long_list(cli_argv[at])<0)return 2;}
        else if(equal(p,"-n")||equal(p,"--name")){if(++at==cli_argc)return 2;}
        else if(equal(p,"-s")||equal(p,"--shell")){if(++at==cli_argc || (!equal(cli_argv[at],"sh") && !equal(cli_argv[at],"bash")))return 2;}
        else if(equal(p,"-q")||equal(p,"--quiet"))quiet=1;else if(equal(p,"-Q")||equal(p,"--quiet-output"))silent=1;else if(equal(p,"-a")||equal(p,"--alternative"))alternative=1;else return 2;}
    if(!shorts)shorts="";int stopped=0;const char *positions[32];int position_count=0;
    for(;at<cli_argc;at++){const char *p=cli_argv[at];if(stopped || p[0]!='-' || !p[1]){positions[position_count++]=p;if(shorts[0]=='+')stopped=1;continue;}
        if(equal(p,"--")){stopped=1;continue;}int equal_at=-1,li=-1,long_mode=p[1]=='-';const char *name=p+(long_mode?2:1);
        if(long_mode || alternative)li=named_option(name,&equal_at);if(long_mode || li>=0){if(li<0)return option_error(p);option_name *o=longs+li;const char *value=0;
            if(equal_at>=0){if(!o->argument)return option_error(p);value=name+equal_at+1;}else if(o->argument==1){if(++at==cli_argc)return option_error(p);value=cli_argv[at];}else if(o->argument==2)value="";
            char flag[66];flag[0]=flag[1]='-';copy(flag+2,o->name,64);if(emit(flag)<0 || (value && emit(value)<0))return 2;continue;}
        for(int j=1;p[j];j++){int argument=short_argument(shorts,p[j]);if(argument<0)return option_error(p);char flag[3]={'-',p[j],0};if(emit(flag)<0)return 2;
            if(argument){const char *value=0;if(p[j+1])value=p+j+1;else if(argument==1){if(++at==cli_argc)return option_error(p);value=cli_argv[at];}else value="";
                if(emit(value)<0)return 2;break;}}}
    if(emit("--")<0)return 2;for(int i=0;i<position_count;i++)if(emit(positions[i])<0)return 2;if(silent)return 0;return cli_text(1,output)<0 || cli_text(1,"\n")<0;
}
