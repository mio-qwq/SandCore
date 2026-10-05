#include "../SCIO.H"
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,mode=0,newline=1;
    for(;at<cli_argc;at++){char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(*p!='-' || !p[1])break;for(int i=1;p[i];i++){
            if(p[i]=='f'||p[i]=='e'||p[i]=='m')mode=p[i];else if(p[i]=='n')newline=0;else return 2;}}
    if(cli_argc-at!=1)return 2;if(!mode)return 1;char path[64];if(cli_path(cli_argv[at],path)<0)return 1;
    u32 info[2];if(mode!='m' && sc_stat(path,info)<0){if(mode!='f')return 1;char parent[64];copy(parent,path,64);int end=length(parent);while(end && parent[end-1]!='/')end--;parent[end?end-1:0]=0;
        if(sc_stat(parent,info)<0 || info[0]!=2)return 1;}
    if(cli_text(1,"/")<0 || cli_text(1,path)<0)return 1;return newline && cli_text(1,"\n")<0;
}
