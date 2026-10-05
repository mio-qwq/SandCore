#include "SCIO.H"
#include "SCAUDIO.H"
/* 真实三环流式解码+seek/replay，专门链接许可已登记的dr_mp3。 */
static int file(const char *path,int tone)
{
    sc_audio_file music;int r=sc_audio_file_open(&music,path);if(r<0)return 1;if(music.kind!=2 || music.rate!=44100 || music.channels!=2){sc_audio_file_close(&music);return 2;}
    short pcm[4096];u32 total=0,nonzero=0;while((r=sc_audio_file_read(&music,pcm,2048))>0){total+=(u32)r;for(int i=0;i<r*2;i++)if(pcm[i])nonzero++;sc_yield();}
    if(r<0 || total!=80u*1152u || (tone?!nonzero:nonzero)){sc_audio_file_close(&music);return 3;}
    if(sc_audio_file_seek(&music,0)<0 || sc_audio_file_read(&music,pcm,2048)!=2048){sc_audio_file_close(&music);return 4;}
    if(sc_audio_file_seek(&music,1)<0 || sc_audio_file_read(&music,pcm,2048)!=2048){sc_audio_file_close(&music);return 5;}
    sc_audio_file_close(&music);cli_text(1,tone?"PASS MP3 tone decode frames replay seek\n":"PASS MP3 zero decode frames replay seek\n");return 0;
}
static int matrix(const char *path,int mode)
{
    sc_audio_file music;music.fd=-1;music.decoder=0;
    int r=sc_audio_file_open(&music,path);if(r<0)return mode==1?0:10;
    if(mode==1){sc_audio_file_close(&music);return 11;}
    short pcm[4098];pcm[4096]=1234;pcm[4097]=-2345;u32 frames=0,hash=2166136261u,start=sc_tick();
    u32 values[8]={0,0,(u32)music.rate,(u32)music.channels,(u32)music.bits,(u32)music.kind,0,0};
    if(sc_audio_file_read(&music,pcm,2049)>=0)return 12;
    while((r=sc_audio_file_read(&music,pcm,2048))>0){
        u8 *p=(u8 *)pcm;for(int i=0;i<r*4;i++)hash=(hash^p[i])*16777619u;
        if(mode==2 && cli_write(1,pcm,(u32)r*4)!=r*4)return 17;
        frames+=(u32)r;sc_yield();
    }
    if(r<0 || pcm[4096]!=1234 || pcm[4097]!=-2345)return 13;
    values[0]=frames;values[1]=hash;values[6]=sc_tick()-start;
    if(sc_audio_file_seek(&music,0)<0 || sc_audio_file_read(&music,pcm,2048)!=(int)(frames<2048?frames:2048))return 14;
    sc_audio_file_close(&music);if(music.fd!=-1 || music.decoder)return 15;
    if(mode==2)return 0;
    return cli_write(1,values,sizeof(values))==(int)sizeof(values)?0:16;
}
int main(void)
{
    if(cli_parse()<0)return 2;
    if(cli_argc==2 && (equal(cli_argv[0],"matrix") || equal(cli_argv[0],"reject") || equal(cli_argv[0],"dump")))
        return matrix(cli_argv[1],equal(cli_argv[0],"reject")?1:equal(cli_argv[0],"dump")?2:0);
    if(cli_argc)return 2;
    if(file("/SYS/TEST/ZERO.MP3",0) || file("/SYS/TEST/TONE.MP3",1))return 1;sc_audio_file bad;
    if(sc_audio_file_open(&bad,"/SYS/TEST/BAD.WAV")>=0){sc_audio_file_close(&bad);return 2;}
    cli_text(1,"PASS truncated RIFF rejected\n");return 0;
}
