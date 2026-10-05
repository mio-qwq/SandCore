#include "../SCIO.H"
int main(void){if(cli_parse()<1)return 2;int parents=equal(cli_argv[0],"-p"),first=parents?1:0;
    for(int i=first;i<cli_argc;i++){
        char path[64];if(cli_path(cli_argv[i],path)<0)return 2;
        for(int at=0;;at++){char c=path[at];if(!c || (parents && c=='/')){path[at]=0;u32 info[2];
                if(sc_stat(path,info)<0){int r=sc_mkdir(path);if(r<0)return cli_error("mkdir",r);}else if(info[0]!=2 || (!parents && !c))return 1;
                path[at]=c;}if(!c)break;
        }
    }
    return 0;
}
