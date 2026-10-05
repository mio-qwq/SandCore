#include "../SCFILE.H"
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,quiet=0;if(equal(cli_argv[0],"-q")){quiet=1;at++;}if(at==cli_argc)return 2;
    u32 table[520],self[8];if(sc_processes2(table,520)<0 || sc_process_self(self)<0)return 1;int result=0;
    for(int a=at;a<cli_argc;a++){if(cli_argv[a][0]=='-')return 2;int found=0;
        for(u32 i=1;i<table[1];i++){u32 *row=table+8+i*table[2];if(!row[1] || row[1]==2 || row[0]==self[1])continue;
            if(equal((char *)(row+8),file_basename(cli_argv[a]))){found=1;int r=sc_kill_generation((int)row[0],row[2]);if(r<0){result=1;if(!quiet)cli_error(cli_argv[a],r);}}}
        if(!found){result=1;if(!quiet){cli_text(2,"killall: no match: ");cli_text(2,cli_argv[a]);cli_text(2,"\n");}}}return result;
}
