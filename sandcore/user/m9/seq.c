#include "../SCIO.H"
int main(void){if(cli_parse()<0 || !cli_argc || cli_argc>3)return 2;int first=1,step=1,last;
    if(cli_integer(cli_argv[cli_argc-1],&last)<0)return 2;if(cli_argc>=2 && cli_integer(cli_argv[0],&first)<0)return 2;
    if(cli_argc==3 && (cli_integer(cli_argv[1],&step)<0 || !step))return 2;
    for(int n=first;step>0?n<=last:n>=last;){char text[12];decimal(text,n);if(cli_text(1,text)<0 || cli_text(1,"\n")<0)return 1;
        if(step>0?(u32)n+0x80000000u>0xFFFFFFFFu-(u32)step:(u32)n+0x80000000u<0u-(u32)step)break;n=(int)((u32)n+(u32)step);sc_yield();}return 0;}
