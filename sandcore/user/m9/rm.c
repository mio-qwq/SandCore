#include "../SCFILE.H"
static int recursive,force;
static int remove_item(const char *path,int kind,u32 size,int after,void *context)
{(void)size;(void)context;if(kind==2 && !recursive)return -1;if(after){int r=sc_remove(path);if(r<0)return r;}return 0;}
int main(void){if(cli_parse()<0)return 2;int at=0;while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){
    if(equal(cli_argv[at],"--")){at++;break;}for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];if(c=='r'||c=='R')recursive=1;else if(c=='f')force=1;else return 2;}at++;}
    if(at==cli_argc)return force?0:2;int result=0;for(;at<cli_argc;at++){char p[64];u32 info[2];if(cli_path(cli_argv[at],p)<0)return 2;
        if(!*p || equal(p,"SYS") || equal(p,"SYS/CORE") || equal(p,"SYS/AUTH"))return cli_error("rm: protected root",-5);
        if(sc_stat(p,info)<0){if(!force)result=1;continue;}int r=file_walk(cli_argv[at],0,remove_item,0);if(r<0){cli_error("rm",r);result=1;}}return result;}
