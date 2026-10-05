#include "SCIO.H"
/* 客体原生编译的接口/资源探针：不用内核内部结构冒充用户实际能力。 */
static short samples[4096*2];
static int fail(const char *why){cli_text(2,why);cli_text(2,"\n");return 1;}
static int idle(void)
{
    u32 start=(u32)sc_tick(),info[16];
    while((u32)sc_tick()-start<500){
        sc_audio_info(info);
        if((u32)sc_tick()-start>30 && !info[11])return 0;
        sc_yield();
    }
    return -1;
}
int main(void)
{
    if(cli_parse()<1)return 2;const char *mode=cli_argv[0];u32 info[16];
    if(equal(mode,"info")){if(sc_audio_info(info)<0)return 1;return cli_write(1,info,64)==64?0:1;}
    if(equal(mode,"effect")){
        int kind;if(cli_argc!=2 || cli_integer(cli_argv[1],&kind)<0)return 2;
        if(idle()<0 || sc_audio_effect(kind)<0)return fail("effect rejected");
        /* 首批尚未提交时queue=0，先让过一个tick，再观察完整DMA消耗。 */
        u32 start=(u32)sc_tick();while((u32)sc_tick()-start<10)sc_yield();
        if(idle()<0)return fail("effect did not drain");
        sc_audio_info(info);return cli_write(1,info,64)==64?0:1;
    }
    if(equal(mode,"foreign")){
        int token;if(cli_argc!=2 || cli_integer(cli_argv[1],&token)<0)return 2;
        if(sc_audio_status((u32)token,info)>=0 || sc_audio_control((u32)token,0,0)>=0)
            return fail("foreign voice accessible");
        if(sc_audio_open(48000,2)!=-4)return fail("UID voice quota bypass");
        cli_text(1,"PASS foreign voice and shared UID quota denied\n");return 0;
    }
    if(equal(mode,"bounds")){
        if(sc_audio_open(7999,2)>=0 || sc_audio_open(192001,2)>=0 || sc_audio_open(48000,0)>=0
           || sc_audio_open(48000,3)>=0)return fail("invalid PCM accepted");
        int first=sc_audio_open(48000,2),second=sc_audio_open(24000,1);
        if(first<=0 || second<=0 || first==second || sc_audio_open(48000,2)!=-4)return fail("voice quota");
        if(sc_audio_submit((u32)first,samples,4097)>=0 || sc_audio_control((u32)first,2,257)>=0
            || sc_audio_control((u32)first,99,0)>=0)return fail("invalid voice control accepted");
        char command[128],numbered[16];decimal(numbered,first);
        copy(command,"/TMP/SOUNDBOUNDS.SCX foreign ",sizeof(command));append(command,numbered,sizeof(command));
        int descriptors[3]={0,1,2};int job=sc_spawn2(command,descriptors,0);if(job<=0)return fail("foreign child did not start");
        u32 completion[8];int result;while((result=sc_wait2((u32)job,completion))>0)sc_yield();
        if(result<0 || completion[2])return fail("foreign child failed");
        if(sc_audio_control((u32)first,1,1)<0 || sc_audio_submit((u32)first,samples,4096)!=4096)return fail("paused submit");
        u32 start=(u32)sc_tick();while((u32)sc_tick()-start<50)sc_yield();
        if(sc_audio_status((u32)first,info)<0 || info[3]!=4096 || info[7] || !info[5])return fail("pause consumed PCM");
        if(sc_audio_control((u32)first,3,0)<0 || sc_audio_status((u32)first,info)<0 || info[3] || info[7])return fail("flush did not clear");
        if(sc_audio_control((u32)first,0,0)<0 || sc_audio_status((u32)first,info)>=0
           || sc_audio_control((u32)first,0,0)>=0)return fail("stale voice token accepted");
        int third=sc_audio_open(48000,2);if(third<=0 || third==first)return fail("voice token reused");
        if(sc_audio_control((u32)second,0,0)<0 || sc_audio_control((u32)third,0,0)<0)return fail("voice close");
        cli_text(1,"PASS voice parameters quota pause flush stale token reclaim\n");return 0;
    }
    if(equal(mode,"leave")){
        int first=sc_audio_open(48000,2),second=sc_audio_open(48000,2);
        if(first<=0 || second<=0)return fail("exit probe quota not reclaimed");
        cli_text(1,"PASS exit probe left two owned voices\n");return 0;
    }
    return 2;
}
