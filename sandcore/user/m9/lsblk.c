#include "../SCTEXT.H"
int main(void){u32 info[32];if(sc_storage(info)<0)return 1;cli_text(1,"DEVICE KIND SIZE_KiB\nFLOPPY0 boot-contract ");text_unsigned(1,info[4]/2);cli_text(1,"\n");
    if(info[1]){cli_text(1,"IDE0 ATA-LBA28 ");text_unsigned(1,info[3]/2);cli_text(1,info[6]?" SandFS-whole-device\n":" no-volume\n");}return 0;}
