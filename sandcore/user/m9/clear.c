#include "../SCIO.H"
int main(void){if(cli_parse()!=0)return 2;for(;;){int n=sc_terminal_clear(1);if(n==-6){sc_yield();continue;}return n<0?1:0;}}
