#include "GFXPRIVATE.H"

static void gfx_history_free(GfxHistory *h)
{if(!h)return;
    while(h->tiles){GfxTile *t=h->tiles;
        h->tiles=t->next;
        free(t);
    }free(h->image);
    free(h);
}
static void gfx_stack_clear(ScGfxSurface *s,GfxHistory **head,GfxHistory **last,ScU32 *count)
{while(*head){GfxHistory *h=*head;
         *head=h->next;
        s->history_bytes-=h->bytes;
        gfx_history_free(h);
    }*last=0;
     *count=0;
}
static void gfx_history_trim(ScGfxSurface *s)
{while(s->history_bytes>s->history_budget && s->undo_last){GfxHistory *h=s->undo_last;
        s->undo_last=h->previous;
        if(s->undo_last)s->undo_last->next=0;
        else s->undo=0;
        s->history_bytes-=h->bytes;
        s->undo_count--;
        gfx_history_free(h);
}}
static ScStatus gfx_surface_allocate(const ScGfxSurfaceDesc *d,ScGfxSurface **out)
{
    if(!gfx_input(d,sizeof(*d)) || !out || d->flags || d->reserved)return SC_GFX_E_INVALID;
    if(!d->width || !d->height || d->width>SC_GFX_MAX_WIDTH || d->height>SC_GFX_MAX_HEIGHT || d->width>SC_GFX_MAX_PIXELS/d->height)return SC_GFX_E_RANGE;
    if(d->undo_budget_bytes && (d->undo_budget_bytes<4096u || d->undo_budget_bytes>32u*1024u*1024u))return SC_GFX_E_RANGE;
    ScGfxSurface *s=calloc(1,sizeof(*s));
    if(!s)return SC_GFX_E_MEMORY;
    s->bytes=d->width*d->height*4u;
    s->pixels=malloc(s->bytes);
    if(!s->pixels){free(s);
        return SC_GFX_E_MEMORY;
    }
    s->magic=GFX_SURFACE_MAGIC;
    s->references=1;
    s->width=d->width;
    s->height=d->height;
    s->stride=d->width*4u;
    s->revision=s->saved_revision=s->change_epoch=1;
    s->next_revision=2;
    s->history_budget=d->undo_budget_bytes?d->undo_budget_bytes:8u*1024u*1024u;
     *out=s;
    return 0;
}
ScStatus ScGfxSurfaceCreate(const ScGfxSurfaceDesc *d,ScGfxSurface **out)
{ScGfxSurface *s=0;
    ScStatus status=gfx_surface_allocate(d,&s);
    if(status<0)return status;
    for(ScU32 i=0;i<s->bytes/4;i++)s->pixels[i]=d->fill_argb;
     *out=s;
    return 0;
}
ScStatus ScGfxSurfaceRetain(ScGfxSurface *s)
{if(!gfx_surface(s))return SC_GFX_E_STALE;
    if(s->references==UINT_MAX)return SC_GFX_E_RANGE;
    s->references++;
    return 0;
}
void ScGfxSurfaceRelease(ScGfxSurface *s)
{if(!gfx_surface(s) || --s->references)return;
    gfx_stack_clear(s,&s->undo,&s->undo_last,&s->undo_count);
    gfx_stack_clear(s,&s->redo,&s->redo_last,&s->redo_count);
    s->magic=0;
    free(s->pixels);
    free(s);
}
ScStatus ScGfxSurfaceQuery(const ScGfxSurface *s,ScGfxSurfaceInfo *out)
{
    if(!gfx_surface(s))return SC_GFX_E_STALE;
    if(!gfx_output(out,sizeof(*out)))return SC_GFX_E_INVALID;
    ScGfxSurfaceInfo v;
    gfx_record(&v,sizeof(v));
    v.width=s->width;
    v.height=s->height;
    v.stride=s->stride;
    v.bytes=s->bytes;
    v.revision=s->revision;
    v.saved_revision=s->saved_revision;
    v.change_epoch=s->change_epoch;
    v.flags=(s->revision!=s->saved_revision || (s->edit && s->edit->changed)?SC_GFX_DIRTY:0)|(s->edit?SC_GFX_EDITING:0)|(s->busy || s->readers?SC_GFX_BUSY:0);
    v.undo_records=s->undo_count;
    v.redo_records=s->redo_count;
    v.history_bytes=s->history_bytes;
    v.history_budget=s->history_budget;
    memcpy(out,&v,sizeof(v));
    return 0;
}
ScStatus ScGfxSurfacePixels(const ScGfxSurface *s,ScGfxPixels *out)
{if(!gfx_surface(s))return SC_GFX_E_STALE;
    if(!gfx_output(out,sizeof(*out)))return SC_GFX_E_INVALID;
    ScGfxPixels p={sizeof(p),1,s->pixels,s->width,s->height,s->stride,s->bytes,s->change_epoch};
    memcpy(out,&p,sizeof(p));
    return 0;
}
ScStatus ScGfxSurfacePixel(const ScGfxSurface *s,ScI32 x,ScI32 y,ScU32 *out)
{if(!gfx_surface(s))return SC_GFX_E_STALE;
    if(!out || x<0 || y<0 || (ScU32)x>=s->width || (ScU32)y>=s->height)return SC_GFX_E_RANGE;
     *out=s->pixels[(ScU32)y*s->width+(ScU32)x];
    return 0;
}
ScStatus ScGfxSurfaceMarkSaved(ScGfxSurface *s,ScU32 revision)
{if(!gfx_surface(s))return SC_GFX_E_STALE;
    if(s->revision!=revision || s->edit)return SC_GFX_E_STALE;
    s->saved_revision=revision;
    return 0;
}
ScStatus ScGfxSurfaceHistoryBudget(ScGfxSurface *s,ScU32 bytes)
{if(!gfx_surface(s))return SC_GFX_E_STALE;
    if(s->edit || s->busy)return SC_GFX_E_BUSY;
    if(bytes<4096 || bytes>32u*1024u*1024u)return SC_GFX_E_RANGE;
    s->history_budget=bytes;
    gfx_history_trim(s);
    while(s->history_bytes>bytes && s->redo_last){GfxHistory *h=s->redo_last;
        s->redo_last=h->previous;
        if(s->redo_last)s->redo_last->next=0;
        else s->redo=0;
        s->history_bytes-=h->bytes;
        s->redo_count--;
        gfx_history_free(h);
    }return 0;
}
ScStatus ScGfxEditBegin(ScGfxSurface *s,const char *label,ScGfxEdit **out)
{
    if(!gfx_surface(s))return SC_GFX_E_STALE;
    if(!out || !label || strlen(label)>=48)return SC_GFX_E_INVALID;
    if(s->edit || s->busy || s->readers)return SC_GFX_E_BUSY;
    if(s->next_revision==UINT_MAX || s->change_epoch==UINT_MAX)return SC_GFX_E_LIMIT;
    ScGfxEdit *e=calloc(1,sizeof(*e));
    GfxHistory *h=calloc(1,sizeof(*h));
    if(!e || !h){free(e);
        free(h);
        return SC_GFX_E_MEMORY;
    }
    e->tile_columns=(s->width+GFX_TILE-1)/GFX_TILE;
    e->tile_rows=(s->height+GFX_TILE-1)/GFX_TILE;
    e->index=calloc(e->tile_columns*e->tile_rows,sizeof(*e->index));
    if(!e->index){free(h);
        free(e);
        return SC_GFX_E_MEMORY;
    }
    e->magic=GFX_EDIT_MAGIC;
    e->surface=s;
    e->record=h;
    h->before_revision=s->revision;
    h->bytes=sizeof(*h);
    snprintf(h->label,sizeof(h->label),"%s",label);
    ScStatus retained=ScGfxSurfaceRetain(s);
    if(retained<0){free(e->index);
        free(h);
        free(e);
        return retained;
    }s->edit=e;
     *out=e;
    return 0;
}
static ScStatus gfx_edit_touch(ScGfxEdit *e,ScU32 x,ScU32 y)
{
    ScGfxSurface *s=e->surface;
    ScU32 tx=x/GFX_TILE,ty=y/GFX_TILE,index=ty*e->tile_columns+tx;
    if(e->index[index])return 0;
    ScU32 left=tx*GFX_TILE,top=ty*GFX_TILE,w=s->width-left,h=s->height-top;
    if(w>GFX_TILE)w=GFX_TILE;
    if(h>GFX_TILE)h=GFX_TILE;
    ScU32 bytes=(ScU32)sizeof(GfxTile)+w*h*4u;
    if(e->record->bytes>s->history_budget || bytes>s->history_budget-e->record->bytes){e->failed=(ScU32)SC_GFX_E_LIMIT;
        return SC_GFX_E_LIMIT;
    }
    GfxTile *tile=malloc(bytes);
    if(!tile){e->failed=(ScU32)SC_GFX_E_MEMORY;
        return SC_GFX_E_MEMORY;
    }
    tile->x=left;
    tile->y=top;
    tile->width=w;
    tile->height=h;
    tile->bytes=bytes;
    for(ScU32 row=0;row<h;row++)memcpy(gfx_tile_pixels(tile)+row*w,s->pixels+(top+row)*s->width+left,w*4u);
    tile->next=e->record->tiles;
    e->record->tiles=tile;
    e->record->bytes+=bytes;
    e->index[index]=tile;
    return 0;
}
ScStatus ScGfxEditPixel(ScGfxEdit *e,ScI32 x,ScI32 y,ScU32 argb,ScU32 blend)
{
    if(!e || e->magic!=GFX_EDIT_MAGIC || !gfx_surface(e->surface) || e->surface->edit!=e)return SC_GFX_E_STALE;
    if(e->failed)return (ScStatus)e->failed;
    if(blend>SC_GFX_BLEND_ERASE)return SC_GFX_E_INVALID;
    ScGfxSurface *s=e->surface;
    if(x<0 || y<0 || (ScU32)x>=s->width || (ScU32)y>=s->height)return 0;
    ScU32 *pixel=s->pixels+(ScU32)y*s->width+(ScU32)x,value=blend==SC_GFX_BLEND_COPY?argb:blend==SC_GFX_BLEND_OVER?ScGfxArgbOver(argb,*pixel):ScGfxArgbErase(*pixel,argb>>24);
    if(value==*pixel)return 0;
    ScStatus status=gfx_edit_touch(e,(ScU32)x,(ScU32)y);
    if(status<0)return status;
     *pixel=value;
    e->changed=1;
    gfx_union(&e->dirty,x,y);
    return 0;
}
ScStatus ScGfxEditQuery(const ScGfxEdit *e,ScGfxEditInfo *out)
{if(!e || e->magic!=GFX_EDIT_MAGIC || !gfx_output(out,sizeof(*out)))return SC_GFX_E_INVALID;
    ScGfxEditInfo i;
    gfx_record(&i,sizeof(i));
    i.width=e->surface->width;
    i.height=e->surface->height;
    i.bytes=e->record->bytes;
    i.revision=e->surface->revision;
    i.change_epoch=e->surface->change_epoch;
    i.flags=(e->changed?SC_GFX_DIRTY:0)|(e->failed?SC_GFX_BUSY:0);
    i.dirty=e->dirty;
    memcpy(out,&i,sizeof(i));
    return 0;
}
static void gfx_edit_discard(ScGfxEdit *e,ScU32 restore)
{
    if(!e || e->magic!=GFX_EDIT_MAGIC)return;
    ScGfxSurface *s=e->surface;
    if(restore)for(GfxTile *t=e->record->tiles;t;t=t->next)for(ScU32 row=0;row<t->height;row++)memcpy(s->pixels+(t->y+row)*s->width+t->x,gfx_tile_pixels(t)+row*t->width,t->width*4u);
    if(e->changed)gfx_epoch(s);
    s->edit=0;
    gfx_history_free(e->record);
    free(e->index);
    e->magic=0;
    free(e);
    ScGfxSurfaceRelease(s);
}
void ScGfxEditAbort(ScGfxEdit *e){gfx_edit_discard(e,1);
}
ScStatus ScGfxEditCommit(ScGfxEdit *e)
{
    if(!e || e->magic!=GFX_EDIT_MAGIC)return SC_GFX_E_STALE;
    if(e->failed)return (ScStatus)e->failed;
    ScGfxSurface *s=e->surface;
    if(e->changed){GfxHistory *h=e->record;
        h->after_revision=s->next_revision++;
        s->revision=h->after_revision;
        gfx_stack_clear(s,&s->redo,&s->redo_last,&s->redo_count);
        h->next=s->undo;
        h->previous=0;
        if(s->undo)s->undo->previous=h;
        else s->undo_last=h;
        s->undo=h;
        s->undo_count++;
        s->history_bytes+=h->bytes;
        gfx_history_trim(s);
        gfx_epoch(s);
    }else gfx_history_free(e->record);
    s->edit=0;
    free(e->index);
    e->magic=0;
    free(e);
    ScGfxSurfaceRelease(s);
    return 0;
}
ScStatus ScGfxHistoryLabel(const ScGfxSurface *s,ScU32 direction,ScU32 index,char *out,ScU32 capacity)
{if(!gfx_surface(s) || !out || !capacity || (direction!=1 && direction!=2))return SC_GFX_E_INVALID;
    GfxHistory *h=direction==1?s->undo:s->redo;
    while(h && index--)h=h->next;
    if(!h)return SC_GFX_END;
    if(strlen(h->label)>=capacity)return SC_GFX_E_RANGE;
    snprintf(out,capacity,"%s",h->label);
    return 0;
}
