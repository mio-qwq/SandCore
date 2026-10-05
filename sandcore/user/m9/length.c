#include "../SCIO.H"
int main(void){if(cli_parse()!=1)return 2;cli_number(1,length(cli_argv[0]));return cli_text(1,"\n")<0?1:0;}
