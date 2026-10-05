#include "../SCIO.H"
int main(void){int seconds;if(cli_parse()!=1 || cli_integer(cli_argv[0],&seconds)<0 || seconds<0 || seconds>21474836)return 2;
    u32 begin=(u32)sc_tick(),ticks=(u32)seconds*100;while((u32)((u32)sc_tick()-begin)<ticks)sc_yield();return 0;}
