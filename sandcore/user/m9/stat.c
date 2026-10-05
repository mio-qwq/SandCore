#include "../SCIO.H"
int main(void){if(cli_parse()<1)return 2;for(int i=0;i<cli_argc;i++){char path[64];u32 info[8];if(cli_path(cli_argv[i],path)<0)return 2;
    int r=sc_fsmeta(path,info);if(r<0)return cli_error("stat",r);cli_text(1,path);cli_text(1," kind=");cli_number(1,(int)info[1]);
    cli_text(1," bytes=");cli_number(1,(int)info[2]);cli_text(1," uid=");cli_number(1,(int)info[3]);cli_text(1," gid=");cli_number(1,(int)info[4]);
    cli_text(1," rw=");cli_number(1,(int)info[5]);cli_text(1," generation=");cli_number(1,(int)info[7]);cli_text(1,"\n");}return 0;}
