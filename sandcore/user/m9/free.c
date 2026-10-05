#include "../SCTEXT.H"
int main(void){u32 info[48];if(sc_monitor(info)<0)return 1;cli_text(1,"MEMORY TOTAL_KiB USED_KiB FREE_KiB\nPhysical ");
    for(int i=2;i<=4;i++){text_unsigned(1,info[i]/1024);cli_text(1,i==4?"\n":" ");}return 0;}
