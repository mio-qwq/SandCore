#include "../SCIO.H"
int main(void){u32 table[SC_PROCESS_PAGE_WORDS],cursor=0xFFFFFFFFu;cli_text(1,"PID UID GID STATE NAME\n");
    do{
        if(sc_process_page(table,SC_PROCESS_PAGE_WORDS,cursor)<0)return 1;
        for(u32 i=0;i<table[3];i++){u32 *r=table+16+i*table[2];if(!r[1])continue;
        cli_number(1,(int)r[0]);cli_text(1," ");cli_number(1,(int)r[3]);cli_text(1," ");cli_number(1,(int)r[4]);cli_text(1," ");
            cli_number(1,(int)r[1]);cli_text(1," ");cli_text(1,r[0]?(char *)(r+8):"kernel");cli_text(1,"\n");}
        cursor=table[4];
    }while(cursor!=0xFFFFFFFFu);
    return 0;}
