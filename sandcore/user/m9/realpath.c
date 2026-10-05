#include "../SCIO.H"
int main(void){if(cli_parse()<1)return 2;for(int i=0;i<cli_argc;i++){char path[64];u32 info[2];if(cli_path(cli_argv[i],path)<0 || sc_stat(path,info)<0)return cli_error("realpath",-1);
    cli_text(1,"/");cli_text(1,path);cli_text(1,"\n");}return 0;}
