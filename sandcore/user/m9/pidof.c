#include "../SCIO.H"
#include "../SCFILE.H"
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,single=0,exclude=-1;if(equal(cli_argv[at],"-s")){single=1;at++;}
    if(at<cli_argc && equal(cli_argv[at],"-o")){if(at+2>=cli_argc || cli_integer(cli_argv[at+1],&exclude)<0)return 2;at+=2;}
    if(at==cli_argc)return 2;u32 table[SC_PROCESS_PAGE_WORDS],self[8],cursor=0xFFFFFFFFu;if(sc_process_self(self)<0)return 1;int found=0;
    do{
        if(sc_process_page(table,SC_PROCESS_PAGE_WORDS,cursor)<0)return 1;
        for(u32 i=0;i<table[3];i++){u32 *row=table+16+i*table[2];if(!row[0] || !row[1] || row[1]==2 || row[0]==self[1] || (int)row[0]==exclude)continue;const char *name=(const char *)(row+8);
            for(int j=at;j<cli_argc;j++)if(equal(name,file_basename(cli_argv[j]))){if(found)cli_text(1," ");cli_number(1,(int)row[0]);found=1;break;}if(found && single)break;}
        cursor=table[4];
    }while(cursor!=0xFFFFFFFFu && !(found && single));
    if(found)cli_text(1,"\n");return found?0:1;
}
