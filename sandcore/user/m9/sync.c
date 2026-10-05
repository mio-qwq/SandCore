#include "../SCIO.H"
int main(void){if(cli_parse()!=0)return 2;int r=sc_storage_flush();return r<0?cli_error("sync",r):0;}
