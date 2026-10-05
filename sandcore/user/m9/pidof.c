#include "../SCIO.H"
#include "../SCFILE.H"
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,single=0,exclude=-1;if(equal(cli_argv[at],"-s")){single=1;at++;}
    if(at<cli_argc && equal(cli_argv[at],"-o")){if(at+2>=cli_argc || cli_integer(cli_argv[at+1],&exclude)<0)return 2;at+=2;}
    if(at==cli_argc)return 2;u32 table[520],self[8];if(sc_process_self(self)<0 || sc_processes2(table,520)<0)return 1;int found=0;
    for(u32 i=1;i<table[1];i++){u32 *row=table+8+i*table[2];if(!row[1] || row[1]==2 || row[0]==self[1] || (int)row[0]==exclude)continue;const char *name=(const char *)(row+8);
        for(int j=at;j<cli_argc;j++)if(equal(name,file_basename(cli_argv[j]))){if(found)cli_text(1," ");cli_number(1,(int)row[0]);found=1;break;}if(found && single)break;}
    if(found)cli_text(1,"\n");return found?0:1;
}
