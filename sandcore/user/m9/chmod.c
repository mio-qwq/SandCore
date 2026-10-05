#include "../SCFILE.H"
int main(void){if(cli_parse()<2)return 2;u32 mode;if(file_mode(cli_argv[0],&mode)<0)return cli_error("chmod: mode ugo (digits 0..3, r=1 w=2)",-1);
    for(int i=1;i<cli_argc;i++){char path[64];if(cli_path(cli_argv[i],path)<0)return 2;int r=sc_permissions(path,-2,-2,mode);if(r<0)return cli_error("chmod",r);}return 0;}
