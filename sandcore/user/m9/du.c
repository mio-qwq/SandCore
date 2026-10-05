#include "../SCFILE.H"
static u32 total;static int summary;
static int count(const char *path,int kind,u32 bytes,int after,void *context)
{(void)context;if(after)return 0;if(kind==1){u32 blocks=(bytes+1023)/1024;if(total>0xFFFFFFFFu-blocks)return -1;total+=blocks;
    if(!summary){text_unsigned(1,blocks);cli_text(1,"\t/");cli_text(1,path);cli_text(1,"\n");}}return 0;}
int main(void){if(cli_parse()<0)return 2;int at=0;if(cli_argc && equal(cli_argv[0],"-s")){summary=1;at=1;}
    for(int i=at;i<cli_argc || (i==at && at==cli_argc);i++){total=0;const char *path=i==cli_argc?".":cli_argv[i];if(file_walk(path,0,count,0)<0)return 1;
        text_unsigned(1,total);cli_text(1,"\t");cli_text(1,path);cli_text(1,"\n");}return 0;}
