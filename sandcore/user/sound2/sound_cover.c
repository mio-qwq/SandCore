#include "SOUNDCOVERPRIVATE.H"
typedef char scov_contract_268[(sizeof(SndCover)==268)?1:-1];

static void scov_record(void *record,ScU32 bytes)
{memset(record,0,bytes);
    ((ScU32 *)record)[0]=bytes;
    ((ScU32 *)record)[1]=1;
}
static ScU32 scov_be(const unsigned char *p)
{return (ScU32)p[0]<<24|(ScU32)p[1]<<16|(ScU32)p[2]<<8|p[3];
}
static ScU32 scov_le(const unsigned char *p)
{return (ScU32)p[3]<<24|(ScU32)p[2]<<16|(ScU32)p[1]<<8|p[0];
}
static ScU32 scov_sync(const unsigned char *p)
{return (ScU32)p[0]<<21|(ScU32)p[1]<<14|(ScU32)p[2]<<7|p[3];
}
static void scov_wipe(void *memory,ScU32 bytes)
{volatile unsigned char *p=memory;
    while(bytes--)*p++=0;
}
void snd_cover_close(SndCover *out)
{
    if(!out)return;
    if(out->data){scov_wipe(out->data,out->bytes);
        free(out->data);
    }
    memset(out,0,sizeof(*out));
}
static ScStatus scov_source_open(const char *path,ScU32 revision,ScovSource *source)
{
    memset(source,0,sizeof(*source));
    if(!path || path[0]!='/' || !revision || strlen(path)>64)return SC32_E_INVALID;
    for(const unsigned char *p=(const unsigned char *)path;*p;p++)if(*p<33 || *p=='\\' || *p==':')return SC32_E_INVALID;
    ScLeaseOpenDesc d;
    scov_record(&d,sizeof(d));
    d.path=path;
    d.object_kind=SC32_PATH_FILE;
    d.lease_kind=SC32_LEASE_READ_STABLE;
    d.open_flags=SC32_FILE_READ;
    d.expected_revision=revision;
    scov_record(&source->lease,sizeof(source->lease));
    ScStatus s=ScFileLeaseOpenPinned(&d,&source->lease);
    if(s<0)return s;
    ScFileInfo info;
    scov_record(&info,sizeof(info));
    s=ScFileQuery(source->lease.file,&info);
    if(s==0 && info.revision!=revision)s=SC32_E_STALE;
    if(s<0){scov_source_close(source);
        return s;
    }
    source->length=info.length;
    source->revision=revision;
    return 0;
}
static void scov_source_close(ScovSource *source)
{
    if(source->lease.lease){
        ScLeaseControl d;
        scov_record(&d,sizeof(d));
        d.lease=source->lease.lease;
        d.generation=source->lease.generation;
        d.operation_sequence=source->lease.operation_sequence;
        ScLeaseInfo result;
        scov_record(&result,sizeof(result));
        ScFileLeaseRelease(&d,&result);
    }
    /* Close是已有lease安全点，未结束的GC由后端接管；不在同步可选
    * 元数据内轮询release状态，也不丢给另一个进程的FILE。 */
    if(source->lease.file)ScFileClose(source->lease.file);
    memset(source,0,sizeof(*source));
}
static ScStatus scov_current(ScovSource *source,const char *path)
{
    ScFileInfo info;
    scov_record(&info,sizeof(info));
    ScStatus s=ScFileQuery(source->lease.file,&info);
    if(s==0 && (info.revision!=source->revision || info.length!=source->length))s=SC32_E_STALE;
    ScPathInfo meta;
    scov_record(&meta,sizeof(meta));
    if(s==0)s=ScPathQuery(path,&meta);
    if(s==0 && (meta.kind!=SC32_PATH_FILE || meta.generation!=source->revision || meta.bytes!=source->length))s=SC32_E_STALE;
    return s;
}
static ScStatus scov_seek(ScovSource *source,ScU32 at)
{
    ScFileInfo info;
    scov_record(&info,sizeof(info));
    ScU32 first=at>(ScU32)INT_MAX?(ScU32)INT_MAX:at;
    ScStatus s=ScFileSeek(source->lease.file,(ScI32)first,SC32_SEEK_SET,&info);
    at-=first;
    while(s==0 && at){ScU32 part=at>(ScU32)INT_MAX?(ScU32)INT_MAX:at;
        scov_record(&info,sizeof(info));
        s=ScFileSeek(source->lease.file,(ScI32)part,SC32_SEEK_CUR,&info);
        at-=part;
    }
    return s;
}
static ScStatus scov_read(ScovSource *source,ScU32 at,void *out,ScU32 bytes,ScU32 end)
{
    if(end>source->length || at>end || bytes>end-at)return SC32_E_RANGE;
    if(!bytes)return 0;
    ScStatus s=scov_seek(source,at);
    if(s<0)return s;
    ScU32 used=0;
    while(used<bytes){ScU32 count=bytes-used;
        if(count>SCOV_READ_BATCH)count=SCOV_READ_BATCH;
        ScIoResult io;
        scov_record(&io,sizeof(io));
        s=ScFileRead(source->lease.file,(unsigned char *)out+used,count,&io);
        if(s<0)return s;
        if(!io.transferred || io.transferred>count)return SC32_E_IO;
        used+=io.transferred;
        ScProcessYield();
    }return 0;
}
static void scov_text(char out[128],const unsigned char *input,ScU32 bytes,ScU32 encoding)
{
    ScU32 at=0,used=0;
    int big_endian=encoding==2;
    if(encoding>3)return;
    if(encoding==1 && bytes>=2){big_endian=input[0]==0xfe && input[1]==0xff;
        if(big_endian || (input[0]==0xff && input[1]==0xfe))at=2;
    }
    while(at<bytes){
        ScU32 scalar=input[at++];
        if(encoding==1 || encoding==2){
            if(at==bytes)break;
            scalar=big_endian?(scalar<<8)|input[at]:scalar|((ScU32)input[at]<<8);
            at++;
            if(scalar>=0xd800 && scalar<=0xdbff){
                if(bytes-at<2){scalar='?';
                    at=bytes;
                }
                else {ScU32 next=big_endian?((ScU32)input[at]<<8)|input[at+1]:input[at]|((ScU32)input[at+1]<<8);
                    if(next>=0xdc00 && next<=0xdfff){at+=2;
                        scalar=0x10000+((scalar-0xd800)<<10)+(next-0xdc00);
                    }else scalar='?';
                }
            }else if(scalar>=0xdc00 && scalar<=0xdfff)scalar='?';
        }else if(encoding==3){
            ScU32 start=at-1,offset=start;
            if(ScUtf8Decode(input,bytes,&offset,&scalar)<0){scalar='?';
            }else at=offset;
        }
        if(!scalar)break;
        if(scalar<32 || scalar==127)scalar=' ';
        unsigned char encoded[4];
        ScU32 count=0;
        if(ScUtf8Encode(scalar,encoded,sizeof(encoded),&count)<0){encoded[0]='?';
            count=1;
        }
        if(count>127-used)break;
        memcpy(out+used,encoded,count);
        used+=count;
    }
    out[used]=0;
}
static ScStatus scov_extract(SndCover *out,ScovSource *source,ScU32 at,ScU32 bytes,ScU32 end,ScU32 limit)
{
    if(!bytes || bytes>limit || at>end || bytes>end-at)return SC32_E_RANGE;
    unsigned char *data=malloc(bytes);
    if(!data)return SC32_E_MEMORY;
    ScStatus s=scov_read(source,at,data,bytes,end);
    if(s<0){scov_wipe(data,bytes);
        free(data);
        return s;
    }
    out->data=data;
    out->bytes=bytes;
    out->source_kind=1;
    return 0;
}
/* 文本最多需要512B即可填满127B显示预算；只读这个前缀，不把长
* 注释或整个音频读入。UTF-16/UTF-8边界在cover_text中完整处理。 */
static ScStatus scov_text_field(SndCover *out,ScovSource *source,ScU32 at,ScU32 bytes,ScU32 end,int artist,ScU32 encoding)
{
    unsigned char text[512];
    ScU32 count=bytes<sizeof(text)?bytes:sizeof(text);
    ScStatus s=scov_read(source,at,text,count,end);
    if(s==0)scov_text(artist?out->artist:out->title,text,count,encoding);
    scov_wipe(text,sizeof(text));
    return s;
}
static int scov_field(const unsigned char *input,ScU32 bytes,const char *key)
{
    ScU32 n=(ScU32)strlen(key);
    if(bytes<n)return 0;
    for(ScU32 i=0;i<n;i++){unsigned char c=input[i];
        if(c>='a' && c<='z')c-=32;
        if(c!=(unsigned char)key[i])return 0;
    }return 1;
}
static ScStatus scov_apic(ScovSource *source,ScU32 at,ScU32 bytes,ScU32 end,ScovPicture *selected)
{
    if(bytes<5 || bytes>SCOV_COVER_LIMIT)return 0;
    unsigned char encoding;
    ScStatus s=scov_read(source,at,&encoding,1,end);
    if(s<0)return s;
    if(encoding>3)return 0;
    ScU32 position=1;
    unsigned char buffer[1024];
    int mime_end=0;
    ScU32 picture_type=0;
    /* 逐块搜索MIME和描述终止符；即使描述接近1MiB，也不逐字节seek。 */
    while(position<bytes && !mime_end){ScU32 count=bytes-position;
        if(count>sizeof(buffer))count=sizeof(buffer);
        s=scov_read(source,at+position,buffer,count,end);
        if(s<0)return s;
        for(ScU32 i=0;i<count;i++){position++;
            if(!buffer[i]){mime_end=1;
                break;
            }}
    }
    if(!mime_end || position>=bytes)return 0;
    unsigned char type;
    s=scov_read(source,at+position,&type,1,end);
    if(s<0)return s;
    position++;
    picture_type=type;
    ScU32 unit=encoding==1 || encoding==2?2:1;
    int terminated=0;
    while(position<bytes && !terminated){ScU32 count=bytes-position;
        if(count>sizeof(buffer))count=sizeof(buffer);
        if(unit==2)count&=~1u;
        if(!count)break;
        s=scov_read(source,at+position,buffer,count,end);
        if(s<0)return s;
        for(ScU32 i=0;i<count;i+=unit){position+=unit;
            if(!buffer[i] && (unit==1 || !buffer[i+1])){terminated=1;
                break;
            }}
    }
    if(terminated && position<bytes && (!selected->bytes || picture_type==3)){
        selected->offset=at+position;
        selected->bytes=bytes-position;
        selected->type=picture_type;
    }return 0;
}
static ScStatus scov_mp3(SndCover *out,ScovSource *source,const unsigned char *header,int pictures)
{
    ScU32 version=header[3];
    if((version!=3 && version!=4) || (header[5]&0xc0))return SC32_E_NOT_SUPPORTED;
    for(ScU32 i=6;i<10;i++)if(header[i]&128)return SC32_E_INVALID;
    ScU32 tag=scov_sync(header+6);
    if(source->length<10 || tag>source->length-10)return SC32_E_INVALID;
    ScU32 at=10,end=10+tag;
    ScovPicture picture={0,0,0};
    for(ScU32 frame=0;frame<128 && end-at>=10;frame++){
        unsigned char h[10];
        ScStatus s=scov_read(source,at,h,sizeof(h),end);
        if(s<0)return s;
        if(!h[0])break;
        if(version==4 && ((h[4]|h[5]|h[6]|h[7])&128))break;
        ScU32 bytes=version==4?scov_sync(h+4):scov_be(h+4);
        at+=10;
        if(!bytes || bytes>end-at)break;
        int supported=version==3?!(h[9]&0xe0):!(h[9]&0x4f);
        if(supported && bytes<=SCOV_COVER_LIMIT){
            if(bytes>1 && (!memcmp(h,"TIT2",4) || !memcmp(h,"TPE1",4))){
                unsigned char encoding;
                s=scov_read(source,at,&encoding,1,end);
                if(s==0)s=scov_text_field(out,source,at+1,bytes-1,end,!memcmp(h,"TPE1",4),encoding);
            }else if(pictures && !memcmp(h,"APIC",4))s=scov_apic(source,at,bytes,end,&picture);
            if(s<0)return s;
        }
        at+=bytes;
        ScProcessYield();
    }
    return picture.bytes?scov_extract(out,source,picture.offset,picture.bytes,end,SCOV_COVER_LIMIT):0;
}
static int scov_base64(unsigned char c)
{
    if(c>='A' && c<='Z')return c-'A';
    if(c>='a' && c<='z')return c-'a'+26;
    if(c>='0' && c<='9')return c-'0'+52;
    if(c=='+')return 62;
    if(c=='/')return 63;
    return c=='='?-2:-1;
}
static ScStatus scov_picture_memory(SndCover *out,unsigned char *body,ScU32 decoded)
{
    if(decoded<32)return SC32_E_INVALID;
    ScU32 mime=scov_be(body+4),at=8;
    if(mime>decoded-at)return SC32_E_INVALID;
    at+=mime;
    if(decoded-at<4)return SC32_E_INVALID;
    ScU32 description=scov_be(body+at);
    at+=4;
    if(description>decoded-at)return SC32_E_INVALID;
    at+=description;
    if(decoded-at<20)return SC32_E_INVALID;
    ScU32 bytes=scov_be(body+at+16);
    at+=20;
    if(!bytes || bytes>SCOV_FLAC_LIMIT || bytes>decoded-at)return SC32_E_INVALID;
    /* 图片正文移到同一拥有缓冲的开头，避免16MiB封面再分配/复制一份。
    * 尾部元数据先擦掉；realloc缩小失败仍归caller Close，不丢原指针。 */
    memmove(body,body+at,bytes);
    scov_wipe(body+bytes,decoded-bytes);
    unsigned char *smaller=realloc(body,bytes);
    if(smaller)body=smaller;
    out->data=body;
    out->bytes=bytes;
    out->source_kind=1;
    return 0;
}
static ScStatus scov_comment_picture(SndCover *out,ScovSource *source,ScU32 at,ScU32 bytes,ScU32 end)
{
    ScU32 maximum=((SCOV_FLAC_LIMIT+8192u)/3u+1u)*4u;
    if(!bytes || (bytes&3u) || bytes>maximum)return SC32_E_INVALID;
    ScU32 capacity=bytes/4u*3u;
    unsigned char *body=malloc(capacity);
    if(!body)return SC32_E_MEMORY;
    unsigned char input[4096];
    ScU32 used=0,decoded=0;
    ScStatus s=0;
    while(used<bytes){ScU32 count=bytes-used;
        if(count>sizeof(input))count=sizeof(input);
        s=scov_read(source,at+used,input,count,end);
        if(s<0)break;
        for(ScU32 i=0;i<count;i+=4){
            int a=scov_base64(input[i]),b=scov_base64(input[i+1]),c=scov_base64(input[i+2]),d=scov_base64(input[i+3]);
            if(a<0 || b<0 || c==-1 || d==-1 || (c==-2 && d!=-2)
            || ((c==-2 || d==-2) && used+i+4!=bytes)
            || (c==-2 && (b&15)) || (d==-2 && c>=0 && (c&3))){s=SC32_E_INVALID;
                break;
            }
            body[decoded++]=(unsigned char)((a<<2)|(b>>4));
            if(c>=0){body[decoded++]=(unsigned char)((b<<4)|(c>>2));
                if(d>=0)body[decoded++]=(unsigned char)((c<<6)|d);
            }
        }
        if(s<0)break;
        used+=count;
        ScProcessYield();
    }
    if(s==0)s=scov_picture_memory(out,body,decoded);
    if(s<0){scov_wipe(body,capacity);
        free(body);
    }return s;
}
static ScStatus scov_flac(SndCover *out,ScovSource *source,int pictures)
{
    ScU32 at=4,comment_at=0,comment_bytes=0;
    int last=0;
    ScovPicture picture={0,0,0};
    while(!last){
        unsigned char h[8];
        if(at>source->length || source->length-at<4)return SC32_E_INVALID;
        ScStatus s=scov_read(source,at,h,4,source->length);
        if(s<0)return s;
        last=(h[0]&128)!=0;
        ScU32 kind=h[0]&127,bytes=(ScU32)h[1]<<16|(ScU32)h[2]<<8|h[3];
        at+=4;
        if(bytes>source->length-at)return SC32_E_INVALID;
        ScU32 end=at+bytes;
        if(pictures && kind==6 && bytes>=32){
            ScU32 position=at;
            s=scov_read(source,position,h,8,end);
            if(s<0)return s;
            ScU32 type=scov_be(h),mime=scov_be(h+4);
            position+=8;
            if(mime>end-position)return SC32_E_INVALID;
            position+=mime;
            s=scov_read(source,position,h,4,end);
            if(s<0)return s;
            ScU32 description=scov_be(h);
            position+=4;
            if(description>end-position)return SC32_E_INVALID;
            position+=description;
            unsigned char fields[20];
            s=scov_read(source,position,fields,20,end);
            if(s<0)return s;
            position+=20;
            ScU32 length=scov_be(fields+16);
            if(length>end-position)return SC32_E_INVALID;
            if(length && length<=SCOV_FLAC_LIMIT && (!picture.bytes || type==3)){
                picture.offset=position;
                picture.bytes=length;
                picture.type=type;
            }
        }else if(kind==4 && bytes>=8){
            ScU32 position=at;
            s=scov_read(source,position,h,4,end);
            if(s<0)return s;
            ScU32 vendor=scov_le(h);
            position+=4;
            if(vendor>end-position || end-position-vendor<4)return SC32_E_INVALID;
            position+=vendor;
            s=scov_read(source,position,h,4,end);
            if(s<0)return s;
            ScU32 count=scov_le(h);
            position+=4;
            if(count>(end-position)/4)return SC32_E_INVALID;
            for(ScU32 i=0;i<count;i++){
                s=scov_read(source,position,h,4,end);
                if(s<0)return s;
                ScU32 length=scov_le(h);
                position+=4;
                if(length>end-position)return SC32_E_INVALID;
                unsigned char value[23];
                ScU32 prefix=length<sizeof(value)?length:sizeof(value);
                s=scov_read(source,position,value,prefix,end);
                if(s<0)return s;
                if(scov_field(value,prefix,"TITLE="))s=scov_text_field(out,source,position+6,length-6,end,0,3);
                else if(scov_field(value,prefix,"ARTIST="))s=scov_text_field(out,source,position+7,length-7,end,1,3);
                else if(pictures && !comment_bytes && scov_field(value,prefix,"METADATA_BLOCK_PICTURE=")){comment_at=position+23;
                    comment_bytes=length-23;
                }
                if(s<0)return s;
                position+=length;
                ScProcessYield();
            }
        }
        at=end;
        ScProcessYield();
    }
    if(picture.bytes)return scov_extract(out,source,picture.offset,picture.bytes,source->length,SCOV_FLAC_LIMIT);
    return comment_bytes?scov_comment_picture(out,source,comment_at,comment_bytes,source->length):0;
}
static void scov_default(SndCover *out,const char *path)
{
    memset(out,0,sizeof(*out));
    const char *base=path?strrchr(path,'/'):0;
    base=base?base+1:path;
    if(base)scov_text(out->title,(const unsigned char *)base,(ScU32)strlen(base),3);
    strcpy(out->artist,"Local library");
}
static int scov_name_equal(const char *left,const char *right)
{
    while(*left && *right){unsigned char a=(unsigned char)*left++,b=(unsigned char)*right++;
        if(a>='a' && a<='z')a-=32;
        if(b>='a' && b<='z')b-=32;
        if(a!=b)return 0;
    }
    return !*left && !*right;
}
static ScStatus scov_sidecars(SndCover *out,const char *path)
{
    static const char *scov_names[]={"cover.png","cover.jpg","folder.png","folder.jpg"};
    const char *slash=strrchr(path,'/');
    if(!slash)return SC32_E_INVALID;
    ScU32 prefix=(ScU32)(slash-path)+1;
    for(ScU32 i=0;i<4;i++){
        if(scov_name_equal(slash+1,scov_names[i]))continue;
        char candidate[65];
        ScU32 length=(ScU32)strlen(scov_names[i]);
        if(prefix>64 || length>64-prefix)continue;
        memcpy(candidate,path,prefix);
        memcpy(candidate+prefix,scov_names[i],length+1);
        ScPathInfo meta;
        scov_record(&meta,sizeof(meta));
        ScStatus s=ScPathQuery(candidate,&meta);
        if(s<0 || meta.kind!=SC32_PATH_FILE || !meta.bytes || meta.bytes>SCOV_COVER_LIMIT)continue;
        ScovSource source;
        s=scov_source_open(candidate,meta.generation,&source);
        if(s<0)continue;
        SndCover picture;
        memset(&picture,0,sizeof(picture));
        s=scov_extract(&picture,&source,0,source.length,source.length,SCOV_COVER_LIMIT);
        if(s==0)s=scov_current(&source,candidate);
        scov_source_close(&source);
        if(s==0){out->data=picture.data;
            out->bytes=picture.bytes;
            out->source_kind=2;
            return 0;
        }
        snd_cover_close(&picture);
    }
    return 0;
}
static ScStatus scov_open_internal(const char *path,ScU32 exact_revision,SndCover *out,int only_sidecar)
{
    if(!out)return SC32_E_INVALID;
    scov_default(out,path);
    ScovSource source;
    ScStatus s=scov_source_open(path,exact_revision,&source);
    if(s<0)return s;
    ScStatus metadata=0;
    {
        unsigned char header[12]={0};
        ScU32 bytes=source.length<sizeof(header)?source.length:sizeof(header);
        metadata=scov_read(&source,0,header,bytes,source.length);
        if(metadata==0){if(bytes>=4 && !memcmp(header,"fLaC",4))metadata=scov_flac(out,&source,!only_sidecar);
            else if(bytes>=10 && !memcmp(header,"ID3",3))metadata=scov_mp3(out,&source,header,!only_sidecar);
        }
    }
    if(!out->data)scov_sidecars(out,path);
    /* 封面与音频候选仍是同一修订。权限/metadata变化不能把前一次
    * 路径的图片静默拼给新曲目；读失败只影响可选结果，不碰声音对象。 */
    s=scov_current(&source,path);
    scov_source_close(&source);
    if(s<0){snd_cover_close(out);
        scov_default(out,path);
        return s;
    }
    return out->data || metadata>=0?0:metadata;
}
ScStatus snd_cover_open(const char *absolute_path,ScU32 exact_revision,SndCover *out)
{return scov_open_internal(absolute_path,exact_revision,out,0);
}
ScStatus snd_cover_sidecar(const char *absolute_path,ScU32 exact_revision,SndCover *out)
{return scov_open_internal(absolute_path,exact_revision,out,1);
}
