#include "../SCIO.H"
int main(void){char a[64],b[64];if(cli_parse()!=2 || cli_path(cli_argv[0],a)<0 || cli_path(cli_argv[1],b)<0)return 2;
    int r=sc_rename(a,b);return r<0?cli_error("mv",r):0;}
