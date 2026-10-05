#include "../SCFILE.H"
int main(void){if(cli_parse()<1)return 2;char paths[256];if(sc_user(4,"PATH",paths,sizeof(paths))<0)copy(paths,"/BIN:/APPS",256);int result=0;
    for(int i=0;i<cli_argc;i++){const char *p=paths;int found=0;while(*p){char parent[64],candidate[64];int n=0;while(*p && *p!=':'){if(n==63)return 2;parent[n++]=*p++;}parent[n]=0;if(*p)p++;
        if(file_join(candidate,parent,cli_argv[i])<0)continue;char path[64];u32 meta[2];
        for(int suffix=0;suffix<2;suffix++){if(cli_path(candidate,path)>=0 && sc_stat(path,meta)>=0 && meta[0]==1){int fd=sc_stream_open(candidate,1,0);
                if(fd>=0){sc_stream_close(fd,0);cli_text(1,"/");cli_text(1,path);cli_text(1,"\n");found=1;break;}}
            if(length(candidate)>59)break;append(candidate,".scx",64);}if(found)break;}
        if(!found)result=1;}return result;}
