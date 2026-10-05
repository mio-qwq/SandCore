#include "../SCIO.H"
int main(void){if(cli_parse()<0)return 2;char line[1024];line[0]=0;if(!cli_argc)copy(line,"y",sizeof(line));
    for(int i=0;i<cli_argc;i++){if(length(line)+length(cli_argv[i])+2>=1024)return 2;if(i)append(line," ",1024);append(line,cli_argv[i],1024);}append(line,"\n",1024);
    for(;;){if(cli_text(1,line)<0)return 0;sc_yield();}}
