#include "../SCFILE.H"
static int recursive,preserve;static char source_root[64],target_root[64];
static int copy_item(const char *path,int kind,u32 bytes,int after,void *context)
{
    (void)bytes;(void)context;if(after)return 0;if(kind==2 && !recursive)return -1;
    u32 prefix=(u32)length(source_root);if(length(path)<(int)prefix)return -1;char target[64];const char *suffix=path+prefix;
    if(*suffix=='/')suffix++;if(*suffix){if(file_join(target,target_root,suffix)<0)return -1;}else copy(target,target_root,64);
    if(equal(path,target))return -1;
    if(kind==2){u32 info[2];int r=sc_stat(target,info);if(r<0)return sc_mkdir(target);return info[0]==2?0:-1;}
    char from[65],to[65];from[0]=to[0]='/';copy(from+1,path,64);copy(to+1,target,64);int r=cli_copy_file(from,to);if(r<0)return r;
    if(preserve){u32 meta[8];if(sc_fsmeta(path,meta)<0)return -1;r=sc_permissions(target,-2,-2,meta[5]);}return r;
}
int main(void)
{
    if(cli_parse()<2)return 2;int at=0;while(at<cli_argc-2 && cli_argv[at][0]=='-' && cli_argv[at][1]){
        for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];if(c=='r'||c=='R')recursive=1;else if(c=='p')preserve=1;else if(c!='f')return 2;}at++;}
    if(cli_argc-at<2)return 2;char destination[64];if(cli_path(cli_argv[cli_argc-1],destination)<0)return 2;u32 info[2];int is_directory=sc_stat(destination,info)>=0 && info[0]==2;
    if(cli_argc-at>2 && !is_directory)return 2;
    for(int i=at;i<cli_argc-1;i++){
        if(cli_path(cli_argv[i],source_root)<0 || !*source_root)return 2;
        if(is_directory){if(file_join(target_root,destination,file_basename(source_root))<0)return 2;}else copy(target_root,destination,64);
        /* 不能在源目录里面创建目标再递归扫描它；直接拒绝自复制环。 */
        int n=length(source_root),prefix=length(target_root)>=n;
        for(int j=0;j<n && prefix;j++){u8 a=(u8)source_root[j],b=(u8)target_root[j];if(a>='a'&&a<='z')a-=32;if(b>='a'&&b<='z')b-=32;if(a!=b)prefix=0;}
        if(prefix && (!target_root[n] || target_root[n]=='/'))return cli_error("cp: target is inside source",-1);
        int r=file_walk(cli_argv[i],0,copy_item,0);if(r<0)return cli_error("cp",r);
    }return 0;
}
