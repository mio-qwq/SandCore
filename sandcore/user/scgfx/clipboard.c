#include "GFXPRIVATE.H"

ScStatus ScGfxClipboardCopyBegin(ScGfxSurface *s,const ScGfxRect *rectangle,ScU32 expected,ScGfxOperation **out)
{
    if(!gfx_surface(s))return SC_GFX_E_STALE;
    ScGfxRect all=gfx_rect(0,0,(ScI32)s->width,(ScI32)s->height),r;
    if(!gfx_clip_rect(s,rectangle?rectangle:&all,&r) || !r.width || !r.height)return SC_GFX_E_RANGE;
    ScGfxOperation *o=0;
    ScStatus status=gfx_operation_new(GFX_COPY,s,&o);
    if(status<0)return status;
    o->rectangle=r;
    o->clipboard_epoch=expected;
    o->total=(ScU32)r.width*(ScU32)r.height;
    o->clipboard_total=32u+o->total*4u;
    o->buffer=malloc(o->clipboard_total);
    if(!o->buffer){ScGfxSurfaceRelease(s);
        free(o);
        return SC_GFX_E_MEMORY;
    }
    o->buffer_bytes=o->clipboard_total;
    memset(o->buffer,0,32);
    memcpy(o->buffer,"SCGFXA1",8);
    gfx_put32(o->buffer+8,(ScU32)r.width);
    gfx_put32(o->buffer+12,(ScU32)r.height);
    gfx_put32(o->buffer+16,(ScU32)r.width*4u);
    gfx_put32(o->buffer+20,o->total*4u);
    gfx_put32(o->buffer+24,1);
    s->readers++;
    o->flags|=1u;
     *out=o;
    return 0;
}
ScStatus ScGfxClipboardPasteBegin(ScGfxSurface *s,ScI32 x,ScI32 y,ScGfxOperation **out)
{
    if(!gfx_surface(s) || x<-(ScI32)SC_GFX_MAX_WIDTH || x>(ScI32)s->width || y<-(ScI32)SC_GFX_MAX_HEIGHT || y>(ScI32)s->height)return SC_GFX_E_RANGE;
    ScGfxOperation *o=0;
    ScStatus status=gfx_operation_new(GFX_PASTE,s,&o);
    if(status<0)return status;
    ScClipboardInfo info;
    gfx_record(&info,sizeof(info));
    status=ScClipboardOpen(&info);
    if(status<0){ScGfxSurfaceRelease(s);
        free(o);
        return status;
    }
    o->clipboard_snapshot=info.handle;
    o->clipboard_epoch=info.epoch;
    o->x=x;
    o->y=y;
    s->readers++;
    o->flags|=1u;
     *out=o;
    return 0;
}
static ScStatus gfx_clipboard_copy_step(ScGfxOperation *o,ScU32 budget)
{
    if(o->cancel)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    if(!o->phase){ScGfxSurface *s=o->surface;
        ScU32 end=o->total-o->progress;
        if(end>budget)end=budget;
        end+=o->progress;
        for(;o->progress<end;o->progress++){ScU32 x=o->progress%(ScU32)o->rectangle.width,y=o->progress/(ScU32)o->rectangle.width;
            ScU32 color=s->pixels[((ScU32)o->rectangle.y+y)*s->width+(ScU32)o->rectangle.x+x];
            gfx_put32(o->buffer+32u+o->progress*4u,color);
        }
        if(o->progress<o->total)return SC_GFX_PENDING;
        o->phase=1;
        return SC_GFX_PENDING;
    }
    if(o->phase==1){ScClipboardDesc d;
        gfx_record(&d,sizeof(d));
        d.budget_bytes=o->clipboard_total+4096u;
        ScStatus status=ScClipboardBegin(&d,&o->clipboard_candidate);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        o->phase=2;
        return SC_GFX_PENDING;
    }
    if(o->phase==2){ScStatus status=ScClipboardOffer(o->clipboard_candidate,SC_GFX_CLIPBOARD_ARGB,o->buffer,o->clipboard_total);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        o->phase=3;
        return SC_GFX_PENDING;
    }
    ScClipboardInfo info;
    gfx_record(&info,sizeof(info));
    ScStatus status=ScClipboardCommit(o->clipboard_candidate,o->clipboard_epoch,&info);
    if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
    o->clipboard_candidate=0;
    o->state=SC_GFX_OP_READY;
    return 0;
}
static ScStatus gfx_clipboard_paste_step(ScGfxOperation *o,ScU32 budget)
{
    if(o->cancel)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    if(!o->phase){ScClipboardFormatInfo formats[32];
        ScClipboardPage page;
        gfx_record(&page,sizeof(page));
        ScStatus status=ScClipboardFormats(o->clipboard_snapshot,o->offset,formats,32,&page);
        if(status<0)return gfx_operation_fail(o,SC_GFX_E_IO,status);
        for(ScU32 i=0;i<page.count;i++)if(formats[i].format==SC_GFX_CLIPBOARD_ARGB){o->clipboard_total=formats[i].bytes;
            o->phase=1;
            break;
        }
        if(!o->phase){if(page.flags&SC32_CLIPBOARD_FINISHED)return gfx_operation_fail(o,SC_GFX_E_NOT_SUPPORTED,0);
            o->offset=page.cursor;
            return SC_GFX_PENDING;
        }
        if(o->clipboard_total<32u || o->clipboard_total>32u+SC_GFX_MAX_PIXELS*4u)return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
        o->offset=0;
        return SC_GFX_PENDING;
    }
    if(o->phase==1){ScBrokerIoResult io;
        gfx_record(&io,sizeof(io));
        ScStatus status=ScClipboardRead(o->clipboard_snapshot,SC_GFX_CLIPBOARD_ARGB,0,o->header,32,&io);
        if(status<0 || io.transferred!=32)return gfx_operation_fail(o,SC_GFX_E_IO,status<0?status:SC32_E_IO);
        ScU32 w=gfx_u32(o->header+8),h=gfx_u32(o->header+12);
        if(memcmp(o->header,"SCGFXA1",8) || !w || !h || w>SC_GFX_MAX_WIDTH || h>SC_GFX_MAX_HEIGHT || w>SC_GFX_MAX_PIXELS/h
        || gfx_u32(o->header+16)!=w*4u || gfx_u32(o->header+20)!=w*h*4u || gfx_u32(o->header+24)!=1 || gfx_u32(o->header+28)
        || o->clipboard_total!=32u+w*h*4u)return gfx_operation_fail(o,SC_GFX_E_FORMAT,0);
        o->rectangle=gfx_rect(0,0,(ScI32)w,(ScI32)h);
        o->buffer_bytes=w*h*4u;
        o->buffer=malloc(o->buffer_bytes);
        if(!o->buffer)return gfx_operation_fail(o,SC_GFX_E_MEMORY,0);
        o->total=w*h;
        o->phase=2;
        return SC_GFX_PENDING;
    }
    if(o->phase==2){ScU32 bytes=budget*4u;
        if(bytes>65536u)bytes=65536u;
        if(bytes>o->buffer_bytes-o->offset)bytes=o->buffer_bytes-o->offset;
        ScBrokerIoResult io;
        gfx_record(&io,sizeof(io));
        ScStatus status=ScClipboardRead(o->clipboard_snapshot,SC_GFX_CLIPBOARD_ARGB,32u+o->offset,o->buffer+o->offset,bytes,&io);
        if(status<0 || io.transferred!=bytes)return gfx_operation_fail(o,SC_GFX_E_IO,status<0?status:SC32_E_IO);
        o->offset+=bytes;
        o->progress=o->offset/4u;
        if(o->offset<o->buffer_bytes)return SC_GFX_PENDING;
        o->surface->readers--;
        o->flags&=~1u;
        status=ScGfxEditBegin(o->surface,"Paste pixels",&o->edit);
        if(status<0)return gfx_operation_fail(o,status,status);
        o->surface->busy=o;
        o->phase=3;
        o->progress=0;
        return SC_GFX_PENDING;
    }
    ScU32 end=o->total-o->progress;
    if(end>budget)end=budget;
    end+=o->progress;
    for(;o->progress<end;o->progress++){ScU32 at=o->progress;
        ScI32 x=o->x+(ScI32)(at%(ScU32)o->rectangle.width),y=o->y+(ScI32)(at/(ScU32)o->rectangle.width);
        ScStatus status=ScGfxEditPixel(o->edit,x,y,gfx_u32(o->buffer+at*4u),SC_GFX_BLEND_OVER);
        if(status<0)return gfx_operation_fail(o,status,status);
    }
    gfx_epoch(o->surface);
    return o->progress==o->total?gfx_edit_finish(o):SC_GFX_PENDING;
}
static ScStatus gfx_clipboard_step(ScGfxOperation *o,ScU32 budget)
{return o->kind==GFX_COPY?gfx_clipboard_copy_step(o,budget):gfx_clipboard_paste_step(o,budget);
}
