#include "../SCIO.H"
int main(void){char name[32];if(sc_user(0,"",name,32)<0)return 1;cli_text(1,name);cli_text(1,"\n");return 0;}
