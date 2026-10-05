#include "../SCIO.H"
#include "../IMAGECLIENT.inc"
#include "cover.h"
/* 封面元数据是独立的可选输入：坏封面不破坏已确认的音频流。
 * 大图解码复用已有三环IMAGE服务，不在内核/播放器重复塞一套解码器。
 * 只在换曲时读取元数据、建立私有临时源，画面刷新不反复写盘/解码。 */
#define COVER_LIMIT (1024u*1024u)
static u32 sequence;
static u32 big(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static u32 little(const u8 *p){return (u32)p[3]<<24|(u32)p[2]<<16|(u32)p[1]<<8|p[0];}
static u32 syncsafe(const u8 *p){return (u32)p[0]<<21|(u32)p[1]<<14|(u32)p[2]<<7|p[3];}
static int field(const u8 *p,u32 size,const char *key)
{u32 n=(u32)length(key);if(size<n)return 0;for(u32 i=0;i<n;i++){u8 c=p[i];if(c>='a' && c<='z')c-=32;if(c!=(u8)key[i])return 0;}return 1;}
static int exact(int fd,u32 at,void *out,u32 bytes,u32 end)
{
    if(at>end || bytes>end-at || sc_stream_seek(fd,at)<0)return -1;
    u32 used=0;while(used<bytes){u32 part=bytes-used;if(part>65536)part=65536;
        int n=cli_read(fd,(u8 *)out+used,part);if(n<=0)return -1;used+=(u32)n;}return 0;
}
static void text(char *out,const u8 *input,u32 size,int encoding)
{
    u32 at=0;int n=0,bigend=encoding==2;
    if(encoding==1 && size>=2){bigend=input[0]==0xFE && input[1]==0xFF;if(bigend || (input[0]==0xFF && input[1]==0xFE))at=2;}
    while(at<size && n<123){
        u32 c=input[at++];
        if(encoding==1 || encoding==2){
            if(at==size)break;c=bigend?(c<<8)|input[at]:c|((u32)input[at]<<8);at++;
            if(c>=0xD800 && c<=0xDBFF){
                if(size-at<2)break;u32 next=bigend?((u32)input[at]<<8)|input[at+1]:input[at]|((u32)input[at+1]<<8);at+=2;
                c=next>=0xDC00 && next<=0xDFFF?0x10000+((c-0xD800)<<10)+(next-0xDC00):'?';
            }else if(c>=0xDC00 && c<=0xDFFF)c='?';
        }
        if(!c)break;if(c<32)c=' ';
        if(encoding==3){
            int width=c<128?1:c>=0xC2 && c<=0xDF?2:c>=0xE0 && c<=0xEF?3:c>=0xF0 && c<=0xF4?4:0;
            if(!width || size-at<(u32)(width-1)){out[n++]='?';continue;}
            int valid=1;for(int k=0;k<width-1;k++)if((input[at+k]&192)!=128)valid=0;
            if(!valid){out[n++]='?';continue;}if(n+width>127)break;
            out[n++]=(char)c;for(int k=0;k<width-1;k++)out[n++]=(char)input[at++];continue;
        }
        if(c<128)out[n++]=(char)c;
        else if(c<2048){out[n++]=(char)(0xC0|(c>>6));out[n++]=(char)(0x80|(c&63));}
        else if(c<65536){out[n++]=(char)(0xE0|(c>>12));out[n++]=(char)(0x80|((c>>6)&63));out[n++]=(char)(0x80|(c&63));}
        else {out[n++]=(char)(0xF0|(c>>18));out[n++]=(char)(0x80|((c>>12)&63));out[n++]=(char)(0x80|((c>>6)&63));out[n++]=(char)(0x80|(c&63));}
    }
    /* UTF8显示副本在完整标量边界停，不能因127B预算截断汉字。 */
    out[n]=0;
}
static int extract(sc_album_art *art,int source,u32 at,u32 size,u32 end)
{
    if(!size || size>COVER_LIMIT || at>end || size>end-at)return -1;
    u32 self[8];if(sc_process_self(self)<0)return -1;char a[12],b[12],c[12];
    decimal(a,(int)self[1]);decimal(b,(int)self[2]);decimal(c,(int)++sequence);
    copy(art->temporary,"/TMP/SC-COVER-",64);append(art->temporary,a,64);append(art->temporary,"-",64);
    append(art->temporary,b,64);append(art->temporary,"-",64);append(art->temporary,c,64);append(art->temporary,".BIN",64);
    if(sc_create_ex(art->temporary,1,3)<0){art->temporary[0]=0;return -1;}
    int fd=sc_stream_open(art->temporary,2,0),result=-1;
    if(fd>=0){
        u8 buffer[4096];u32 used=0;
        while(used<size){u32 chunk=size-used;if(chunk>sizeof(buffer))chunk=sizeof(buffer);
            if(exact(source,at+used,buffer,chunk,end)<0 || cli_write(fd,buffer,chunk)!=(int)chunk)break;
            used+=chunk;sc_yield();}
        result=sc_stream_close(fd,used==size?1:0);
        if(used!=size)result=-1;
    }
    if(result>=0)result=image_client_begin(&art->image,art->temporary);
    if(result<0){sc_remove(art->temporary);art->temporary[0]=0;}
    return result;
}
static int flac(sc_album_art *art,int fd,u32 end)
{
    u32 at=4;int last=0,selected=-1;u32 picture=0,picture_size=0;
    for(int block=0;block<128 && !last && at<=end && end-at>=4;block++){
        u8 h[8];if(exact(fd,at,h,4,end)<0)return -1;
        last=h[0]&128;int kind=h[0]&127;u32 size=(u32)h[1]<<16|(u32)h[2]<<8|h[3];at+=4;
        if(size>end-at)return -1;u32 limit=at+size;
        if(kind==6 && size>=32){
            u32 p=at;if(exact(fd,p,h,8,limit)<0)return -1;u32 type=big(h),mime=big(h+4);p+=8;
            if(mime>limit-p || mime>127)return -1;p+=mime;
            if(exact(fd,p,h,4,limit)<0)return -1;u32 description=big(h);p+=4;
            if(description>limit-p)return -1;p+=description;
            u8 fields[20];if(exact(fd,p,fields,20,limit)<0)return -1;p+=20;
            u32 bytes=big(fields+16);if(bytes>limit-p)return -1;
            if(bytes && bytes<=COVER_LIMIT && (selected<0 || type==3)){
                selected=(int)type;picture=p;picture_size=bytes;
            }
        }else if(kind==4 && size<=8192 && size>=8){
            u8 comments[8192];if(exact(fd,at,comments,size,limit)<0)return -1;
            u32 vendor=little(comments),p=4;if(vendor>size-p || size-p-vendor<4)return -1;p+=vendor;
            u32 count=little(comments+p);p+=4;
            if(count>128)return -1;
            for(u32 i=0;i<count;i++){
                if(size-p<4)return -1;u32 n=little(comments+p);p+=4;if(n>size-p)return -1;
                if(field(comments+p,n,"TITLE="))text(art->title,comments+p+6,n-6,3);
                if(field(comments+p,n,"ARTIST="))text(art->artist,comments+p+7,n-7,3);
                p+=n;
            }
        }
        at=limit;
    }
    return picture?extract(art,fd,picture,picture_size,end):-1;
}
static int mp3(sc_album_art *art,int fd,const u8 *header,u32 end)
{
    int version=header[3];if((version!=3 && version!=4) || (header[5]&0xC0))return -1;
    for(int i=6;i<10;i++)if(header[i]&128)return -1;
    u32 tag=syncsafe(header+6);if(tag>end-10)return -1;u32 at=10,limit=10+tag,picture=0,picture_size=0;
    int selected=-1;
    for(int frame=0;frame<128 && limit-at>=10;frame++){
        u8 h[10];if(exact(fd,at,h,10,limit)<0 || !h[0])break;
        if(version==4 && ((h[4]|h[5]|h[6]|h[7])&128))break;
        u32 n=version==4?syncsafe(h+4):big(h+4);at+=10;if(!n || n>limit-at)break;
        int supported=version==3?!(h[9]&0xE0):!(h[9]&0x4F);
        if(supported && n<=COVER_LIMIT && (h[0]=='T' || (h[0]=='A' && h[1]=='P' && h[2]=='I' && h[3]=='C'))){
            u8 *body=sc_alloc(n);if(!body)return -1;if(exact(fd,at,body,n,limit)<0){sc_free(body);return -1;}
            if(h[0]=='T' && n>1){
                if(h[1]=='I' && h[2]=='T' && h[3]=='2')text(art->title,body+1,n-1,body[0]);
                if(h[1]=='P' && h[2]=='E' && h[3]=='1')text(art->artist,body+1,n-1,body[0]);
            }else if(n>=5){
                u32 p=1;while(p<n && body[p])p++;p++;
                if(p<n){int type=body[p++],encoding=body[0];
                    if(encoding==1 || encoding==2){while(p+1<n && (body[p] || body[p+1]))p+=2;p+=2;}
                    else {while(p<n && body[p])p++;p++;}
                    if(p<n && (selected<0 || type==3)){selected=type;picture=at+p;picture_size=n-p;}
                }
            }
            sc_free(body);
        }
        at+=n;
    }
    return picture?extract(art,fd,picture,picture_size,end):-1;
}
void sc_album_close(sc_album_art *art)
{
    image_client_cancel(&art->image);if(art->image.pixels)sc_free(art->image.pixels);
    art->image.pixels=0;if(art->temporary[0])sc_remove(art->temporary);art->temporary[0]=0;
    art->source[0]=art->sidecar[0]=0;art->source_generation=art->sidecar_generation=0;
}
int sc_album_current(sc_album_art *art,const char *name)
{
    /* 重播复用封面，但同名文件被COW替换、cwd改变、目录封面更新时
     * 必须失效；比较内核对象代数而不是仅比较用户输入字符串。 */
    char root[64];u32 info[8];
    if((!art->image.pixels && !art->image.ticket) || sc_resolve(name,root,64)<0 || !equal(root,art->source)
        || sc_fsmeta(root,info)<0 || info[7]!=art->source_generation)return 0;
    return !art->sidecar[0] || (sc_fsmeta(art->sidecar,info)>=0 && info[7]==art->sidecar_generation);
}
int sc_album_begin(sc_album_art *art,const char *name)
{
    sc_album_close(art);art->title[0]=art->artist[0]=0;art->began=(u32)sc_tick();
    char root[64];u32 info[2];if(sc_resolve(name,root,64)<0 || sc_stat(root,info)<0 || info[0]!=1)return -1;
    u32 meta[8];if(sc_fsmeta(root,meta)<0)return -1;copy(art->source,root,64);art->source_generation=meta[7];
    int slash=-1;for(int i=0;root[i];i++)if(root[i]=='/')slash=i;
    copy(art->title,root+slash+1,128);copy(art->artist,"Local library",128);
    int fd=sc_stream_open(name,1,0),result=-1;
    if(fd>=0){u8 header[12];if(info[1]>=12 && exact(fd,0,header,12,info[1])==0){
        if(header[0]=='f' && header[1]=='L' && header[2]=='a' && header[3]=='C')result=flac(art,fd,info[1]);
        else if(header[0]=='I' && header[1]=='D' && header[2]=='3')result=mp3(art,fd,header,info[1]);}
        sc_stream_close(fd,0);}
    if(result>=0)return 0;
    static const char *names[4]={"cover.png","cover.jpg","folder.png","folder.jpg"};
    for(int i=0;i<4;i++){char path[65];path[0]='/';for(int n=0;n<=slash;n++)path[n+1]=root[n];path[slash+2]=0;
        if(length(path)+length(names[i])>64)continue;append(path,names[i],65);
        char normalized[64];if(sc_resolve(path,normalized,64)<0 || sc_stat(normalized,info)<0 || info[0]!=1 || info[1]>COVER_LIMIT)continue;
        if(image_client_begin(&art->image,path)>=0){
            copy(art->sidecar,normalized,64);if(sc_fsmeta(normalized,meta)>=0)art->sidecar_generation=meta[7];return 0;}}
    return -1;
}
int sc_album_step(sc_album_art *art)
{
    if(!art->image.ticket)return art->image.pixels?0:-1;
    int result=image_client_step(&art->image);
    if(result==1 && (u32)((u32)sc_tick()-art->began)<1000)return 1;
    if(result==1){image_client_cancel(&art->image);result=-1;}
    if(art->temporary[0]){sc_remove(art->temporary);art->temporary[0]=0;}
    return result;
}
