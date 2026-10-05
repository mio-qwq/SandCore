#include "../SCTEXT.H"
int main(void){if(cli_parse()<0)return 2;int at=0,quiet=0;if(cli_argc && equal(cli_argv[0],"-s")){quiet=1;at++;}if(cli_argc-at!=2)return 2;
    int a=cli_input(at),b=cli_input(at+1);if(a<0 || b<0){if(a>0)sc_stream_close(a,0);if(b>0)sc_stream_close(b,0);return 2;}
    text_reader ar={a,{0},0,0},br={b,{0},0,0};u32 offset=0,line=1;int result=0;
    for(;;){int x=text_next(&ar),y=text_next(&br);if((x<0 && x!=-256)||(y<0 && y!=-256)){result=2;break;}if(x!=y){result=1;if(!quiet){cli_text(1,"differ: byte ");text_unsigned(1,offset+1);cli_text(1,", line ");text_unsigned(1,line);cli_text(1,"\n");}break;}if(x==-256)break;offset++;if(x=='\n')line++;}
    if(a)sc_stream_close(a,0);if(b)sc_stream_close(b,0);return result;}
