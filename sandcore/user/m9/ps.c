#include "../SCIO.H"
int main(void){u32 table[520];if(sc_processes2(table,520)<0)return 1;cli_text(1,"PID UID GID STATE NAME\n");
    for(u32 i=0;i<table[1];i++){u32 *r=table+8+i*table[2];if(!r[1])continue;
        cli_number(1,(int)r[0]);cli_text(1," ");cli_number(1,(int)r[3]);cli_text(1," ");cli_number(1,(int)r[4]);cli_text(1," ");
        cli_number(1,(int)r[1]);cli_text(1," ");cli_text(1,(char *)(r+8));cli_text(1,"\n");}
    return 0;}
