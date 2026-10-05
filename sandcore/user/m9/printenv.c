#include "../SCIO.H"
int main(void){if(cli_parse()<0)return 2;if(!cli_argc){char text[8192];int n=sc_env_list(text,sizeof(text));return n<0?1:cli_write(1,text,(u32)n)<0;}
    int result=0;for(int i=0;i<cli_argc;i++){char value[256];if(sc_user(4,cli_argv[i],value,sizeof(value))<0){result=1;continue;}cli_text(1,value);cli_text(1,"\n");}return result;}
