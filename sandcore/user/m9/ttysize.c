#include "../SCTEXT.H"
int main(void){u32 info[8];if(sc_terminal_info2(1,info)<0 || !info[2] || !info[3])return 1;text_unsigned(1,info[2]);cli_text(1," ");text_unsigned(1,info[3]);return cli_text(1,"\n")<0?1:0;}
