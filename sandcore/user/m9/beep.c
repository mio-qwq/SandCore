#include "../SCIO.H"
int main(void){int kind=2;if(cli_parse()<0 || cli_argc>1 || (cli_argc && cli_integer(cli_argv[0],&kind)<0))return 2;
    int result=sc_audio_effect(kind);return result<0?cli_error("beep",result):0;}
