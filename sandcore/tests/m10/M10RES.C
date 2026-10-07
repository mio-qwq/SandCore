#include "SCIO.H"
/* 补关联全局对象的真实数量/正文/回收检查，不改变旧M9配额夹具。
 * 临时文件只在PID/代数组成的新目录，拒绝覆盖已存在目录；窗口
 * 属于本人的登录会话，不为了测试把SYSTEM界面放入普通桌面。 */
static int failures;
static void check(int okay,const char *message)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,message);cli_text(1,"\n");if(!okay)failures++;}
static void metric(const char *label,u32 value)
{cli_text(1,label);cli_number(1,(int)value);cli_text(1,"\n");}
static int free_pages(u32 *out)
{
    u32 memory[50];memory[48]=0xA1010001;memory[49]=0xA1010002;
    if(sc_monitor(memory)<0 || memory[48]!=0xA1010001 || memory[49]!=0xA1010002)return -1;
    *out=memory[4]/4096u;return 0;
}
static int pixel_read(u32 token,u32 expected)
{
    u32 pixels[10];pixels[0]=0xA1010003;pixels[9]=0xA1010004;
    if(sc_capture_read(token,0,pixels+1,32)!=32 || pixels[0]!=0xA1010003 || pixels[9]!=0xA1010004)return 0;
    for(int i=1;i<9;i++)if(pixels[i]!=expected)return 0;return 1;
}
static void close_window(int window)
{sc_call(0x11,window,0,0,0,0,0);}
static int graphics(void)
{
    u32 cold,before,peak,after,theme[32];if(free_pages(&cold)<0 || sc_theme(theme)<0)return 1;
    /* 首次进入本会话的view对象属于会话缓存，不能冒充窗口泄漏，也
     * 不能藏掉成本：单列冷起点，预热后再核本组显式窗口/截图归还。 */
    int warm=sc_open_rgb("Resource QA",128,80);u32 warm_visibility[8];
    if(warm<0)return 1;int ready=sc_visibility(warm,warm_visibility);close_window(warm);
    if(ready<0 || free_pages(&before)<0)return 1;metric("free_pages_cold=",cold);
    int windows[18],opened=0;
    for(;opened<18;opened++){windows[opened]=sc_open_rgb("Resource QA",opened==17?1920:128,opened==17?1080:80);if(windows[opened]<0)break;
        u32 visibility[8];if(sc_visibility(windows[opened],visibility)<0){opened++;break;}}
    check(opened==18,"18 independent native windows exceed old global six");
    if(opened!=18){for(int i=0;i<opened;i++)if(windows[i]>=0)close_window(windows[i]);return 1;}
    int dimensions[5];u32 visibility[8];int window=windows[17];
    if(sc_info(window,dimensions)<0 || sc_visibility(window,visibility)<0 || dimensions[2]<1 || dimensions[3]<1
        || dimensions[2]>1920 || dimensions[3]>1080){for(int i=0;i<opened;i++)close_window(windows[i]);return 1;}
    u32 bytes=(u32)dimensions[2]*(u32)dimensions[3]*4u,expected=0xFF000000u|theme[8+SC_THEME_FACE];
    /* 直接服从内核可见性。隐藏会话只读已初始化的画布快照，不绘制
     * 测试帧、不切换会话；可见时用已登记SC_RGB_PAPER提交原生像素。 */
    if(visibility[1]){
        u32 *pixels=sc_alloc(bytes);if(!pixels){for(int i=0;i<opened;i++)close_window(windows[i]);return 1;}
        expected=0xFF000000u|SC_RGB_PAPER;for(u32 i=0;i<bytes/4;i++)pixels[i]=expected;
        check(sc_frame32(window,pixels,bytes)==0,"visible native reference pixels submitted");sc_free(pixels);
    }
    u32 amount=(64u*1024u*1024u)/bytes+1u,*tokens=sc_alloc(amount*sizeof(u32));
    if(!tokens){for(int i=0;i<opened;i++)close_window(windows[i]);return 1;}
    u32 captured=0;int okay=1;u32 header[10];
    for(;captured<amount;captured++){
        header[0]=0xA1010005;header[9]=0xA1010006;int token=sc_capture_open(window,header+1);if(token<=0)break;
        tokens[captured]=(u32)token;
        if(header[0]!=0xA1010005 || header[9]!=0xA1010006 || header[1]!=1 || header[2]!=(u32)window || header[6]!=2
            || header[7]!=bytes || !pixel_read((u32)token,expected))okay=0;
    }
    metric("windows=",(u32)opened);metric("captures=",captured);metric("capture_bytes=",captured*bytes);
    check(captured==amount && captured>8 && captured*bytes>64u*1024u*1024u && okay,"live immutable snapshots exceed eight and 64MiB with exact pixel bytes");
    if(free_pages(&peak)<0){check(0,"snapshot peak PF measurement");peak=before;}
    metric("free_pages_before=",before);metric("free_pages_peak=",peak);
    close_window(window);windows[17]=-1;
    check(sc_info(window,dimensions)<0 && (!captured || pixel_read(tokens[0],expected)),"closing window preserves independent immutable snapshot");
    if(captured){u32 stale=tokens[0];check(sc_capture_close(stale)==0,"first snapshot explicitly closes");tokens[0]=0;
        u32 pixel;check(sc_capture_read(stale,0,&pixel,4)<0 && sc_capture_close(stale)<0,"closed snapshot token cannot be reused");
        int token=sc_capture_open(windows[0],header+1);if(token>0)tokens[0]=(u32)token;
        check(token>0 && (u32)token!=stale,"replacement snapshot gets a distinct ticket");}
    okay=1;for(u32 i=0;i<captured;i++)if(tokens[i] && sc_capture_close(tokens[i])<0)okay=0;
    check(okay,"all remaining snapshots explicitly reclaim");sc_free(tokens);
    for(int i=0;i<opened;i++)if(windows[i]>=0)close_window(windows[i]);
    okay=1;for(int i=0;i<opened;i++)if(windows[i]>=0 && sc_info(windows[i],dimensions)>=0)okay=0;
    check(okay,"all original window handles are invalid after close");
    if(free_pages(&after)<0)return 1;metric("free_pages_after=",after);
    check(after==before,"explicit window capture and heap pages restore exact PF baseline");return failures?1:0;
}
static void file_path(const char *directory,int index,char out[64])
{char text[12];copy(out,directory,64);append(out,"/T",64);decimal(text,index);append(out,text,64);}
static int transactions(void)
{
    u32 self[8],info[2];if(sc_process_self(self)<0)return 1;
    char directory[64],text[12],path[64];copy(directory,"/TMP/M10R-",sizeof(directory));decimal(text,(int)self[1]);append(directory,text,sizeof(directory));
    append(directory,"-",sizeof(directory));decimal(text,(int)self[2]);append(directory,text,sizeof(directory));
    if(sc_stat(directory,info)>=0 || sc_mkdir(directory)<0)return cli_error("exclusive fixture directory",-17);
    int fds[12],prepared=0,active=0,okay=1;for(int i=0;i<12;i++)fds[i]=-1;
    u8 original[4]={0x31,0,0xA5,0x7F},replacement[4]={0x42,0,0x5A,0x80},bytes[4];
    for(;prepared<12;prepared++){file_path(directory,prepared,path);if(sc_write(path,original,4)!=4)break;}
    for(int i=0;i<prepared;i++){file_path(directory,i,path);fds[i]=sc_stream_open(path,2,512);if(fds[i]<0)break;
        active++;if(sc_stream_write(fds[i],replacement,4)!=4)okay=0;}
    for(int i=0;i<prepared;i++){file_path(directory,i,path);if(sc_read(path,bytes,4)!=4)okay=0;
        else for(int j=0;j<4;j++)if(bytes[j]!=original[j])okay=0;}
    metric("transactions=",(u32)active);check(prepared==12 && active==12 && okay,"12 simultaneous write transactions preserve original targets");
    okay=1;for(int i=0;i<active;i++){
        int commit=i&1,result=sc_stream_close(fds[i],commit);if(result<0){okay=0;sc_stream_close(fds[i],0);}fds[i]=-1;
        file_path(directory,i,path);if(sc_read(path,bytes,4)!=4)okay=0;
        else for(int j=0;j<4;j++)if(bytes[j]!=(commit?replacement[j]:original[j]))okay=0;
    }
    check(okay,"alternating commit abort returns exact binary targets");
    for(int i=0;i<prepared;i++){file_path(directory,i,path);if(sc_remove(path)<0)okay=0;}
    if(sc_remove(directory)<0)okay=0;check(okay,"all fixture transactions and files clean up");return failures?1:0;
}
int main(void)
{
    if(cli_parse()<0 || cli_argc!=1)return 2;
    if(equal(cli_argv[0],"graphics"))return graphics();
    if(equal(cli_argv[0],"transactions"))return transactions();return 2;
}
