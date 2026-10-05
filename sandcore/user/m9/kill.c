#include "../SCIO.H"
int main(void){if(cli_parse()<1)return 2;for(int i=0;i<cli_argc;i++){int pid;if(cli_integer(cli_argv[i],&pid)<0)return 2;
    int r=sc_kill2(pid);if(r<0)return cli_error("kill",r);}return 0;}
