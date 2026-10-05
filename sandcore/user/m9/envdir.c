#include "../SCLAUNCH.H"
#include "../SCTEXT.H"
int main(void)
{
    if(cli_parse()<2)return 2;char path[64];if(cli_path(cli_argv[0],path)<0)return 2;char *list=sc_alloc(65536);if(!list)return 1;
    int result=1;if(sc_dir(path,list,65536)<0)goto done;const char *p=list;
    while(*p){char kind=*p++;if(*p++!=' ')goto done;char name[32];int used=0;while(*p && *p!=' ' && *p!='\n'){if(used==31)goto done;name[used++]=*p++;}name[used]=0;
        while(*p && *p!='\n')p++;if(*p)p++;if(kind!='F')continue;
        char full[65];full[0]='/';copy(full+1,path,64);if(length(full)+used+2>65)goto done;if(path[0])append(full,"/",65);append(full,name,65);
        int fd=sc_stream_open(full,1,0);if(fd<0)goto done;u32 bytes;char *value=cli_slurp(fd,256,&bytes);sc_stream_close(fd,0);if(!value)goto done;
        if(!bytes){sc_free(value);if(sc_env_set(name,0,1)<0)goto done;continue;}u32 n=0;while(n<bytes && value[n]!='\n')n++;
        while(n && (value[n-1]==' ' || value[n-1]=='\t' || value[n-1]=='\r'))n--;value[n]=0;for(u32 i=0;i<n;i++)if(!value[i])value[i]=' ';
        int r=sc_env_set(name,value,0);sc_free(value);if(r<0)goto done;}
    {int fds[3]={0,1,2},job=launch_words(1,fds);result=job<0?1:launch_wait((u32)job);}
done:
    sc_free(list);return result;
}
