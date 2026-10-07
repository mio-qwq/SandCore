#include "../SCIO.H"
#include "../SCAUDIO.H"
#ifndef AUDIO_CLI
#include "../NUI.inc"
#include "cover.h"
#endif
static sc_audio_file music;
static short pcm[4096];
static u32 audio_token;
static u32 playback_base;
static int paused,eof,finished,volume=224,error_code;
static char filename[128],status_text[96];
static int peaks[64],peak_cursor;
#ifndef AUDIO_CLI
static sc_album_art album;
static u32 *cover_scaled,cover_revision,cover_cached_revision,cover_theme;
static int cover_width,cover_height,mini,restore_geometry[5];
#endif
static int start(const char *path)
{
    if(length(path)>=128)return -1;sc_audio_file candidate={0};candidate.fd=-1;
    int result=sc_audio_file_open(&candidate,path);if(result<0)return result;
    result=sc_audio_open((u32)candidate.rate,2);if(result<0){sc_audio_file_close(&candidate);return result;}
    /* 新文件及声道都建立后才换掉旧曲目；选错文件不丢失旧播放状态。 */
    if(audio_token)sc_audio_control(audio_token,0,0);sc_audio_file_move(&music,&candidate);
    audio_token=(u32)result;paused=eof=finished=0;playback_base=0;peak_cursor=0;
#ifndef AUDIO_CLI
    if(!sc_album_current(&album,path)){
        sc_album_begin(&album,path);cover_revision++;
    }
#endif
    for(int i=0;i<64;i++)peaks[i]=0;sc_audio_control(audio_token,2,(u32)volume);return 0;
}
static int fill(void)
{
    if(!audio_token || paused || eof)return 0;u32 info[16];if(sc_audio_status(audio_token,info)<0)return -1;
    if(info[4]<2048)return 0;
    int count=sc_audio_file_read(&music,pcm,2048);if(count<0)return count;
    if(!count){eof=1;sc_audio_control(audio_token,4,0);return 0;}
    int peak=0;for(int i=0;i<count*2;i++){int v=pcm[i];if(v<0)v=-v;if(v>peak)peak=v;}
    peaks[peak_cursor++&63]=peak;
    int result=sc_audio_submit(audio_token,pcm,(u32)count);return result==count?0:-1;
}
static int seek_relative(int seconds)
{
    if(!audio_token)return -1;u32 info[16];if(sc_audio_status(audio_token,info)<0)return -1;
    int position=(int)((playback_base+info[7])/(u32)music.rate)+seconds;if(position<0)position=0;
    int result=sc_audio_file_seek(&music,(u32)position);if(result<0)return result;
    sc_audio_control(audio_token,3,0);eof=finished=0;playback_base=(u32)position*(u32)music.rate;sc_audio_control(audio_token,1,(u32)paused);return 0;
}
#ifndef AUDIO_CLI
static u32 blend(u32 a,u32 b,int fraction)
{
    u32 out=0;for(int shift=0;shift<24;shift+=8){int x=(int)((a>>shift)&255),y=(int)((b>>shift)&255);
        out|=(u32)(x+(y-x)*fraction/256)<<shift;}return out;
}
static void cover(int x,int y,int width,int height)
{
    int w=ui_px(width),h=ui_px(height);if(w<1 || h<1)return;
    if(!cover_scaled || cover_width!=w || cover_height!=h || cover_cached_revision!=cover_revision || cover_theme!=ui_theme[2]){
        if(cover_scaled)sc_free(cover_scaled);cover_scaled=sc_alloc((u32)w*h*4);
        cover_width=w;cover_height=h;cover_cached_revision=cover_revision;cover_theme=ui_theme[2];
        if(cover_scaled){
            u32 paper=ui_role(SC_THEME_FACE_ALT),accent=ui_role(SC_THEME_ACCENT),gold=ui_role(SC_THEME_GOLD);
            u32 sw=album.image.info[1],sh=album.image.info[2],cropw=sw,croph=sh;
            if(album.image.pixels){if(sw*(u32)h>sh*(u32)w)cropw=sh*(u32)w/(u32)h;else croph=sw*(u32)h/(u32)w;}
            for(int row=0;row<h;row++)for(int col=0;col<w;col++){
                u32 rgb;
                if(album.image.pixels && sw && sh){
                    u32 sx=(sw-cropw)/2+(u32)col*cropw/(u32)w,sy=(sh-croph)/2+(u32)row*croph/(u32)h;
                    u32 pixel=album.image.pixels[sy*sw+sx];int alpha=(int)(pixel>>24);
                    rgb=alpha==255?pixel&0xFFFFFFu:blend(paper,pixel,alpha);
                }else{
                    /* 原创无封面唱片：只用既有主题角色，层次/渐变一次缓存。
                     * 两主题保留各自表面与边框，不另造未经登记的裸色。 */
                    int px=col*256/w-128,py=row*256/h-128,radius=px*px+py*py;
                    rgb=blend(paper,accent,(col+row)*90/(w+h));
                    if(radius<96*96){rgb=blend(ui_role(SC_THEME_TEXT),accent,48+(radius/256)%28);
                        if(radius<30*30)rgb=gold;if(radius<6*6)rgb=paper;}
                }
                cover_scaled[row*w+col]=0xFF000000u|rgb;
            }
        }
    }
    if(!cover_scaled)return;int px=ui_px(x),py=ui_px(y);
    for(int row=0;row<h;row++)if(py+row>=0 && py+row<ui_height){
        int left=px<0?-px:0,right=w;if(px+right>ui_width)right=ui_width-px;
        if(right>left)sc_mem_copy(ui_pixels+(py+row)*ui_width+px+left,cover_scaled+row*w+left,(u32)(right-left)*4);
    }
}
static int toggle_mini(void)
{
    if(!mini){
        if(sc_info(ui_win,restore_geometry)<0)return -1;
        if(sc_window_place(ui_win,ui_px(10),0x7FFFFFFF,ui_px(360),ui_px(112))<0)return -1;
        mini=1;
    }else{
        if(sc_window_place(ui_win,restore_geometry[0],restore_geometry[1],restore_geometry[2],restore_geometry[3])<0)return -1;
        mini=0;
    }
    ui_geometry();ui_followup=1;return 0;
}
static void draw(void)
{
    if(mini){
        ui_background(ui_classic?SC_THEME_FACE:SC_THEME_PAPER);
        cover(10,10,84,84);
        ui_clip_set(108,10,UI_W-120,42);
        ui_text(108,10,album.title[0]?album.title:"SandAudio",PAL_UI_TEXT);
        ui_text(108,32,album.artist[0]?album.artist:"MP3 / WAV / FLAC",PAL_UI_MUTED);ui_clip_clear();
        ui_small_control(1,108,64,70,finished?"Replay":paused?"Play":"Pause",paused);
        ui_small_control(7,190,64,74,"Expand",0);
        if(UI_W>320)ui_small_control(6,274,64,70,"Open",0);
        return;
    }
    ui_header("SandAudio","MP3 / WAV / FLAC  |  your local listening room");
    if(UI_W>=640 && UI_H>=450){
        int side=UI_H-166;if(side>UI_W*2/3-40)side=UI_W*2/3-40;if(side<180)side=180;
        int top=82,right=side+56,column=UI_W-right-24;
        ui_round_rgb(20,top+4,side+8,side+8,ui_radius,ui_role(SC_THEME_LINE));cover(24,top+8,side,side);
        ui_clip_set(right,top+14,column,92);
        ui_text(right,top+14,album.title[0]?album.title:"Choose a record",PAL_UI_TEXT);
        ui_text(right,top+46,album.artist[0]?album.artist:"Local library",PAL_UI_MUTED);
        ui_text(right,top+78,status_text,PAL_UI_MUTED);ui_clip_clear();
        ui_span(right,top+124,column,PAL_UI_LINE);
        ui_control(1,right,top+150,120,finished?"Replay":paused?"Resume":"Pause",paused);
        ui_small_control(6,right+132,top+154,78,"Open",0);
        ui_small_control(2,right,top+196,78,"-10s",0);ui_small_control(3,right+92,top+196,78,"+10s",0);
        ui_small_control(4,right,top+234,78,"Vol -",0);ui_small_control(5,right+92,top+234,78,"Vol +",0);
        ui_small_control(7,right,top+290,100,"Mini",0);ui_small_control(8,right+112,top+290,100,"Full view",0);
        int y=top+side-38;if(y>top+342){
            int step=column/64;if(step<1)step=1;
            for(int i=0;i<64;i++){int h=peaks[(peak_cursor+i)&63]*48/32768;if(h<2)h=2;
                ui_rect(right+i*step,y+48-h,step>2?step-2:1,h,PAL_UI_CYAN+4+(i&3));}}
        ui_clip_set(right,UI_H-64,column,20);ui_text(right,UI_H-64,filename[0]?filename:"Enter a path and press Enter",PAL_UI_MUTED);ui_clip_clear();
        ui_footer("Space play  M mini  F full view  O open  R replay");return;
    }
    int y=ui_compact?38:78;ui_clip_set(16,y,UI_W-32,42);ui_text(16,y,filename[0]?filename:"Enter a path and press Enter",PAL_UI_TEXT);
    ui_text(16,y+22,status_text,PAL_UI_MUTED);ui_clip_clear();y+=56;
    int bottom=UI_H-(ui_compact?60:80),height=bottom-y-46;if(height<12)height=12;
    int width=UI_W-32,step=width/64;if(step<1)step=1;
    for(int i=0;i<64;i++){int p=peaks[(peak_cursor+i)&63],h=p*height/32768;
        if(h<2)h=2;ui_rect(16+i*step,y+height-h,step>2?step-2:1,h,PAL_UI_CYAN+4+(i&3));}
    y+=height+12;
    ui_small_control(1,16,y,104,finished?"Replay":paused?"Resume":"Pause",paused);
    if(UI_W>330){ui_small_control(2,132,y,78,"-10s",0);ui_small_control(3,222,y,78,"+10s",0);}
    if(UI_W>490){ui_small_control(4,312,y,70,"Vol -",0);ui_small_control(5,394,y,70,"Vol +",0);}
    if(UI_W>590)ui_small_control(6,476,y,78,"Open",0);
    if(UI_W>410)ui_small_control(7,UI_W-100,ui_compact?6:16,84,"Mini",0);
    ui_footer("Space play  M mini  O open  R replay  [ / ] seek");
}
#endif
int main(void)
{
    music.fd=-1;if(cli_parse()<0 || cli_argc>1)return 2;
    if(cli_argc){if(length(cli_argv[0])>=128)return 2;copy(filename,cli_argv[0],sizeof(filename));}
#ifdef AUDIO_CLI
    if(!filename[0])return cli_error("soundplay: path.mp3, path.wav or path.flac",-1);
#else
    if(ui_open("SandAudio / mio")<0)return 1;ui_followup=1;
    ui_background_events=SC_EVENT_AUDIO|SC_EVENT_IMAGE;ui_background_interval=1;
#endif
    if(filename[0]){error_code=start(filename);if(error_code<0)copy(status_text,"Unable to open audio",sizeof(status_text));}
    u32 last_draw=0xFFFFFFFFu,drain_tick=0xFFFFFFFFu;
    for(;;){
        if(audio_token && !error_code){int result=fill();if(result<0){error_code=result;copy(status_text,"Decoder or stream error",sizeof(status_text));}}
#ifdef AUDIO_CLI
        if(error_code<0){sc_audio_file_close(&music);return cli_error("soundplay",error_code);}
        u32 info[16];if(sc_audio_status(audio_token,info)<0){sc_audio_file_close(&music);return 1;}
        if(eof && !info[3]){sc_audio_control(audio_token,0,0);sc_audio_file_close(&music);return 0;}sc_yield();
#else
        if(album.image.ticket){int result=sc_album_step(&album);if(result!=1){cover_revision++;ui_followup=1;}}
        u32 sec=(u32)sc_tick()/100;
        if(audio_token && eof && !finished && !error_code && (u32)sc_tick()!=drain_tick){u32 info[16];drain_tick=(u32)sc_tick();
            if(sc_audio_status(audio_token,info)==0 && !info[3]){finished=1;ui_followup=1;}}
        if(audio_token && !paused && !finished && !error_code && sec!=last_draw)ui_followup=1;
        if(!ui_frame_due_event()){sc_yield();continue;}ui_pointer();
        last_draw=sec;
        if(audio_token && !error_code){u32 info[16];if(sc_audio_status(audio_token,info)==0){
            char number[16];copy(status_text,finished?"Finished  ":paused?"Paused  ":"Playing  ",sizeof(status_text));
            decimal(number,(int)((playback_base+info[7])/(u32)music.rate));append(status_text,number,sizeof(status_text));append(status_text," s  /  ",sizeof(status_text));
            decimal(number,music.rate);append(status_text,number,sizeof(status_text));append(status_text," Hz",sizeof(status_text));}}
        draw();ui_present();int key=sc_key();
        if(key==27)break;
        if(((key=='m' || key=='M') && audio_token && !error_code) || ui_action==7){toggle_mini();continue;}
        if(((key=='f' || key=='F') && audio_token && !error_code) || ui_action==8){if(mini)toggle_mini();sc_window(ui_win,1);ui_geometry();ui_followup=1;continue;}
        if(key=='o' || key=='O' || ui_action==6){char path[128];copy(path,filename,128);int previous_pause=paused,return_mini=mini;
            /* 目录浏览器需要完整工作区；迷你客户区不能裁掉保存/取消
             * 按钮。临时展开同一窗口，选曲或取消后恢复左下角模式。 */
            if(return_mini)toggle_mini();
            if(audio_token)sc_audio_control(audio_token,1,1);
            if(ui_edit_path(path,sizeof(path),"Open audio")){
                /* NUI目录浏览器交回的是历史FS根相对路径。流式接口
                 * 按cwd解释，先显式加根斜杠，不能再拼一次HOME。 */
                char rooted[128];rooted[0]=0;if(path[0]!='/')copy(rooted,"/",128);append(rooted,path,128);
                int result=length(path)+(path[0]!='/'?1:0)>=128?-1:start(rooted);
                if(result>=0){copy(filename,rooted,128);error_code=0;}
                else {if(!audio_token)error_code=result;copy(status_text,"Unable to open selected audio",sizeof(status_text));
                    if(audio_token)sc_audio_control(audio_token,1,(u32)previous_pause);}
            }else if(audio_token)sc_audio_control(audio_token,1,(u32)previous_pause);
            if(return_mini)toggle_mini();ui_followup=1;continue;
        }
        if(!audio_token || error_code<0){
            int n=length(filename);
            if(key==8 && n){filename[n-1]=0;ui_followup=1;}
            else if(key>=32 && key<127 && n<127){filename[n]=(char)key;filename[n+1]=0;ui_followup=1;}
            else if(key=='\n' && filename[0]){error_code=start(filename);ui_followup=1;}
        }else {
            if(key=='r' || key=='R' || ((key==' ' || ui_action==1) && finished)){error_code=start(filename);ui_followup=1;}
            else if(key==' ' || ui_action==1){paused=!paused;sc_audio_control(audio_token,1,(u32)paused);ui_followup=1;}
            if(ui_action==2 || ui_action==3 || key=='[' || key==']'){int result=seek_relative(ui_action==2 || key=='['?-10:10);
                if(result<0)copy(status_text,"Seek is outside this file",sizeof(status_text));ui_followup=1;}
            if(ui_action==4 || ui_action==5 || key=='-' || key=='+' || key=='='){volume+=ui_action==4 || key=='-'?-16:16;if(volume<0)volume=0;if(volume>256)volume=256;
                sc_audio_control(audio_token,2,(u32)volume);ui_followup=1;}
        }
#endif
    }
    if(audio_token)sc_audio_control(audio_token,0,0);sc_audio_file_close(&music);
#ifndef AUDIO_CLI
    sc_album_close(&album);if(cover_scaled)sc_free(cover_scaled);
#endif
    return 0;
}
