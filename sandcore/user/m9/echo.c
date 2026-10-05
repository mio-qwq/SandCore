#include "../SCIO.H"
int main(void)
{
    if(cli_parse()<0)return 2;int at=0,newline=1,escape=0;
    while(at<cli_argc && (equal(cli_argv[at],"-n") || equal(cli_argv[at],"-e") || equal(cli_argv[at],"-E"))){
        if(equal(cli_argv[at],"-n"))newline=0;else escape=equal(cli_argv[at],"-e");at++;
    }
    for(int i=at;i<cli_argc;i++){
        if(i>at)cli_text(1," ");const char *p=cli_argv[i];
        while(*p){char c=*p++;
            if(escape && c=='\\' && *p){c=*p++;if(c=='n')c='\n';else if(c=='t')c='\t';else if(c=='r')c='\r';
                else if(c=='b')c='\b';else if(c=='c')return 0;else if(c!='\\')cli_text(1,"\\");}
            if(cli_write(1,&c,1)<0)return 1;
        }
    }
    return newline && cli_text(1,"\n")<0?1:0;
}
