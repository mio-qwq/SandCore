#include "SCIO.H"
#include "SCSIMD.H"
/* 客体集成探针，第二阶段由系统内s3c编译；不向内核加入测试后门。 */
static int failures;
static void check(int okay,const char *name)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,name);cli_text(1,"\n");if(!okay)failures++;}
static int abi(void)
{
    u32 old[68],modern[204];for(int i=0;i<68;i++)old[i]=0x7A31BE49u;for(int i=0;i<204;i++)modern[i]=0x7A31BE49u;
    check(sc_call(0x79,(int)(old+2),0,0,0,0,0)==0,"old CPUINFO return");check(old[0]==0x7A31BE49u && old[1]==0x7A31BE49u && old[66]==0x7A31BE49u && old[67]==0x7A31BE49u,"old CPUINFO 256B guard");
    check(sc_cpu2(modern+2)==0,"CPUINFO2 return");check(modern[0]==0x7A31BE49u && modern[1]==0x7A31BE49u && modern[202]==0x7A31BE49u && modern[203]==0x7A31BE49u,"CPUINFO2 800B guard");
    u32 small[12];for(int i=0;i<12;i++)small[i]=0x7A31BE49u;check(sc_stat("/SYS",small+2)==0,"old STAT return");check(small[1]==0x7A31BE49u && small[4]==0x7A31BE49u,"old STAT 8B guard");
    for(int i=0;i<12;i++)small[i]=0x7A31BE49u;check(sc_auth_info(small+2)==0,"AUTHINFO return");check(small[1]==0x7A31BE49u && small[10]==0x7A31BE49u,"AUTHINFO 32B guard");
    check(sc_cpu2((u32 *)0x1000)<0 && sc_clock2((u32 *)0x1000)<0 && sc_auth_info((u32 *)0x1000)<0,"unmapped snapshots denied");return failures;
}
static int streams(void)
{
    int ends[2];u8 send[1024],received[1024];for(int i=0;i<1024;i++)send[i]=(u8)i;
    check(sc_stream_pipe(ends)==0,"pipe create");if(failures)return failures;
    check(cli_write(ends[1],send,1024)==1024,"binary pipe write");check(cli_read(ends[0],received,1024)==1024,"binary pipe read");int same=1;for(int i=0;i<1024;i++)if(send[i]!=received[i])same=0;check(same,"NUL high bytes preserved");
    sc_stream_close(ends[1],0);check(cli_read(ends[0],received,1)==0,"pipe EOF after writer close");sc_stream_close(ends[0],0);
    int fd=sc_stream_memory(send,1024);check(fd>=3,"memory snapshot create");if(fd>=3){send[0]=99;check(cli_read(fd,received,1024)==1024 && received[0]==0,"memory snapshot independent");sc_stream_close(fd,0);}
    sc_remove("/TMP/M9ATOM");check(sc_create_ex("/TMP/M9ATOM",1,3)==0,"exclusive file create");check(sc_create_ex("/TMP/M9ATOM",1,63)==-8,"exclusive collision");
    u32 meta[8];check(sc_fsmeta("/TMP/M9ATOM",meta)==0 && meta[5]==3 && meta[2]==0,"private mode at publication");
    fd=sc_stream_open("/TMP/M9ATOM",2,4);check(fd>=3,"replacement transaction");if(fd>=3){cli_write(fd,"new",3);sc_stream_close(fd,0);char bytes[4];check(sc_read("/TMP/M9ATOM",bytes,4)==0,"abort preserves old file");}
    sc_remove("/TMP/M9ATOM");return failures;
}
static int security(const char *role)
{
    u32 identity[8];check(sc_auth_info(identity)==0,"identity readable");int uid=equal(role,"root")?0:1001;check((int)identity[1]==uid && identity[3]==0,"normal realm UID bound");
    check(sc_auth_login("SYSTEM","wrong")<0,"SYSTEM ordinary login denied");check(sc_stream_open("/SYS/MOD/ESCAPE",2,1)<0,"MOD write denied");
    check(sc_create_ex("/SYS/MOD/ESCAPE",1,3)==-5,"MOD exclusive create denied");check(sc_module_exec("/SYS/MOD/M9ONCE.SKM",0)==-5,"Ring0 denied");
    char modules[2049];check(sc_module_list(modules,sizeof(modules))==-5,"resident names require serial SYSTEM");
    char window_text[12];int window_bytes=sc_read("/TMP/M9WIN",window_text,11),window=0;u32 captured[8];if(window_bytes>0){window_text[window_bytes]=0;if(cli_integer(window_text,&window)==0)check(sc_capture_open(window,captured)==-5,"SYSTEM window capture denied");else check(0,"SYSTEM window handle");}else check(0,"SYSTEM window fixture ready");
    check(sc_stream_open("/DEV/COM2",1,0)<0,"raw UART file unavailable");int terminal=sc_stream_open("/DEV/COM2",8,0);check(terminal>=3,"terminal borrow");if(terminal>=3){u32 endpoint[8];check(sc_terminal_info2(terminal,endpoint)==0 && (endpoint[1]==4 || endpoint[1]==5),"borrowed endpoint is caller terminal");sc_stream_close(terminal,0);}
    char bytes[16];int r=sc_read("/TMP/M9PRIVATE",bytes,16);check(uid==0?r==6:r<0,uid==0?"root ordinary rw bypass":"other UID read denied");
    int fds[3]={0,1,2},job=sc_spawn2("/TMP/IODENY.SCX",fds,0);u32 state[8],begin=sc_tick();int finished=-1;
    if(job>0){do{finished=sc_wait2((u32)job,state);if(finished<=0)break;sc_yield();}while((u32)sc_tick()-begin<300);
        if(finished>0 && state[3]){sc_kill_generation((int)state[3],state[4]);sc_wait2((u32)job,state);}}
    check(job>0 && finished==0 && state[2]==0,"raw UART IO GP under bound UID");
    check(sc_env_set("UID","-1",0)==0,"untrusted UID environment accepted");check(sc_auth_info(identity)==0 && (int)identity[1]==uid,"environment cannot change credentials");return failures;
}
static int simd(void)
{
    u32 info[8];check(sc_simd_info(info)==0,"SIMD capabilities");if(!info[3]){cli_text(1,"UNSUPPORTED SSE2; scalar fallback\n");u32 a[4]={1,2,3,4},b[4]={8,7,6,5},out[4];sc_add4(out,a,b,0);for(int i=0;i<4;i++)check(out[i]==9,"scalar add");return failures;}
    u32 a[4]={1,2,3,4},b[4]={0xFFFFFFFFu,10,20,30},out[4];for(int n=0;n<200;n++){sc_add4(out,a,b,1);sc_yield();for(int i=0;i<4;i++)if(out[i]!=a[i]+b[i]){check(0,"SSE2 add after yield");return failures;}}
    check(1,"SSE2 integer add after yield");return failures;
}
static int audio(void)
{
    u32 info[16];check(sc_audio_info(info)==0 && info[1] && info[2]==48000,"AC97 ready");if(failures)return failures;short pcm[512];for(int i=0;i<512;i++)pcm[i]=(short)((i%32<16?1:-1)*2000);
    int voice=sc_audio_open(24000,1);check(voice>0,"PCM voice open");if(voice>0){check(sc_audio_submit((u32)voice,pcm,512)==512,"PCM queued");check(sc_audio_control((u32)voice,4,0)==0,"PCM finish");
        u32 start=sc_tick();int status=0;do{status=sc_audio_status((u32)voice,info);if(status<0 || !info[3])break;sc_yield();}while((u32)sc_tick()-start<300);check(status==0,"PCM status");check(info[3]==0,"PCM queue drained");check(sc_audio_control((u32)voice,0,0)==0,"PCM voice reclaimed");}
    return failures;
}
static int gui(void)
{
    int window=sc_open_rgb("M9 Capture",340,230);check(window>=0,"window created");if(window<0)return failures;int dimensions[5];if(sc_info(window,dimensions)<0)return 1;
    u32 size=(u32)dimensions[2]*(u32)dimensions[3],*pixels=sc_alloc(size*4);if(!pixels)return 1;
    /* FRAME32接收ARGB；纯RGB常量的高字节为0，必须显式置不透明，
     * 否则客体窗口与正确保留alpha的截图都只是透明色块。 */
    for(int y=0;y<dimensions[3];y++)for(int x=0;x<dimensions[2];x++)pixels[y*dimensions[2]+x]=0xFF000000u|(x<dimensions[2]/2?SC_RGB_PAPER:SC_RGB_INK);
    check(sc_frame32(window,pixels,size*4)==0,"true color submitted");sc_free(pixels);cli_text(1,"WINDOW ");cli_number(1,window);cli_text(1,"\n");
    int fd=sc_stream_open("/TMP/M9WIN",2,12);char number[12];decimal(number,window);if(fd<0)return 1;cli_write(fd,number,(u32)length(number));sc_stream_close(fd,1);
    u32 start=sc_tick();while((u32)sc_tick()-start<35000){int key=sc_key();if(key==1){int output=sc_stream_open("/TMP/M9KEY",2,3);if(output>=3){cli_write(output,"F1\n",3);sc_stream_close(output,1);}}
        if(key==27)break;sc_yield();}return failures;
}
int main(void)
{
    if(cli_parse()!=1)return 2;const char *mode=cli_argv[0];if(equal(mode,"abi"))return abi();if(equal(mode,"streams"))return streams();if(equal(mode,"simd"))return simd();if(equal(mode,"audio"))return audio();if(equal(mode,"gui"))return gui();
    if(equal(mode,"root") || equal(mode,"user"))return security(mode);if(equal(mode,"ticks")){cli_number(1,sc_tick());cli_text(1,"\n");return 0;}return 2;
}
