#include "../SCFILE.H"
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,directory=0;u32 mode=23;int uid=-2,gid=-2;
    while(at<cli_argc && cli_argv[at][0]=='-'){char *option=cli_argv[at++];if(equal(option,"--"))break;if(equal(option,"-d"))directory=1;
        else if(equal(option,"-m")||equal(option,"-o")||equal(option,"-g")){if(at==cli_argc)return 2;if(option[1]=='m'){if(file_mode(cli_argv[at++],&mode)<0)return 2;}
            else {int n;if(cli_integer(cli_argv[at++],&n)<0 || n<-1)return 2;if(option[1]=='o')uid=n;else gid=n;}}
        else if(equal(option,"-c")){}else return 2;}
    if(directory){if(at==cli_argc)return 2;for(;at<cli_argc;at++){char path[64];if(cli_path(cli_argv[at],path)<0 || !*path)return 2;
            for(int i=0;;i++){char c=path[i];if(!c || c=='/'){path[i]=0;u32 info[2];int n=sc_stat(path,info);if(n<0)n=sc_mkdir(path);else if(info[0]!=2)n=-1;path[i]=c;if(n<0)return 1;}if(!c)break;}
            if(sc_permissions(path,uid,gid,mode)<0)return 1;}return 0;}
    if(cli_argc-at<2)return 2;const char *target=cli_argv[cli_argc-1];char destination[64];u32 info[2];if(cli_path(target,destination)<0)return 2;
    int is_dir=sc_stat(destination,info)>=0 && info[0]==2;if(cli_argc-at>2 && !is_dir)return 2;
    for(int i=at;i<cli_argc-1;i++){char path[64],rooted[65];if(is_dir){if(file_join(path,destination,file_basename(cli_argv[i]))<0)return 2;}else copy(path,destination,64);
        char source_path[64];if(cli_path(cli_argv[i],source_path)<0 || !text_compare(source_path,(u32)length(source_path),path,(u32)length(path),1))return 2;
        rooted[0]='/';copy(rooted+1,path,64);if(cli_copy_file(cli_argv[i],rooted)<0 || sc_permissions(path,uid,gid,mode)<0)return 1;}return 0;
}
