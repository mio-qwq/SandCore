#include "GFXPRIVATE.H"

static ScStatus gfx_operation_new(ScU32 kind,ScGfxSurface *surface,ScGfxOperation **out)
{
    if(!out || (surface && !gfx_surface(surface)))return SC_GFX_E_INVALID;
    if(surface && (surface->busy || surface->edit || surface->readers))return SC_GFX_E_BUSY;
    ScGfxOperation *o=calloc(1,sizeof(*o));
    if(!o)return SC_GFX_E_MEMORY;
    o->magic=GFX_OP_MAGIC;
    o->kind=kind;
    o->state=SC_GFX_OP_RUNNING;
    o->surface=surface;
    if(surface){ScStatus status=ScGfxSurfaceRetain(surface);
        if(status<0){free(o);
            return status;
        }o->revision=surface->revision;
    }*out=o;
    return 0;
}
static ScStatus gfx_operation_fail(ScGfxOperation *o,ScStatus status,ScStatus cause)
{
    o->status=status;
    o->cause=cause;
    if(o->edit && o->edit->changed){o->flags|=2u;
        o->tile=o->edit->record->tiles;
        o->tile_pixel=o->progress=0;
        o->total=0;
        for(GfxTile *t=o->tile;t;t=t->next)o->total+=t->width*t->height;
        return SC_GFX_PENDING;
    }
    o->state=status==SC_GFX_E_CANCELLED?SC_GFX_OP_CANCELLED:SC_GFX_OP_FAILED;
    if(o->edit){gfx_edit_discard(o->edit,0);
        o->edit=0;
    }
    if(o->surface && o->surface->busy==o)o->surface->busy=0;
    return status;
}
static ScStatus gfx_rollback_step(ScGfxOperation *o,ScU32 budget)
{
    ScGfxSurface *s=o->surface;
    ScU32 work=0;
    while(o->tile && work<budget){GfxTile *t=o->tile;
        ScU32 at=o->tile_pixel;
        s->pixels[(t->y+at/t->width)*s->width+t->x+at%t->width]=gfx_tile_pixels(t)[at];
        o->tile_pixel++;
        o->progress++;
        work++;
        if(o->tile_pixel==t->width*t->height){o->tile=t->next;
            o->tile_pixel=0;
    }}
    if(work)gfx_epoch(s);
    if(o->tile)return SC_GFX_PENDING;
    gfx_edit_discard(o->edit,0);
    o->edit=0;
    s->busy=0;
    o->flags&=~2u;
    o->state=o->status==SC_GFX_E_CANCELLED?SC_GFX_OP_CANCELLED:SC_GFX_OP_FAILED;
    return o->status;
}
static void gfx_history_move(ScGfxSurface *s,GfxHistory *h,ScU32 direction)
{
    GfxHistory **from=direction==SC_GFX_HISTORY_UNDO?&s->undo:&s->redo;
    GfxHistory **from_last=direction==SC_GFX_HISTORY_UNDO?&s->undo_last:&s->redo_last;
    GfxHistory **to=direction==SC_GFX_HISTORY_UNDO?&s->redo:&s->undo;
    GfxHistory **to_last=direction==SC_GFX_HISTORY_UNDO?&s->redo_last:&s->undo_last;
     *from=h->next;
    if(*from)(*from)->previous=0;
    else *from_last=0;
    h->previous=0;
    h->next=*to;
    if(*to)(*to)->previous=h;
    else *to_last=h;
     *to=h;
    if(direction==SC_GFX_HISTORY_UNDO){s->undo_count--;
        s->redo_count++;
        s->revision=h->before_revision;
    }
    else{s->redo_count--;
        s->undo_count++;
        s->revision=h->after_revision;
    }
    gfx_epoch(s);
}
ScStatus ScGfxHistoryBegin(ScGfxSurface *s,ScU32 direction,ScGfxOperation **out)
{
    if(!gfx_surface(s) || (direction!=1 && direction!=2))return SC_GFX_E_INVALID;
    GfxHistory *h=direction==1?s->undo:s->redo;
    if(!h)return SC_GFX_END;
    ScGfxOperation *o=0;
    ScStatus status=gfx_operation_new(GFX_HISTORY,s,&o);
    if(status<0)return status;
    o->history=h;
    o->history_direction=direction;
    o->tile=h->tiles;
    o->total=0;
    if(h->image)o->total=1;
    else for(GfxTile *t=h->tiles;t;t=t->next)o->total+=t->width*t->height;
    s->busy=o;
     *out=o;
    return 0;
}
static ScStatus gfx_history_step(ScGfxOperation *o,ScU32 budget)
{
    ScGfxSurface *s=o->surface;
    GfxHistory *h=o->history;
    if(h->image){if(o->cancel)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
        ScU32 *image=s->pixels,w=s->width,height=s->height,bytes=s->bytes;
        s->pixels=h->image;
        s->width=h->image_width;
        s->height=h->image_height;
        s->bytes=h->image_bytes;
        s->stride=s->width*4u;
        h->image=image;
        h->image_width=w;
        h->image_height=height;
        h->image_bytes=bytes;
        gfx_history_move(s,h,o->history_direction);
        s->busy=0;
        o->progress=o->total;
        o->state=SC_GFX_OP_READY;
        return 0;
    }
    if(o->cancel && !o->history_rollback){
        /* 已交换的前缀再次交换即可精确回滚，不改变history链/保存
         * revision。回滚也按预算推进，取消一幅高清Undo不堵住UI。 */
        o->history_rollback=1;
        o->history_swapped=o->progress;
        o->progress=0;
        o->tile=h->tiles;
        o->tile_pixel=0;
    }
    ScU32 target=o->history_rollback?o->history_swapped:o->total,work=0;
    while(o->tile && o->progress<target && work<budget){GfxTile *t=o->tile;
        ScU32 at=o->tile_pixel,x=at%t->width,y=at/t->width,*pixel=s->pixels+(t->y+y)*s->width+t->x+x;
        ScU32 value=*pixel;
         *pixel=gfx_tile_pixels(t)[at];
        gfx_tile_pixels(t)[at]=value;
        o->progress++;
        o->tile_pixel++;
        work++;
        if(o->tile_pixel==t->width*t->height){o->tile=t->next;
            o->tile_pixel=0;
        }
    }
    if(work)gfx_epoch(s);
    if(o->progress<target)return SC_GFX_PENDING;
    if(o->history_rollback)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    gfx_history_move(s,h,o->history_direction);
    s->busy=0;
    o->state=SC_GFX_OP_READY;
    return 0;
}
static ScStatus gfx_edit_operation(ScGfxSurface *s,ScU32 kind,const char *label,ScGfxOperation **out)
{
    ScGfxOperation *o=0;
    ScStatus status=gfx_operation_new(kind,s,&o);
    if(status<0)return status;
    status=ScGfxEditBegin(s,label,&o->edit);
    if(status<0){ScGfxSurfaceRelease(s);
        free(o);
        return status;
    }
    s->busy=o;
     *out=o;
    return 0;
}
ScStatus ScGfxClearBegin(ScGfxSurface *s,ScU32 color,ScGfxOperation **out)
{if(!gfx_surface(s))return SC_GFX_E_STALE;
    ScGfxRect r={0,0,(ScI32)s->width,(ScI32)s->height};
    return ScGfxClearRectBegin(s,&r,color,out);
}
ScStatus ScGfxClearRectBegin(ScGfxSurface *s,const ScGfxRect *rect,ScU32 color,ScGfxOperation **out)
{if(!gfx_surface(s))return SC_GFX_E_STALE;
    ScGfxRect r;
    if(!gfx_clip_rect(s,rect,&r))return SC_GFX_E_RANGE;
    ScGfxOperation *o=0;
    ScStatus status=gfx_edit_operation(s,GFX_CLEAR,"Clear selection",&o);
    if(status<0)return status;
    o->color=color;
    o->rectangle=r;
    o->total=(ScU32)r.width*(ScU32)r.height;
     *out=o;
    return 0;
}
static ScStatus gfx_queue(ScGfxOperation *o,ScI32 x,ScI32 y)
{
    if(o->queue_count==o->queue_capacity){ScU32 next=o->queue_capacity?o->queue_capacity*2u:64u;
        if(next<o->queue_capacity || next>UINT_MAX/sizeof(GfxPoint))return SC_GFX_E_LIMIT;
        GfxPoint *p=realloc(o->queue,next*sizeof(*p));
        if(!p)return SC_GFX_E_MEMORY;
        o->queue=p;
        o->queue_capacity=next;
    }
    o->queue[o->queue_count].x=x;
    o->queue[o->queue_count++].y=y;
    return 0;
}
ScStatus ScGfxFillBegin(ScGfxSurface *s,ScI32 x,ScI32 y,ScU32 color,ScGfxOperation **out)
{
    if(!gfx_surface(s) || x<0 || y<0 || (ScU32)x>=s->width || (ScU32)y>=s->height)return SC_GFX_E_RANGE;
    ScGfxOperation *o=0;
    ScStatus status=gfx_edit_operation(s,GFX_FILL,"Flood fill",&o);
    if(status<0)return status;
    o->color=color;
    o->old_color=s->pixels[(ScU32)y*s->width+(ScU32)x];
    o->total=s->width*s->height;
    status=gfx_queue(o,x,y);
    if(status<0){gfx_operation_fail(o,status,status);
        ScGfxOperationClose(o);
        return status;
    }*out=o;
    return 0;
}
static ScStatus gfx_edit_finish(ScGfxOperation *o)
{
    ScStatus status=ScGfxEditCommit(o->edit);
    if(status<0)return gfx_operation_fail(o,status,status);
    o->edit=0;
    o->surface->busy=0;
    o->state=SC_GFX_OP_READY;
    return 0;
}
static ScStatus gfx_clear_step(ScGfxOperation *o,ScU32 budget)
{
    if(o->cancel)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    ScGfxSurface *s=o->surface;
    ScU32 end=o->total-o->progress;
    if(end>budget)end=budget;
    end+=o->progress;
    while(o->progress<end){ScU32 at=o->progress++;
        ScStatus status=ScGfxEditPixel(o->edit,o->rectangle.x+(ScI32)(at%(ScU32)o->rectangle.width),o->rectangle.y+(ScI32)(at/(ScU32)o->rectangle.width),o->color,SC_GFX_BLEND_COPY);
        if(status<0)return gfx_operation_fail(o,status,status);
    }
    gfx_epoch(s);
    return o->progress==o->total?gfx_edit_finish(o):SC_GFX_PENDING;
}
static ScStatus gfx_fill_step(ScGfxOperation *o,ScU32 budget)
{
    if(o->cancel)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    if(o->color==o->old_color)return gfx_edit_finish(o);
    ScGfxSurface *s=o->surface;
    ScU32 work=0;
    while(work<budget){
        if(!o->span_mode){if(o->queue_head==o->queue_count)return gfx_edit_finish(o);
            GfxPoint p=o->queue[o->queue_head++];
            if(p.x<0 || p.y<0 || (ScU32)p.x>=s->width || (ScU32)p.y>=s->height || s->pixels[(ScU32)p.y*s->width+(ScU32)p.x]!=o->old_color){work++;
                continue;
            }
            o->scan_y=p.y;
            o->span_left=o->span_right=p.x;
            while(o->span_left>0 && s->pixels[(ScU32)p.y*s->width+(ScU32)o->span_left-1u]==o->old_color)o->span_left--;
            while((ScU32)(o->span_right+1)<s->width && s->pixels[(ScU32)p.y*s->width+(ScU32)o->span_right+1u]==o->old_color)o->span_right++;
            o->span_at=o->span_left;
            o->span_mode=1;
        }
        while(o->span_at<=o->span_right && work<budget){ScI32 x=o->span_at++;
            ScStatus status=ScGfxEditPixel(o->edit,x,o->scan_y,o->color,SC_GFX_BLEND_COPY);
            if(status<0)return gfx_operation_fail(o,status,status);
            o->progress++;
            work++;
        }
        if(o->span_at<=o->span_right)break;
        for(ScI32 direction=-1;direction<=1;direction+=2){ScI32 y=o->scan_y+direction;
            if(y<0 || (ScU32)y>=s->height)continue;
            int inside=0;
            for(ScI32 x=o->span_left;x<=o->span_right;x++){int match=s->pixels[(ScU32)y*s->width+(ScU32)x]==o->old_color;
                if(match && !inside){ScStatus status=gfx_queue(o,x,y);
                    if(status<0)return gfx_operation_fail(o,status,status);
                }inside=match;
        }}
        o->span_mode=0;
        if(o->queue_head>4096 && o->queue_head>o->queue_count/2u){ScU32 remaining=o->queue_count-o->queue_head;
            memmove(o->queue,o->queue+o->queue_head,remaining*sizeof(*o->queue));
            o->queue_count=remaining;
            o->queue_head=0;
        }
    }
    gfx_epoch(s);
    return SC_GFX_PENDING;
}
ScStatus ScGfxTransformBegin(ScGfxSurface *s,ScU32 transform,ScGfxOperation **out)
{
    if(transform<1 || transform>4)return SC_GFX_E_INVALID;
    ScGfxOperation *o=0;
    ScStatus status=gfx_operation_new(GFX_TRANSFORM,s,&o);
    if(status<0)return status;
    if(s->bytes+sizeof(GfxHistory)>s->history_budget || s->next_revision==UINT_MAX){ScGfxSurfaceRelease(s);
        free(o);
        return SC_GFX_E_LIMIT;
    }
    ScGfxSurfaceDesc d;
    gfx_record(&d,sizeof(d));
    d.width=transform<=2?s->height:s->width;
    d.height=transform<=2?s->width:s->height;
    d.undo_budget_bytes=s->history_budget;
    status=gfx_surface_allocate(&d,&o->result);
    o->history=calloc(1,sizeof(*o->history));
    if(status<0 || !o->history){if(o->result)ScGfxSurfaceRelease(o->result);
        free(o->history);
        ScGfxSurfaceRelease(s);
        free(o);
        return status<0?status:SC_GFX_E_MEMORY;
    }
    o->transform=transform;
    o->total=s->width*s->height;
    s->readers++;
    o->flags|=1;
     *out=o;
    return 0;
}
static ScStatus gfx_transform_step(ScGfxOperation *o,ScU32 budget)
{
    if(o->cancel)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    ScGfxSurface *s=o->surface,*r=o->result;
    ScU32 end=o->total-o->progress;
    if(end>budget)end=budget;
    end+=o->progress;
    for(;o->progress<end;o->progress++){ScU32 x=o->progress%r->width,y=o->progress/r->width,sx=x,sy=y;
        if(o->transform==SC_GFX_ROTATE_CW){sx=y;
            sy=s->height-1u-x;
        }
        else if(o->transform==SC_GFX_ROTATE_CCW){sx=s->width-1u-y;
            sy=x;
        }
        else if(o->transform==SC_GFX_FLIP_X)sx=s->width-1u-x;
        else sy=s->height-1u-y;
        r->pixels[o->progress]=s->pixels[sy*s->width+sx];
    }
    if(o->progress<o->total)return SC_GFX_PENDING;
    GfxHistory *h=o->history;
    h->before_revision=s->revision;
    h->after_revision=s->next_revision++;
    h->image=s->pixels;
    h->image_width=s->width;
    h->image_height=s->height;
    h->image_bytes=s->bytes;
    h->bytes=s->bytes+sizeof(*h);
    snprintf(h->label,sizeof(h->label),o->transform<=2?"Rotate image":"Flip image");
    s->pixels=r->pixels;
    r->pixels=0;
    s->width=r->width;
    s->height=r->height;
    s->stride=r->stride;
    s->bytes=r->bytes;
    ScGfxSurfaceRelease(r);
    o->result=0;
    gfx_stack_clear(s,&s->redo,&s->redo_last,&s->redo_count);
    h->next=s->undo;
    if(s->undo)s->undo->previous=h;
    else s->undo_last=h;
    s->undo=h;
    s->undo_count++;
    s->history_bytes+=h->bytes;
    s->revision=h->after_revision;
    gfx_history_trim(s);
    gfx_epoch(s);
    o->history=0;
    o->state=SC_GFX_OP_READY;
    return 0;
}
ScStatus ScGfxOperationQuery(const ScGfxOperation *o,ScGfxOperationInfo *out)
{
    if(!o || o->magic!=GFX_OP_MAGIC || !gfx_output(out,sizeof(*out)))return SC_GFX_E_INVALID;
    ScGfxOperationInfo i;
    gfx_record(&i,sizeof(i));
    i.kind=o->kind;
    i.state=o->state;
    i.progress=o->progress;
    i.total=o->total;
    i.revision=o->revision;
    i.format=o->format;
    i.status=o->status;
    i.cause=o->cause;
    i.flags=(o->flags&4u?SC_GFX_OPERATION_COMMITTED:0)|(o->flags&2u?SC_GFX_OPERATION_ROLLBACK:0)|(o->wait_events?SC_GFX_OPERATION_WAITING:0);
    i.file_revision=o->file_revision;
    i.wait_events=o->wait_events;
    snprintf(i.path,sizeof(i.path),"%s",o->path);
    memcpy(out,&i,sizeof(i));
    return 0;
}
ScStatus ScGfxOperationSurface(ScGfxOperation *o,ScGfxSurface **out)
{if(!o || o->magic!=GFX_OP_MAGIC || !out)return SC_GFX_E_INVALID;
    if(o->state!=SC_GFX_OP_READY)return SC_GFX_E_BUSY;
    ScGfxSurface *s=o->result?o->result:o->surface;
    if(!s)return SC_GFX_E_NOT_SUPPORTED;
    ScStatus status=ScGfxSurfaceRetain(s);
    if(status==0)*out=s;
    return status;
}
ScStatus ScGfxOperationCancel(ScGfxOperation *o)
{if(!o || o->magic!=GFX_OP_MAGIC)return SC_GFX_E_STALE;
    if(o->state!=SC_GFX_OP_RUNNING)return 0;
    o->cancel=1;
    return SC_GFX_PENDING;
}
ScStatus ScGfxOperationStep(ScGfxOperation *o,ScU32 budget)
{
    if(!o || o->magic!=GFX_OP_MAGIC)return SC_GFX_E_STALE;
    if(o->state!=SC_GFX_OP_RUNNING)return o->state==SC_GFX_OP_READY?0:o->status;
    if(!budget)return SC_GFX_E_INVALID;
    if(budget>65536)budget=65536;
    if(o->flags&2u)return gfx_rollback_step(o,budget);
    if(o->kind==GFX_LOAD)return gfx_load_step(o,budget);
    if(o->kind==GFX_SAVE)return gfx_save_step(o,budget);
    if(o->kind==GFX_HISTORY)return gfx_history_step(o,budget);
    if(o->kind==GFX_FILL)return gfx_fill_step(o,budget);
    if(o->kind==GFX_CLEAR)return gfx_clear_step(o,budget);
    if(o->kind==GFX_TRANSFORM)return gfx_transform_step(o,budget);
    if(o->kind==GFX_COPY || o->kind==GFX_PASTE)return gfx_clipboard_step(o,budget);
    if(o->kind==GFX_PATH)return gfx_draw_step(o,budget);
    return SC_GFX_E_INVALID;
}
ScStatus ScGfxOperationClose(ScGfxOperation *o)
{
    if(!o || o->magic!=GFX_OP_MAGIC)return SC_GFX_E_STALE;
    if(o->state==SC_GFX_OP_RUNNING){o->cancel=1;
        return SC_GFX_PENDING;
    }
    ScStatus clean=gfx_lease_cleanup(o);
    if(clean==SC32_PENDING)return SC_GFX_PENDING;
    if(clean<0)return SC_GFX_E_IO;
    if(o->request){ScImageDecodeCancel(o->request);
        o->request=0;
    }if(o->file)ScFileClose(o->file);
    if(o->clipboard_candidate)ScClipboardAbort(o->clipboard_candidate);
    if(o->clipboard_snapshot)ScClipboardClose(o->clipboard_snapshot);
    if(o->surface){if(o->surface->busy==o)o->surface->busy=0;
        if(o->flags&1u)o->surface->readers--;
        ScGfxSurfaceRelease(o->surface);
    }
    if(o->result)ScGfxSurfaceRelease(o->result);
    if(o->kind==GFX_TRANSFORM)gfx_history_free(o->history);
    gfx_draw_destroy(o);
    free(o->queue);
    free(o->buffer);
    o->magic=0;
    free(o);
    return 0;
}
