#include "../SCCALENDAR.H"
int main(void)
{if(cli_parse()<0)return 2;for(int i=0;i<cli_argc;i++)if(!equal(cli_argv[i],"-r") && !equal(cli_argv[i],"--show") && !equal(cli_argv[i],"-u") && !equal(cli_argv[i],"--utc"))return 2;
    u32 value[8];if(calendar_read(value)<0)return 1;calendar_digits((int)value[1],4,'0');cli_text(1,"-");calendar_digits((int)value[2],2,'0');cli_text(1,"-");calendar_digits((int)value[3],2,'0');cli_text(1," ");
    for(int i=4;i<7;i++){calendar_digits((int)value[i],2,'0');if(i!=6)cli_text(1,":");}return cli_text(1," UTC\n")<0;}
