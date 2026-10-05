#include "../SCIO.H"
int main(void)
{if(cli_parse()<1)return 2;for(int i=0;i<cli_argc;i++){char path[64];u32 info[2];if(cli_path(cli_argv[i],path)<0 || sc_stat(path,info)<0 || info[0]!=1)return 1;}
    int r=sc_storage_flush();return r<0?cli_error("fsync",r):0;}
