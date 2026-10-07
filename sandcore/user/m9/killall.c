#include "../SCFILE.H"
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,quiet=0;if(equal(cli_argv[0],"-q")){quiet=1;at++;}if(at==cli_argc)return 2;
    u32 table[SC_PROCESS_PAGE_WORDS],self[8];if(sc_process_self(self)<0)return 1;int result=0;
    for(int a=at;a<cli_argc;a++){if(cli_argv[a][0]=='-')return 2;int found=0;
        u32 cursor=0xFFFFFFFFu;do{
            if(sc_process_page(table,SC_PROCESS_PAGE_WORDS,cursor)<0)return 1;
            for(u32 i=0;i<table[3];i++){u32 *row=table+16+i*table[2];if(!row[0] || !row[1] || row[1]==2 || row[0]==self[1])continue;
                if(equal((char *)(row+8),file_basename(cli_argv[a]))){found=1;int r=sc_kill_generation((int)row[0],row[2]);if(r<0){result=1;if(!quiet)cli_error(cli_argv[a],r);}}}
            cursor=table[4];
        }while(cursor!=0xFFFFFFFFu);
        if(!found){result=1;if(!quiet){cli_text(2,"killall: no match: ");cli_text(2,cli_argv[a]);cli_text(2,"\n");}}}return result;
}
