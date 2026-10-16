#include "../sc32/SC32PRIVATE.H"
#include "../SC32IMAGE.H"
#include "../SC32PATH.H"
#include "SC32IMAGEWIRE.H"

enum {SC32_IMAGE_OP_BEGIN=0x1300,SC32_IMAGE_OP_QUERY=0x1301,SC32_IMAGE_OP_COPY=0x1302,SC32_IMAGE_OP_CANCEL=0x1303,
    SC32_IMAGE_OP_MEMORY=0x1304,SC32_IMAGE_OP_STEP=0x1305,SC32_IMAGE_CHUNK=65536};
typedef struct Sc32ImageJob Sc32ImageJob;
struct Sc32ImageJob {
    Sc32ImageJob *next;
    ScU32 id,legacy,file,kind,width,height,body,position;
    ScU32 source_bytes,file_revision,path_revision,object,ready,memory,sealed;
    ScStatus error;
    const unsigned char *input;
    char path[65];
    ScU32 palette[256];
};
static Sc32ImageJob *sc32_image_jobs;
static ScU32 sc32_image_serial;
static ScU32 sc32_image_word(const unsigned char *p)
{return (ScU32)p[0]|((ScU32)p[1]<<8)|((ScU32)p[2]<<16)|((ScU32)p[3]<<24);
}
static int sc32_image_magic(const unsigned char *p,const char *magic)
{for(ScU32 i=0;i<8;i++)if(p[i]!=(unsigned char)magic[i])return 0;
    return 1;
}
static Sc32ImageJob *sc32_image_find(ScU32 id)
{for(Sc32ImageJob *j=sc32_image_jobs;j;j=j->next)if(j->id==id)return j;
    return 0;
}
static ScStatus sc32_image_error(ScI32 raw,ScU32 operation)
{
    ScStatus code=raw==-2?(operation==SC32_IMAGE_OP_BEGIN?SC32_E_NOT_FOUND:SC32_E_INVALID):SC32_E_FAILED;
    if(raw==-5)code=operation==SC32_IMAGE_OP_BEGIN?SC32_E_ACCESS:SC32_E_MEMORY;
    else if(raw==-3)code=SC32_E_NOT_SUPPORTED;
    else if(raw==-4)code=SC32_E_INVALID;
    else if(raw==-6)code=SC32_E_IO;
    else if(raw==-1)code=operation==SC32_IMAGE_OP_BEGIN?SC32_E_BUSY:SC32_E_STALE;
    return sc32_fail(code,SC32_DOMAIN_LEGACY,raw,operation);
}
static void sc32_image_destroy(Sc32ImageJob *j)
{
    Sc32ImageJob **link=&sc32_image_jobs;
    while(*link && *link!=j)link=&(*link)->next;
    if(*link)*link=j->next;
    if(j->legacy)(void)sc32_call(0x80,(ScI32)j->legacy,0,0,0,0,0);
    if(j->file)(void)ScFileClose(j->file);
    (void)ScMemoryFreePages(j);
}
static void sc32_image_finalize(void)
{while(sc32_image_jobs)sc32_image_destroy(sc32_image_jobs);
}
static ScStatus sc32_image_source(Sc32ImageJob *j)
{
    if(j->memory)return 0;
    ScPathInfo p;
    sc32_zero(&p,sizeof(p));
    p.size=sizeof(p);
    p.version=1;
    ScStatus s=ScPathQuery(j->path,&p);
    if(s<0)return s;
    if(p.kind!=SC32_PATH_FILE || p.generation!=j->path_revision || p.bytes!=j->source_bytes)return SC32_E_STALE;
    ScFileInfo f;
    sc32_zero(&f,sizeof(f));
    f.size=sizeof(f);
    f.version=1;
    s=ScFileQuery(j->file,&f);
    if(s<0)return s;
    return f.object_id==j->object && f.revision==j->file_revision && f.length==j->source_bytes?0:SC32_E_STALE;
}
static void sc32_image_info(Sc32ImageJob *j,ScImageInfo *out)
{
    ScImageInfo v;
    sc32_zero(&v,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.request=j->id;
    v.width=j->width;
    v.height=j->height;
    v.stride=j->width*4u;
    v.bytes=j->width*j->height*4u;
    v.format=SC32_IMAGE_ARGB32;
    v.flags=(j->ready?SC32_IMAGE_READY:0)|((j->memory && !j->sealed)?SC32_IMAGE_MORE_WORK:j->legacy?SC32_IMAGE_SERVICE:!j->ready?SC32_IMAGE_MORE_WORK:0);
    if(j->memory && j->sealed)v.flags|=SC32_IMAGE_INPUT_SEALED;
    if(j->memory && !j->sealed){v.progress=j->position;
        v.total=j->source_bytes;
    }
    else if(j->kind){v.progress=j->position;
        v.total=j->source_bytes-j->body;
    }
    else if(!j->ready)v.wait_events=SC32_EVENT_IMAGE;
    sc32_copy(out,&v,sizeof(v));
}
ScStatus ScImageDecodeBegin(const ScImageDecodeDesc *desc,ScImageRequest *out)
{
    ScStatus s=sc32_record_in(desc,sizeof(*desc));
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_BEGIN);
    s=sc32_out(out,4);
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_BEGIN);
    if(desc->flags || desc->reserved[0] || desc->reserved[1] || desc->reserved[2] || desc->reserved[3])return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_BEGIN);
    if(sc32_image_serial==0x7fffffffu)return sc32_invalid(SC32_E_RANGE,SC32_IMAGE_OP_BEGIN);
    char path[65];
    s=sc32_absolute(desc->path,path,SC32_IMAGE_OP_BEGIN);
    if(s<0)return s;
    ScPathInfo source;
    sc32_zero(&source,sizeof(source));
    source.size=sizeof(source);
    source.version=1;
    s=ScPathQuery(path,&source);
    if(s<0)return s;
    if(source.kind!=SC32_PATH_FILE || source.bytes<24u || source.bytes>16u*1024u*1024u)return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_BEGIN);
    void *pages=0;
    s=ScMemoryAllocatePages(sizeof(Sc32ImageJob)+SC32_IMAGE_CHUNK,&pages);
    if(s<0)return s;
    Sc32ImageJob *j=pages;
    sc32_zero(j,sizeof(*j));
    sc32_copy(j->path,path,65);
    j->source_bytes=source.bytes;
    j->path_revision=source.generation;
    ScFileOpenDesc open;
    sc32_zero(&open,sizeof(open));
    open.size=sizeof(open);
    open.version=1;
    open.path=j->path;
    open.flags=SC32_FILE_READ;
    s=ScFileOpen(&open,&j->file);
    if(s<0)goto failed;
    ScFileInfo fi;
    sc32_zero(&fi,sizeof(fi));
    fi.size=sizeof(fi);
    fi.version=1;
    s=ScFileQuery(j->file,&fi);
    if(s<0)goto failed;
    j->file_revision=fi.revision;
    j->object=fi.object_id;
    if(fi.length!=source.bytes){s=SC32_E_STALE;
        goto failed;
    }
    unsigned char *header=(unsigned char *)(j+1);
    ScIoResult io;
    sc32_zero(&io,sizeof(io));
    io.size=sizeof(io);
    io.version=1;
    s=ScFileRead(j->file,header,64,&io);
    if(s<0)goto failed;
    if(io.transferred<24){s=SC32_E_IO;
        goto failed;
    }
    if(sc32_image_magic(header,"SCB1MIO") || sc32_image_magic(header,"SCB2MIO")){
        j->width=sc32_image_word(header+8);
        j->height=sc32_image_word(header+12);
        if(!j->width || !j->height || j->width>1920 || j->height>1080){s=SC32_E_RANGE;
            goto failed;
        }
        if(header[3]=='1'){
            j->kind=1;
            j->body=24;
            if(sc32_image_word(header+16)!=1 || sc32_image_word(header+20)!=0x004f494du || j->source_bytes!=24u+j->width*j->height){s=SC32_E_INVALID;
                goto failed;
            }
            s=ScDisplayPalette(j->palette,256);
            if(s<0)goto failed;
        }else{
            j->kind=2;
            j->body=32;
            if(io.transferred<32 || sc32_image_word(header+16)!=2 || sc32_image_word(header+20)
            || sc32_image_word(header+24)!=j->width*j->height*4u || sc32_image_word(header+28)!=0x004f494du
            || j->source_bytes!=32u+j->width*j->height*4u){s=SC32_E_INVALID;
                goto failed;
            }
        }
        sc32_zero(&fi,sizeof(fi));
        fi.size=sizeof(fi);
        fi.version=1;
        s=ScFileSeek(j->file,(ScI32)j->body,SC32_SEEK_SET,&fi);
        if(s<0)goto failed;
    }else{
        int supported=(header[0]==255 && header[1]==216)||(header[0]=='B' && header[1]=='M')
        ||(header[0]==137 && header[1]=='P' && header[2]=='N' && header[3]=='G')
        ||(header[0]=='R' && header[1]=='I' && header[2]=='F' && header[3]=='F' && header[8]=='W' && header[9]=='E' && header[10]=='B' && header[11]=='P');
        if(!supported){s=SC32_E_NOT_SUPPORTED;
            goto failed;
        }
        ScI32 raw=sc32_call(0x7c,(ScI32)j->path,0,0,0,0,0);
        if(raw<=0){s=sc32_image_error(raw,SC32_IMAGE_OP_BEGIN);
            goto failed;
        }j->legacy=(ScU32)raw;
    }
    j->id=++sc32_image_serial;
    j->next=sc32_image_jobs;
    sc32_image_jobs=j;
     *out=j->id;
    return 0;
    failed:
    sc32_image_destroy(j);
    return sc32_fail(s,SC32_DOMAIN_LIBRARY,s,SC32_IMAGE_OP_BEGIN);
}
ScStatus ScImageDecodeMemoryBegin(const ScImageMemoryDesc *desc,ScImageRequest *out)
{
    ScStatus s=sc32_record_in(desc,sizeof(*desc));
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_MEMORY);
    s=sc32_out(out,4);
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_MEMORY);
    if(desc->flags || desc->reserved[0] || desc->reserved[1] || desc->reserved[2]
    || !desc->data || desc->bytes<8 || desc->bytes>SC32_IMAGE_MAX_SOURCE_BYTES)return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_MEMORY);
    if(sc32_overlap(out,4,desc->data,desc->bytes))return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_MEMORY);
    if(sc32_image_serial==0x7fffffffu)return sc32_invalid(SC32_E_RANGE,SC32_IMAGE_OP_MEMORY);
    s=sc32_probe(desc->data,desc->bytes,SC32_WIRE_READ);
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_MEMORY);
    void *pages=0;
    s=ScMemoryAllocatePages(sizeof(Sc32ImageJob)+SC32_IMAGE_CHUNK,&pages);
    if(s<0)return s;
    Sc32ImageJob *j=pages;
    sc32_zero(j,sizeof(*j));
    j->memory=1;
    j->input=desc->data;
    j->source_bytes=desc->bytes;
    j->id=++sc32_image_serial;
    j->next=sc32_image_jobs;
    sc32_image_jobs=j;
     *out=j->id;
    return 0;
}
ScStatus ScImageDecodeStep(ScImageRequest request,ScU32 byte_budget,ScImageInfo *out)
{
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_STEP);
    if(!byte_budget || byte_budget>SC32_IMAGE_CHUNK)return sc32_invalid(SC32_E_RANGE,SC32_IMAGE_OP_STEP);
    Sc32ImageJob *j=sc32_image_find(request);
    if(!j)return sc32_invalid(SC32_E_STALE,SC32_IMAGE_OP_STEP);
    if(j->error)return sc32_invalid(j->error,SC32_IMAGE_OP_STEP);
    if(!j->memory || j->sealed)return ScImageDecodeQuery(request,out);
    if(sc32_overlap(out,out->size,j->input,j->source_bytes))return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_STEP);
    if(!j->legacy){Sc32ImageBegin input={sizeof(input),1,j->source_bytes,0};
        Sc32ImageTicket reply={sizeof(reply),1,0,0};
        s=sc32_native(sc32_call(0x300,SC32_I_BEGIN,(ScI32)&input,(ScI32)&reply,0,0,0),SC32_IMAGE_OP_STEP);
        if(s<0){j->error=s;
            j->input=0;
            return s;
        }j->legacy=reply.ticket;
    }
    ScU32 count=j->source_bytes-j->position;
    if(count>byte_budget)count=byte_budget;
    Sc32ImageTransfer input={sizeof(input),1,j->legacy,j->position,(ScU32)(j->input+j->position),count};
    s=sc32_native(sc32_call(0x300,SC32_I_WRITE,(ScI32)&input,0,0,0,0),SC32_IMAGE_OP_STEP);
    if(s<0){j->error=s;
        j->input=0;
        return s;
    }j->position+=count;
    if(j->position==j->source_bytes){Sc32ImageTicket seal={sizeof(seal),1,j->legacy,0};
        s=sc32_native(sc32_call(0x300,SC32_I_SEAL,(ScI32)&seal,0,0,0,0),SC32_IMAGE_OP_STEP);
        if(s<0){j->error=s;
            j->input=0;
            return s;
        }j->sealed=1;
        j->input=0;
    }
    sc32_image_info(j,out);
    return SC32_PENDING;
}
ScStatus ScImageDecodeQuery(ScImageRequest request,ScImageInfo *out)
{
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_QUERY);
    Sc32ImageJob *j=sc32_image_find(request);
    if(!j)return sc32_invalid(SC32_E_STALE,SC32_IMAGE_OP_QUERY);
    if(j->error)return sc32_invalid(j->error,SC32_IMAGE_OP_QUERY);
    if(j->memory && !j->sealed){if(sc32_overlap(out,out->size,j->input,j->source_bytes))return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_QUERY);
        sc32_image_info(j,out);
        return SC32_PENDING;
    }
    s=sc32_image_source(j);
    if(s<0){j->error=s;
        return sc32_invalid(s,SC32_IMAGE_OP_QUERY);
    }
    if(j->ready){sc32_image_info(j,out);
        return 0;
    }
    if(j->kind){
        unsigned char *bytes=(unsigned char *)(j+1);
        ScU32 count=j->source_bytes-j->body-j->position;
        if(count>SC32_IMAGE_CHUNK)count=SC32_IMAGE_CHUNK;
        ScIoResult io;
        sc32_zero(&io,sizeof(io));
        io.size=sizeof(io);
        io.version=1;
        s=ScFileRead(j->file,bytes,count,&io);
        if(s<0 || io.transferred!=count){j->error=s<0?s:SC32_E_IO;
            return sc32_invalid(j->error,SC32_IMAGE_OP_QUERY);
        }
        if(j->kind==1)for(ScU32 i=0;i<count;i++)if(bytes[i]>104u){j->error=SC32_E_INVALID;
            return sc32_invalid(j->error,SC32_IMAGE_OP_QUERY);
        }
        j->position+=count;
        j->ready=j->position==j->source_bytes-j->body;
        sc32_image_info(j,out);
        return j->ready?0:SC32_PENDING;
    }
    ScU32 raw[8];
    sc32_zero(raw,sizeof(raw));
    ScI32 result=sc32_call(0x7d,(ScI32)j->legacy,(ScI32)raw,0,0,0,0);
    if(result==1){sc32_image_info(j,out);
        return SC32_PENDING;
    }
    if(result<0){s=sc32_image_error(result,SC32_IMAGE_OP_QUERY);
        j->error=s;
        return s;
    }
    if(raw[0]!=1 || !raw[1] || !raw[2] || raw[1]>1920 || raw[2]>1080 || raw[3]!=raw[1]*raw[2]*4u
    || raw[4]!=2 || raw[5]!=j->legacy || raw[6] || raw[7]){j->error=SC32_E_INVALID;
        return sc32_invalid(j->error,SC32_IMAGE_OP_QUERY);
    }
    j->width=raw[1];
    j->height=raw[2];
    j->ready=1;
    sc32_image_info(j,out);
    return 0;
}
ScStatus ScImageDecodeCopy(ScImageRequest request,void *pixels,ScU32 capacity,ScImageInfo *out)
{
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_COPY);
    if(!pixels || !capacity)return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_COPY);
    s=sc32_out(pixels,capacity);
    if(s<0)return sc32_invalid(s,SC32_IMAGE_OP_COPY);
    if(sc32_overlap(pixels,capacity,out,out->size))return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_COPY);
    ScImageInfo info;
    sc32_zero(&info,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScImageDecodeQuery(request,&info);
    if(s)return s;
    Sc32ImageJob *j=sc32_image_find(request);
    if(!j || capacity<info.bytes)return sc32_invalid(SC32_E_RANGE,SC32_IMAGE_OP_COPY);
    if(sc32_overlap(pixels,capacity,j,sizeof(*j)+SC32_IMAGE_CHUNK))return sc32_invalid(SC32_E_INVALID,SC32_IMAGE_OP_COPY);
    if(j->kind){ScFileInfo fi;
        sc32_zero(&fi,sizeof(fi));
        fi.size=sizeof(fi);
        fi.version=1;
        s=ScFileSeek(j->file,(ScI32)j->body,SC32_SEEK_SET,&fi);
        if(s<0)return s;
        ScU32 at=0,total=j->source_bytes-j->body;
        unsigned char *buffer=(unsigned char *)(j+1);
        while(at<total){s=sc32_image_source(j);
            if(s<0)break;
            ScU32 count=total-at;
            if(count>SC32_IMAGE_CHUNK)count=SC32_IMAGE_CHUNK;
            ScIoResult io;
            sc32_zero(&io,sizeof(io));
            io.size=sizeof(io);
            io.version=1;
            s=ScFileRead(j->file,buffer,count,&io);
            if(s<0 || io.transferred!=count){if(s>=0)s=SC32_E_IO;
                break;
            }
            if(j->kind==2)sc32_copy((unsigned char *)pixels+at,buffer,count);
            else for(ScU32 i=0;i<count;i++){if(buffer[i]>104u){s=SC32_E_INVALID;
                    break;
                }((ScU32 *)pixels)[at+i]=0xff000000u|j->palette[buffer[i]];
            }
            if(s<0)break;
            at+=count;
            if(at<total)(void)ScProcessYield();
        }
    }else{ScU32 raw[8];
        ScI32 result=sc32_call(0x7d,(ScI32)j->legacy,(ScI32)raw,(ScI32)pixels,(ScI32)capacity,0,0);
        if(result)s=result<0?sc32_image_error(result,SC32_IMAGE_OP_COPY):SC32_E_AGAIN;
        else j->legacy=0;
    }
    if(s==0)s=sc32_image_source(j);
    if(s<0){j->error=s;
        return sc32_invalid(s,SC32_IMAGE_OP_COPY);
    }
    sc32_copy(out,&info,sizeof(info));
    sc32_image_destroy(j);
    return 0;
}
ScStatus ScImageDecodeCancel(ScImageRequest request)
{Sc32ImageJob *j=sc32_image_find(request);
    if(!j)return sc32_invalid(SC32_E_STALE,SC32_IMAGE_OP_CANCEL);
    sc32_image_destroy(j);
    return 0;
}
