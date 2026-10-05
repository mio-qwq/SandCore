#include "SCIO.H"
int main(void){cli_text(1,"TARGET START\n");int x=1;for(int i=0;i<8;i++){x+=i;sc_yield();}cli_number(1,x);cli_text(1,"\nTARGET END\n");return 0;}
