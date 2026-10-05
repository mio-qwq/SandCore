#include "../SCIO.H"
int main(void)
{
    if(cli_parse()<1 || cli_argc>3)return 2;int at=0,where=0;if(equal(cli_argv[at],"-w")){where=1;at++;}if(at<cli_argc && equal(cli_argv[at],"1"))at++;
    if(at+1!=cli_argc)return 2;const char *name=cli_argv[at];int n=length(name);if(n<1 || n>24)return 2;
    for(int i=0;i<n;i++){char c=name[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='['))return 2;}
    char path[64];copy(path,"/SYS/MAN/",64);append(path,name,64);append(path,".TXT",64);int fd=sc_stream_open(path,1,0);if(fd<0)return cli_error("man: page",fd);
    int result=where?(cli_text(1,path)<0 || cli_text(1,"\n")<0?1:0):(cli_copy_fd(fd,1)<0?1:0);sc_stream_close(fd,0);return result;
}
