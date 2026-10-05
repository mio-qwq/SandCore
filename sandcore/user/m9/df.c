#include "../SCTEXT.H"
int main(void){u32 disk[16];if(sc_storage2(disk)<0 || !disk[1])return 1;cli_text(1,"SandFS TOTAL_KiB LIVE_KiB RESERVED_KiB FREE_KiB MAX_RUN_KiB ENTRIES\n/ ");
    u32 values[5]={disk[3]/2,disk[8]/2,disk[9]/2,disk[10]/2,disk[11]/2};for(int i=0;i<5;i++){text_unsigned(1,values[i]);cli_text(1," ");}
    text_unsigned(1,disk[7]);cli_text(1,"/");text_unsigned(1,disk[6]);cli_text(1,disk[15]?" READONLY\n":"\n");return 0;}
