#include "../SCIO.H"
int main(void)
{if(cli_parse()<1)return 2;int at=0,quiet=0;if(equal(cli_argv[0],"-q")){quiet=1;at++;}if(cli_argc-at!=1)return 2;
    char path[64];u32 info[2],disk[16];int root=cli_path(cli_argv[at],path)>=0 && !path[0] && sc_stat(path,info)>=0 && info[0]==2 && sc_storage2(disk)>=0 && disk[1];
    if(!quiet){cli_text(1,cli_argv[at]);cli_text(1,root?" is a mountpoint\n":" is not a mountpoint\n");}return !root;}
