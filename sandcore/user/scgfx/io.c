#include "GFXPRIVATE.H"

static void gfx_lease_control(const GfxLease *lease,ScLeaseControl *c)
{gfx_record(c,sizeof(*c));
    c->lease=lease->info.lease;
    c->generation=lease->info.generation;
    c->operation_sequence=lease->info.operation_sequence;
}
static ScStatus gfx_lease_result(GfxLease *lease,ScStatus status,const ScLeaseInfo *reply)
{
    if(reply->size==sizeof(*reply) && reply->version==1 && reply->lease && reply->generation){lease->info=*reply;
        lease->valid=1;
        if(!lease->closing && reply->file)lease->file=reply->file;
    }
    if((status==0 || status==SC32_PENDING) && !lease->valid)return SC32_E_FAILED;
    return status;
}
static ScStatus gfx_lease_open(GfxLease *lease,const char *path,ScU32 revision,ScU32 writing,ScU32 create)
{
    ScLeaseOpenDesc d;
    gfx_record(&d,sizeof(d));
    d.path=path;
    d.object_kind=SC32_PATH_FILE;
    d.lease_kind=writing?SC32_LEASE_WRITE_EXCLUSIVE:SC32_LEASE_READ_STABLE;
    d.expected_revision=revision;
    d.open_flags=writing?SC32_FILE_WRITE:SC32_FILE_READ;
    d.create_mode=create?3:0;
    ScLeaseInfo reply;
    gfx_record(&reply,sizeof(reply));
    ScStatus status=create?ScFileLeaseCreatePinned(&d,&reply):ScFileLeaseOpenPinned(&d,&reply);
    return gfx_lease_result(lease,status,&reply);
}
static ScStatus gfx_lease_advance(GfxLease *lease)
{ScLeaseControl c;
    gfx_lease_control(lease,&c);
    ScLeaseInfo reply;
    gfx_record(&reply,sizeof(reply));
    return gfx_lease_result(lease,ScFileLeaseAdvance(&c,&reply),&reply);
}
static ScStatus gfx_lease_query(GfxLease *lease)
{ScLeaseControl c;
    gfx_lease_control(lease,&c);
    ScLeaseInfo reply;
    gfx_record(&reply,sizeof(reply));
    return gfx_lease_result(lease,ScFileLeaseQuery(&c,&reply),&reply);
}
static ScStatus gfx_lease_cleanup(ScGfxOperation *o)
{
    GfxLease *leases[2]={&o->candidate_lease,&o->target_lease};
    for(ScU32 i=0;i<2;i++){GfxLease *l=leases[i];
        if(!l->valid)continue;
        if(l->info.state==SC32_LEASE_PENDING_STATE){ScLeaseControl c;
            gfx_lease_control(l,&c);
            ScLeaseInfo reply;
            gfx_record(&reply,sizeof(reply));
            ScStatus s=ScFileLeaseCancel(&c,&reply);
            gfx_lease_result(l,s,&reply);
            if(s==SC32_PENDING){(void)gfx_lease_advance(l);
                return SC32_PENDING;
        }}
        if(!l->closing){if(l->file){ScStatus s=ScFileClose(l->file);
                if(s==SC32_E_BUSY || s==SC32_E_AGAIN)return SC32_PENDING;
                if(o->file==l->file)o->file=0;
                l->file=0;
            }l->closing=1;
        }
        ScLeaseControl c;
        gfx_lease_control(l,&c);
        ScLeaseInfo reply;
        gfx_record(&reply,sizeof(reply));
        ScStatus s=ScFileLeaseRelease(&c,&reply);
        if(s==SC32_PENDING){gfx_lease_result(l,s,&reply);
            return SC32_PENDING;
        }
        if(s<0 && s!=SC32_E_STALE){if(!o->cause)o->cause=s;
            return s;
        }memset(l,0,sizeof(*l));
    }
    return 0;
}
static int gfx_magic(const unsigned char *p,const char *text)
{return !memcmp(p,text,8);
}
static ScStatus gfx_path(const char *input,char output[65])
{ScU32 bytes=0;
    ScStatus s=ScPathResolve(input,output,65,&bytes);
    if(s<0)return s;
    return ScUtf8Validate(output,bytes,0)<0?SC_GFX_E_INVALID:0;
}
ScStatus ScGfxLoadBegin(const char *path,ScGfxOperation **out)
{
    ScGfxOperation *o=0;
    ScStatus status=gfx_operation_new(GFX_LOAD,0,&o);
    if(status<0)return status;
    status=gfx_path(path,o->path);
    ScPathInfo info;
    gfx_record(&info,sizeof(info));
    if(status==0)status=ScPathQuery(o->path,&info);
    if(status==0 && (info.kind!=SC32_PATH_FILE || info.bytes<24 || info.bytes>16u*1024u*1024u))status=SC_GFX_E_FORMAT;
    if(status==0){o->file_revision=info.generation;
        o->file_bytes=info.bytes;
        status=gfx_lease_open(&o->candidate_lease,o->path,info.generation,0,0);
    }
    if(status<0){o->state=SC_GFX_OP_FAILED;
        o->status=SC_GFX_E_IO;
        o->cause=status;
         *out=o;
        return 0;
    }
    o->buffer_bytes=65536;
    o->buffer=malloc(o->buffer_bytes);
    if(!o->buffer){gfx_operation_fail(o,SC_GFX_E_MEMORY,0);
         *out=o;
        return 0;
    }*out=o;
    return 0;
}
ScStatus ScGfxLoadMemoryBegin(const void *data,ScU32 bytes,ScGfxOperation **out)
{
    ScGfxOperation *o=0;
    ScStatus s=gfx_operation_new(GFX_LOAD,0,&o);
    if(s<0)return s;
    ScImageMemoryDesc d;
    gfx_record(&d,sizeof(d));
    d.data=data;
    d.bytes=bytes;
    s=ScImageDecodeMemoryBegin(&d,&o->request);
    if(s<0)gfx_operation_fail(o,SC_GFX_E_IO,s);
    else{o->format=SC_GFX_FILE_COMPRESSED;
        o->phase=4;
    }
     *out=o;
    return 0;
}
static ScStatus gfx_load_header(ScGfxOperation *o)
{
    ScStatus status=o->candidate_lease.info.state==SC32_LEASE_PENDING_STATE?gfx_lease_advance(&o->candidate_lease):0;
    if(status==SC32_PENDING)return SC_GFX_PENDING;
    if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
    o->file=o->candidate_lease.file;
    if(!o->file)return gfx_operation_fail(o,SC_GFX_E_IO,SC32_E_FAILED);
    ScIoResult io;
    gfx_record(&io,sizeof(io));
    status=ScFileRead(o->file,o->header,64,&io);
    if(status<0 || io.transferred<24)return gfx_operation_fail(o,SC_GFX_E_IO,status<0?status:SC32_E_IO);
    const unsigned char *p=o->header;
    ScU32 width=0,height=0;
    if(gfx_magic(p,"SCB1MIO")){
        width=gfx_u32(p+8);
        height=gfx_u32(p+12);
        o->bpp=1;
        o->header_bytes=24;
        o->format=SC_GFX_FILE_SCB1;
        if(gfx_u32(p+16)!=1 || gfx_u32(p+20)!=0x004f494du)return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
        status=ScDisplayPalette(o->palette,256);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
    }else if(io.transferred>=32 && gfx_magic(p,"SCB2MIO")){
        width=gfx_u32(p+8);
        height=gfx_u32(p+12);
        o->bpp=4;
        o->header_bytes=32;
        o->format=SC_GFX_FILE_SCB2;
        if(gfx_u32(p+16)!=2 || gfx_u32(p+20) || gfx_u32(p+28)!=0x004f494du)return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
    }else if(io.transferred>=54 && p[0]=='B' && p[1]=='M'){
        ScU32 dib=gfx_u32(p+14);
        ScI32 signed_height=(ScI32)gfx_u32(p+22);
        width=gfx_u32(p+18);
        if(signed_height==INT_MIN || dib<40 || dib>o->file_bytes-14u || gfx_u16(p+26)!=1 || (gfx_u16(p+28)!=24 && gfx_u16(p+28)!=32) || gfx_u32(p+30))return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
        height=(ScU32)(signed_height<0?-signed_height:signed_height);
        o->top_down=signed_height<0;
        o->bpp=gfx_u16(p+28)/8u;
        o->header_bytes=gfx_u32(p+10);
        o->format=SC_GFX_FILE_BMP;
        if(o->header_bytes<14u+dib)return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
    }else{
        int compressed=(p[0]==255 && p[1]==216)||(p[0]==137 && p[1]=='P' && p[2]=='N' && p[3]=='G')
        ||(p[0]=='R' && p[1]=='I' && p[2]=='F' && p[3]=='F' && p[8]=='W' && p[9]=='E' && p[10]=='B' && p[11]=='P');
        if(!compressed)return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
        ScImageDecodeDesc d;
        gfx_record(&d,sizeof(d));
        d.path=o->path;
        status=ScImageDecodeBegin(&d,&o->request);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        o->format=SC_GFX_FILE_COMPRESSED;
        o->phase=3;
        return SC_GFX_PENDING;
    }
    if(!width || !height || width>SC_GFX_MAX_WIDTH || height>SC_GFX_MAX_HEIGHT || width>SC_GFX_MAX_PIXELS/height)return gfx_operation_fail(o,SC_GFX_E_RANGE,0);
    o->source_width=width;
    o->source_height=height;
    o->stride=(width*o->bpp+3u)&~3u;
    if(o->format!=SC_GFX_FILE_BMP)o->stride=width*o->bpp;
    if(o->header_bytes>o->file_bytes || o->stride*height>o->file_bytes-o->header_bytes
    || (o->format!=SC_GFX_FILE_BMP && o->file_bytes!=o->header_bytes+o->stride*height)
    || (o->format==SC_GFX_FILE_SCB2 && gfx_u32(p+24)!=o->stride*height))return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
    ScGfxSurfaceDesc d;
    gfx_record(&d,sizeof(d));
    d.width=width;
    d.height=height;
    status=gfx_surface_allocate(&d,&o->result);
    if(status<0)return gfx_operation_fail(o,status,status);
    o->total=width*height;
    o->phase=2;
    return SC_GFX_PENDING;
}
static ScStatus gfx_load_step(ScGfxOperation *o,ScU32 budget)
{
    if(o->cancel)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    if(!o->phase)return gfx_load_header(o);
    if(o->phase==3 || o->phase==4){ScImageInfo info;
        gfx_record(&info,sizeof(info));
        ScU32 bytes=budget>16384u?65536u:budget*4u;
        ScStatus s=o->phase==4?ScImageDecodeStep(o->request,bytes,&info):ScImageDecodeQuery(o->request,&info);
        if(s==SC32_PENDING){o->wait_events=info.wait_events;
            return SC_GFX_PENDING;
        }if(s<0)return gfx_operation_fail(o,SC_GFX_E_IO,s);
        ScGfxSurfaceDesc d;
        gfx_record(&d,sizeof(d));
        d.width=info.width;
        d.height=info.height;
        s=gfx_surface_allocate(&d,&o->result);
        if(s<0)return gfx_operation_fail(o,s,s);
        gfx_record(&info,sizeof(info));
        s=ScImageDecodeCopy(o->request,o->result->pixels,o->result->bytes,&info);
        if(s<0)return gfx_operation_fail(o,SC_GFX_E_IO,s);
        o->request=0;
        o->total=o->progress=info.width*info.height;
        o->state=SC_GFX_OP_READY;
        o->wait_events=0;
        return 0;
    }
    ScU32 work=0;
    while(o->progress<o->total && work<budget){ScU32 y=o->progress/o->source_width;
        if(!o->scan_x){ScU32 file_y=o->format==SC_GFX_FILE_BMP && !o->top_down?o->source_height-1u-y:y;
            ScFileInfo pos;
            gfx_record(&pos,sizeof(pos));
            ScStatus status=ScFileSeek(o->file,(ScI32)(o->header_bytes+file_y*o->stride),SC32_SEEK_SET,&pos);
            if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
            ScIoResult io;
            gfx_record(&io,sizeof(io));
            status=ScFileRead(o->file,o->buffer,o->stride,&io);
            if(status<0 || io.transferred!=o->stride)return gfx_operation_fail(o,SC_GFX_E_IO,status<0?status:SC32_E_IO);
        }
        while((ScU32)o->scan_x<o->source_width && work<budget){ScU32 x=(ScU32)o->scan_x++,color;
            if(o->format==SC_GFX_FILE_SCB1){if(o->buffer[x]>104u)return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
                color=0xff000000u|o->palette[o->buffer[x]];
            }
            else if(o->format==SC_GFX_FILE_SCB2)color=gfx_u32(o->buffer+x*4u);
            else{unsigned char *p=o->buffer+x*o->bpp;
                color=0xff000000u|((ScU32)p[2]<<16)|((ScU32)p[1]<<8)|p[0];
            }
            o->result->pixels[o->progress++]=color;
            work++;
        }
        if((ScU32)o->scan_x==o->source_width)o->scan_x=0;
    }
    if(o->progress<o->total)return SC_GFX_PENDING;
    o->state=SC_GFX_OP_READY;
    return 0;
}
static void gfx_scb_header(unsigned char *out,ScU32 width,ScU32 height)
{memset(out,0,32);
    memcpy(out,"SCB2MIO",8);
    gfx_put32(out+8,width);
    gfx_put32(out+12,height);
    gfx_put32(out+16,2);
    gfx_put32(out+24,width*height*4u);
    gfx_put32(out+28,0x004f494du);
}
ScStatus ScGfxSaveBegin(ScGfxSurface *surface,const char *path,ScU32 format,ScU32 expected,ScGfxOperation **out)
{
    if(format!=SC_GFX_FILE_SCB2 && format!=SC_GFX_FILE_BMP)return SC_GFX_E_NOT_SUPPORTED;
    ScGfxOperation *o=0;
    ScStatus status=gfx_operation_new(GFX_SAVE,surface,&o);
    if(status<0)return status;
    status=gfx_path(path,o->path);
    if(status<0){ScGfxSurfaceRelease(surface);
        free(o);
        return status;
    }
    o->format=format;
    o->expected_destination_revision=expected;
    o->final_mode=23;
    o->buffer_bytes=65536;
    o->buffer=malloc(o->buffer_bytes);
    if(!o->buffer){ScGfxSurfaceRelease(surface);
        free(o);
        return SC_GFX_E_MEMORY;
    }
    if(format==SC_GFX_FILE_SCB2){o->header_bytes=32;
        gfx_scb_header(o->header,surface->width,surface->height);
        o->stride=surface->stride;
    }
    else{status=ScDisplayPalette(o->palette,256);
        if(status<0){free(o->buffer);
            ScGfxSurfaceRelease(surface);
            free(o);
            return status;
        }o->color=0xff000000u|o->palette[100];
        o->header_bytes=54;
        o->stride=(surface->width*3u+3u)&~3u;
        memset(o->header,0,54);
        o->header[0]='B';
        o->header[1]='M';
        gfx_put32(o->header+2,54u+o->stride*surface->height);
        gfx_put32(o->header+10,54);
        gfx_put32(o->header+14,40);
        gfx_put32(o->header+18,surface->width);
        gfx_put32(o->header+22,0u-surface->height);
        gfx_put16(o->header+26,1);
        gfx_put16(o->header+28,24);
        gfx_put32(o->header+34,o->stride*surface->height);
    }
    o->total=o->header_bytes+o->stride*surface->height;
    surface->readers++;
    o->flags|=1u;
     *out=o;
    return 0;
}
static ScStatus gfx_save_candidate(ScGfxOperation *o)
{
    ScU32 n=(ScU32)strlen(o->path);
    while(n && o->path[n-1]!='/')n--;
    if(!n || n>=64)return SC_GFX_E_RANGE;
    ScU32 width=64u-n;
    if(width>7)width=7;
    ScU32 limit=1;
    for(ScU32 i=0;i<width;i++){if(limit>UINT_MAX/36u){limit=0;
            break;
        }limit*=36u;
    }
    o->candidate_limit=limit;
    if((limit && o->candidate_sequence>=limit) || o->candidate_sequence==UINT_MAX)return SC_GFX_E_LIMIT;
    ScProcessRef self;
    gfx_record(&self,sizeof(self));
    ScStatus s=ScProcessSelf(&self);
    if(s<0)return s;
    ScU32 value=(self.pid*2654435761u+self.generation+o->candidate_sequence++);
    if(limit)value%=limit;
    memcpy(o->candidate,o->path,n);
    static const char digits[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for(ScU32 i=0;i<width;i++){o->candidate[n+width-1u-i]=digits[value%36u];
        value/=36u;
    }o->candidate[n+width]=0;
    if(!strcmp(o->candidate,o->path))return SC_GFX_PENDING;
    s=gfx_lease_open(&o->candidate_lease,o->candidate,0,1,1);
    return s==SC32_E_EXISTS?SC_GFX_PENDING:s;
}
static ScStatus gfx_save_publish(ScGfxOperation *o)
{
    GfxLease *c=&o->candidate_lease;
    ScLeasePublishDesc d;
    gfx_record(&d,sizeof(d));
    d.source_lease=c->info.lease;
    d.source_generation=c->info.generation;
    d.expected_source_object=c->info.object_id;
    d.expected_source_revision=c->info.revision;
    d.target_path=o->path;
    d.flags=SC32_LEASE_EXPECT_ABSENT|SC32_LEASE_SET_MODE;
    d.final_mode=o->final_mode;
    if(o->target_lease.valid){GfxLease *t=&o->target_lease;
        /* 覆盖已有图像继承目标权限和所有者，不能把临时文件的身份带入。 */
        d.flags=SC32_LEASE_EXPECT_EXACT;
        d.final_mode=0;
        d.target_lease=t->info.lease;
        d.target_generation=t->info.generation;
        d.expected_target_object=t->info.object_id;
        d.expected_target_revision=t->info.revision;
    }
    ScLeaseInfo reply;
    gfx_record(&reply,sizeof(reply));
    ScStatus status=o->target_lease.valid?ScFileLeasePublishPreserving(&d,&reply):ScFileLeasePublish(&d,&reply);
    return gfx_lease_result(c,status,&reply);
}
static ScStatus gfx_save_step(ScGfxOperation *o,ScU32 budget)
{
    ScGfxSurface *s=o->surface;
    if(o->cancel && o->phase<5)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    if(o->phase==0){ScPathInfo info;
        gfx_record(&info,sizeof(info));
        ScStatus status=ScPathQuery(o->path,&info);
        if(status==SC32_E_NOT_FOUND){if(o->expected_destination_revision)return gfx_operation_fail(o,SC_GFX_E_STALE,status);
            o->phase=1;
            return SC_GFX_PENDING;
        }
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        if(info.kind!=SC32_PATH_FILE || !o->expected_destination_revision || info.generation!=o->expected_destination_revision)return gfx_operation_fail(o,SC_GFX_E_STALE,SC32_E_STALE);
        o->final_mode=info.rw;
        status=gfx_lease_open(&o->target_lease,o->path,info.generation,1,0);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        o->phase=1;
        return SC_GFX_PENDING;
    }
    if(o->phase==1){if(o->target_lease.valid && o->target_lease.info.state==SC32_LEASE_PENDING_STATE){ScStatus status=gfx_lease_advance(&o->target_lease);
            if(status==SC32_PENDING)return SC_GFX_PENDING;
            if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        }
        ScStatus status=gfx_save_candidate(o);
        if(status==SC32_PENDING && !o->candidate_lease.valid)return SC_GFX_PENDING;
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        o->phase=2;
        return SC_GFX_PENDING;
    }
    if(o->phase==2){if(o->candidate_lease.info.state==SC32_LEASE_PENDING_STATE){ScStatus status=gfx_lease_advance(&o->candidate_lease);
            if(status==SC32_PENDING)return SC_GFX_PENDING;
            if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        }
        o->file=o->candidate_lease.file;
        if(!o->file)return gfx_operation_fail(o,SC_GFX_E_IO,SC32_E_FAILED);
        o->phase=3;
    }
    if(o->phase==3){ScU32 bytes=budget*4u;
        if(bytes>65536)bytes=65536;
        if(bytes>o->total-o->progress)bytes=o->total-o->progress;
        ScU32 cached_pixel=UINT_MAX,cached_color=0;
        for(ScU32 i=0;i<bytes;i++){ScU32 at=o->progress+i;
            if(at<o->header_bytes)o->buffer[i]=o->header[at];
            else if(o->format==SC_GFX_FILE_SCB2){ScU32 p=at-o->header_bytes;
                o->buffer[i]=(unsigned char)(s->pixels[p/4u]>>(8u*(p&3u)));
            }
            else{ScU32 p=at-54u,y=p/o->stride,x=p%o->stride;
                if(x>=s->width*3u)o->buffer[i]=0;
                else{ScU32 index=y*s->width+x/3u;
                    if(index!=cached_pixel){cached_pixel=index;
                        cached_color=ScGfxArgbOver(s->pixels[index],o->color);
                    }o->buffer[i]=(unsigned char)(cached_color>>(8u*(x%3u)));
        }}}
        ScIoResult io;
        gfx_record(&io,sizeof(io));
        ScStatus status=ScFileWrite(o->file,o->buffer,bytes,&io);
        if(status<0 || io.transferred!=bytes)return gfx_operation_fail(o,SC_GFX_E_IO,status<0?status:SC32_E_IO);
        o->progress+=bytes;
        if(o->progress<o->total)return SC_GFX_PENDING;
        o->phase=4;
        return SC_GFX_PENDING;
    }
    if(o->phase==4){ScStatus status=ScFileFlush(o->file);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        status=gfx_lease_query(&o->candidate_lease);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        status=gfx_save_publish(o);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        o->phase=5;
        return SC_GFX_PENDING;
    }
    if(o->phase==5){GfxLease *l=&o->candidate_lease;
        ScStatus status=0;
        if(o->cancel && l->info.state==SC32_LEASE_PENDING_STATE){ScLeaseControl c;
            gfx_lease_control(l,&c);
            ScLeaseInfo reply;
            gfx_record(&reply,sizeof(reply));
            status=ScFileLeaseCancel(&c,&reply);
            gfx_lease_result(l,status,&reply);
        }
        if(l->info.state==SC32_LEASE_PENDING_STATE)status=gfx_lease_advance(l);
        if(status==SC32_PENDING)return SC_GFX_PENDING;
        if(l->info.state==SC32_LEASE_COMMITTED && (l->info.flags&SC32_LEASE_FLAG_DURABLE)){
            o->flags|=4u;
            o->file_revision=l->info.revision;
            o->state=SC_GFX_OP_READY;
            if(o->format==SC_GFX_FILE_SCB2)ScGfxSurfaceMarkSaved(s,o->revision);
            return 0;
        }
        return gfx_operation_fail(o,o->cancel?SC_GFX_E_CANCELLED:SC_GFX_E_IO,status<0?status:SC32_E_FAILED);
    }
    return SC_GFX_E_INVALID;
}
