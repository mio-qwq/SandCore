#include "host.h"
#include "exaudio.h"
#define main pcalc_entry
#include "../pcalc/pcalc.c"
#undef main
HOST_UI()
int main(void)
{
    struct {const char *s;int v;} cases[]={{"2+3*4",14},{"(2+3)*4",20},{"1<<2+1",8},{"-1+2",1},{"!1==0",1},{"5&3",1},{"4|1",5},{"5^3",6},{"2<3&&4>=4",1},{"0&&1/0",0},{"1||UNKNOWN",1},{"1?7:1/0",7},{"0?1/0:9",9},{"0xffffffff+1",0},{"'A'+1",66},{"1+2==3?4:5",4}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){int v,e;int rc=ex_eval(cases[i].s,0,0,&v,&e);if(rc)fprintf(stderr,"Expression rejected: %s at %d\n",cases[i].s,e);CHECK(!rc);CHECK(v==cases[i].v);}
    int v,e;CHECK(ex_eval("1/0",0,0,&v,&e)<0);CHECK(ex_eval("1+",0,0,&v,&e)<0);CHECK(ex_eval("999999999999999",0,0,&v,&e)<0);
    char out[32];ex_dec(out,32,-2147483647-1);CHECK(!strcmp(out,"-2147483648"));ex_udec(out,32,0xffffffff);CHECK(!strcmp(out,"4294967295"));
    char sentinel='X';ex_dec(&sentinel,0,0);CHECK(sentinel=='X');ex_udec(&sentinel,0,0);CHECK(sentinel=='X');ex_bin(&sentinel,0,0,32);CHECK(sentinel=='X');
    unsigned n;CHECK(ex_parse_u32_hex("000000000",&n)<0);CHECK(!ex_parse_u32_hex("FFFFFFFF",&n)&&n==0xffffffff);
    char csv[]="\"a,b\",\"x\"\"y\",,last\r\n";char *f[4];CHECK(ex_csv_split(csv,f,4)==4);CHECK(!strcmp(f[0],"a,b"));CHECK(!strcmp(f[1],"x\"y"));CHECK(!*f[2]);CHECK(!strcmp(f[3],"last"));
    char bad[]="a,b,c";CHECK(ex_csv_split(bad,f,2)<0);char bad2[]="\"a\"z,b";CHECK(ex_csv_split(bad2,f,4)<0);
    char utf[]={(char)0xED,(char)0xA0,(char)0x80,0};unsigned cp;CHECK(ex_utf8_next(utf,&cp)==1&&cp==0xfffd);
    char cut[3];ex_text_cut(cut,3,"abcdef",100);CHECK(!strcmp(cut,"ab"));
    copy(pc_input,"2+3*4",PC_INPUT);pc_len=length(pc_input);pc_evaluate();CHECK(pc_result==14&&pc_has_result&&!pc_error[0]);pc_action(PC_ID_PAD+39);CHECK(pc_result==14);
    copy(pc_input,"7+1",PC_INPUT);pc_len=length(pc_input);pc_evaluate();pc_action(PC_ID_HIST);CHECK(!strcmp(pc_input,"7+1"));pc_action(PC_ID_HIST+1);CHECK(!strcmp(pc_input,"2+3*4"));
    host_audio_budget=3;CHECK(!exaudio_init());short samples[16];for(int i=0;i<16;i++)samples[i]=i+1;
    CHECK(exaudio_stream(samples,8)==3);CHECK(host_audio_frames==3);host_audio_budget=100;exaudio_pump();CHECK(host_audio_frames==8);CHECK(!memcmp(host_audio,samples,sizeof(samples)));
    host_audio_budget=100000;host_audio_frames=host_nonzero=0;CHECK(!exaudio_tone(440,50,EXWAVE_TRIANGLE,200));CHECK(host_nonzero>100);CHECK(host_audio_frames==2400);exaudio_shutdown();
    host_ui(640,480,100,0);pc_draw();host_image("pcalc-aurora");host_ui(641,481,200,0);pc_draw();host_image("pcalc-200");
    char input[PC_INPUT];copy(input,pc_input,PC_INPUT);host_key_input=27;pc_action(PK_ABOUT_ID);CHECK(!strcmp(pc_input,input)&&ui_action==0&&ui_modal==0);host_image("about-pcalc-200");
    host_key_input=10;pk_about("PCalc","Expression calculator");CHECK(ui_action==0&&ui_modal==0);
    host_pointer_x=32*ui_scale/100;host_pointer_y=(UI_H-60)*ui_scale/100;host_pointer_click=1;pk_about("PCalc","Expression calculator");CHECK(ui_action==0&&ui_modal==0);
    host_report("utils/pcalc/audio");return 0;
}
