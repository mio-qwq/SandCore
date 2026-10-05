#include "../SCIO.H"
int main(void){if(cli_parse()<0)return 2;int all=0;for(int i=0;i<cli_argc;i++)if(equal(cli_argv[i],"-a"))all=1;else if(!equal(cli_argv[i],"-s"))return 2;
    return cli_text(1,all?"SandCore M9 i386\n":"SandCore\n")<0;}
