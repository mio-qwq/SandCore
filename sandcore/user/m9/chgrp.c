#include "../SCFILE.H"
int main(void){if(cli_parse()<2)return 2;int gid;if(cli_integer(cli_argv[0],&gid)<0)return 2;
    for(int i=1;i<cli_argc;i++){char path[64];u32 meta[8];if(cli_path(cli_argv[i],path)<0 || sc_fsmeta(path,meta)<0)return 1;int r=sc_permissions(path,-2,gid,meta[5]);if(r<0)return cli_error("chgrp",r);}return 0;}
