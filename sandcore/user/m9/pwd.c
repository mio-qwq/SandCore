#include "../SCIO.H"
int main(void){char cwd[64];if(sc_user(2,"",cwd,64)<0)return 1;cli_text(1,cwd);cli_text(1,"\n");return 0;}
