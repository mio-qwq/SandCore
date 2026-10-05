#include "../SCIO.H"
int main(void){if(cli_parse()<1 || cli_argc>2)return 2;int resident=cli_argc==2 && equal(cli_argv[1],"--resident");
    if(cli_argc==2 && !resident)return 2;char path[64];if(cli_path(cli_argv[0],path)<0)return 2;
    int result=sc_module_exec(path,resident);return result<0?cli_error("skmrun",result):result;}
