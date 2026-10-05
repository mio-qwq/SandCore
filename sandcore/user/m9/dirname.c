#include "../SCIO.H"
int main(void){if(cli_parse()!=1)return 2;char *path=cli_argv[0];int n=length(path);while(n>1 && path[n-1]=='/')path[--n]=0;
    while(n && path[n-1]!='/')n--;while(n>1 && path[n-1]=='/')n--;if(!n)return cli_text(1,".\n")<0;path[n]=0;cli_text(1,path);return cli_text(1,"\n")<0;}
