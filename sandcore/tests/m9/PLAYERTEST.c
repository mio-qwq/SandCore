#include "SCIO.H"
#include "SCAUDIO.H"
#include "cover.h"
static u32 digest(u32 hash,const void *body,u32 bytes)
{const u8 *p=body;for(u32 i=0;i<bytes;i++)hash=(hash^p[i])*16777619u;return hash;}
static int decode(const char *path,int bad)
{
    sc_audio_file source={0},music={0};source.fd=music.fd=-1;
    int r=sc_audio_file_open(&source,path);
    if(r<0){if(bad){cli_text(1,"PASS corrupt FLAC rejected\n");return 0;}return 1;}
    if(source.kind!=3)return 2;
    sc_audio_file_move(&music,&source);
    /* 候选对象已清空且可离开；对旧栈地址的回调会立即读到无效fd。 */
    short pcm[4098];pcm[4096]=1234;pcm[4097]=-2345;
    if(sc_audio_file_read(&music,pcm,0)!=0 || music.pcm_position || sc_audio_file_read(&music,pcm,2049)>=0)return 3;
    u32 hash=2166136261u,frames=0;
    while((r=sc_audio_file_read(&music,pcm,2048))>0){hash=digest(hash,pcm,(u32)r*4);frames+=(u32)r;sc_yield();}
    if(pcm[4096]!=1234 || pcm[4097]!=-2345)return 4;
    if(bad){sc_audio_file_close(&music);if(r<0){cli_text(1,"PASS corrupt FLAC rejected\n");return 0;}return 5;}
    if(r<0 || (music.pcm_total && frames!=music.pcm_total))return 6;
    u32 out[8]={frames,hash,(u32)music.rate,(u32)music.channels,(u32)music.bits,0,0,0};
    if(sc_audio_file_seek(&music,0)<0 || (r=sc_audio_file_read(&music,pcm,2048))<=0)return 7;
    out[5]=digest(2166136261u,pcm,(u32)r*4);
    if(sc_audio_file_seek(&music,1)<0 || (r=sc_audio_file_read(&music,pcm,2048))<=0)return 8;
    out[6]=digest(2166136261u,pcm,(u32)r*4);
    out[7]=(u32)r;
    if(sc_audio_file_seek(&music,0xFFFFFFFFu)>=0)return 9;
    sc_audio_file_close(&music);if(music.fd!=-1 || music.decoder)return 10;
    return cli_write(1,out,sizeof(out))==(int)sizeof(out)?0:11;
}
static int art(const char *path)
{
    sc_album_art album={0};if(sc_album_begin(&album,path)<0)return 20;
    u32 start=(u32)sc_tick();int r;
    while((r=sc_album_step(&album))==1 && (u32)sc_tick()-start<2000)sc_yield();
    if(r || !album.image.pixels || !sc_album_current(&album,path))return 21;
    cli_text(1,album.title);cli_text(1,"\n");cli_text(1,album.artist);cli_text(1,"\n");
    u32 words[3]={album.image.info[1],album.image.info[2],digest(2166136261u,album.image.pixels,album.image.info[3])};
    cli_write(1,words,sizeof(words));sc_album_close(&album);
    return album.image.pixels || album.image.ticket || album.temporary[0]?22:0;
}
static int placement(void)
{
    int w=sc_open_rgb("M9 place",240,180),old=sc_open("M9 old",96,80),info[5];if(w<0 || old<0)return 30;
    if(sc_window_place(old,0,0,200,100)>=0 || sc_window_place(w,0,0,93,100)>=0 || sc_window_place(w,0,0,200,39)>=0
        || sc_window_place(w,0,0,0x7FFFFFFF,100)>=0 || sc_window_place(w,0,0,200,0x7FFFFFFF)>=0)return 31;
    if(sc_window_place(w,-100,-100,200,100)<0 || sc_info(w,info)<0 || info[0] || info[1] || info[2]!=200 || info[3]!=100)return 32;
    if(sc_window_place(w,0x7FFFFFFF,0x7FFFFFFF,300,120)<0 || sc_info(w,info)<0 || info[0]<0 || info[1]<0 || info[2]!=300 || info[3]!=120)return 33;
    cli_text(1,"PASS native place bounds clamp legacy reject\n");return 0;
}
int main(void)
{
    if(cli_parse()<0)return 2;if(cli_argc==1 && equal(cli_argv[0],"place"))return placement();
    if(cli_argc!=2)return 2;
    if(equal(cli_argv[0],"decode"))return decode(cli_argv[1],0);
    if(equal(cli_argv[0],"bad"))return decode(cli_argv[1],1);
    if(equal(cli_argv[0],"art"))return art(cli_argv[1]);return 2;
}
