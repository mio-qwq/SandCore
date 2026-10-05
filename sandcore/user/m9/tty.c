#include "../SCIO.H"
int main(void){u32 info[8];if(sc_terminal_info2(0,info)<0)return 1;if(info[1]==4)return cli_text(1,"external-serial\n")<0?1:0;
    if(info[1]==5){cli_text(1,"window/");cli_number(1,(int)info[5]);return cli_text(1,"\n")<0?1:0;}cli_text(1,"not a tty\n");return 1;}
