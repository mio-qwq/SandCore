#include "../SCIO.H"
int main(void){if(cli_parse()!=0)return 2;char name[32];if(sc_user(0,"",name,sizeof(name))<0)return 1;return cli_text(1,name)<0 || cli_text(1,"\n")<0?1:0;}
