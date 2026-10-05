#include "../SCIO.H"
int main(void){if(cli_parse()!=1)return 2;int r=sc_auth_account(1,cli_argv[0],0,0,0);return r<0?cli_error("deluser",r):0;}
