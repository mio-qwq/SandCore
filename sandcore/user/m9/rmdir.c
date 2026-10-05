#include "../SCIO.H"
int main(void){if(cli_parse()<1)return 2;for(int i=0;i<cli_argc;i++){char path[64],list[4];u32 info[2];if(cli_path(cli_argv[i],path)<0 || sc_stat(path,info)<0 || info[0]!=2 || sc_dir(path,list,sizeof(list))!=0)return 1;
    /* 旧sc_dir容量不足会返回已装入项；用32KiB再确认空目录，不能把
     * 小缓冲0字节误当成空目录并递归删除其内容。 */
    char *full=sc_alloc(65536);if(!full)return 1;int n=sc_dir(path,full,65536);sc_free(full);if(n!=0)return 1;int r=sc_remove(path);if(r<0)return cli_error("rmdir",r);}return 0;}
