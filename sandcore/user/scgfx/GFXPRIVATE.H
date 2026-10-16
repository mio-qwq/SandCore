#ifndef SANDCORE_GFXPRIVATE_H
#define SANDCORE_GFXPRIVATE_H
#include "SCGFX.H"
#include "SCUICANVAS.H"
#include "SC32LEASE.H"
#include "SC32REPLACE.H"
#include "SC32PATH.H"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

enum {GFX_SURFACE_MAGIC=0x53464731u,GFX_EDIT_MAGIC=0x45464731u,GFX_OP_MAGIC=0x4f464731u,
    GFX_TILE=32,GFX_LOAD=1,GFX_SAVE=2,GFX_HISTORY=3,GFX_FILL=4,GFX_CLEAR=5,GFX_TRANSFORM=6,GFX_COPY=7,GFX_PASTE=8,GFX_PATH=9};
typedef struct GfxTile GfxTile;
struct GfxTile {GfxTile *next;
    ScU32 x,y,width,height,bytes;
};
/* 原生i386头部仍是24字节。像素紧随完整头部而非data[1]，因此
 * 分配、历史预算和回滚都保留24+width*height*4，不能多占一个像素。 */
/* SCCC不内联普通static函数，回滚热循环不能每个像素多一次调用。
 * 参数只出现一次；它仍只是相同的头后地址，不复制或改变尾部布局。 */
#define gfx_tile_pixels(tile) ((ScU32 *)((tile)+1))
typedef struct GfxHistory GfxHistory;
struct GfxHistory {GfxHistory *next,*previous;
    GfxTile *tiles;
    ScU32 bytes,before_revision,after_revision;
    ScU32 *image,image_width,image_height,image_bytes;
    char label[48];
};
struct ScGfxSurface {
    ScU32 magic,references,width,height,stride,bytes,*pixels,revision,saved_revision,next_revision,change_epoch;
    ScU32 history_budget,history_bytes,undo_count,redo_count,readers;
    GfxHistory *undo,*undo_last,*redo,*redo_last;
    ScGfxEdit *edit;
    ScGfxOperation *busy;
};
struct ScGfxEdit {
    ScU32 magic,failed,changed,tile_columns,tile_rows,bytes;
    ScGfxSurface *surface;
    GfxTile **index;
    GfxHistory *record;
    ScGfxRect dirty;
};
typedef struct {ScI32 x,y;
} GfxPoint;
typedef struct {ScLeaseInfo info;
    ScU32 valid,file,closing;
} GfxLease;
struct ScGfxOperation {
    ScU32 magic,kind,state,cancel,phase,progress,total,revision,format,flags;
    ScStatus status,cause;
    ScGfxSurface *surface,*result;
    ScGfxEdit *edit;
    ScFile file;
    ScU32 file_revision,file_bytes,file_object,offset,header_bytes,source_width,source_height,stride,bpp,top_down;
    unsigned char header[64],*buffer;
    ScU32 buffer_bytes,buffer_at,buffer_used,palette[256];
    ScImageRequest request;
    char path[SC32_PATH_CAPACITY],candidate[SC32_PATH_CAPACITY];
    ScU32 expected_destination_revision,candidate_sequence,candidate_limit,final_mode,wait_events;
    GfxLease candidate_lease,target_lease;
    GfxHistory *history;
    GfxTile *tile;
    ScU32 tile_pixel,history_direction,history_swapped,history_rollback;
    ScI32 x,y,scan_x,scan_y;
    ScU32 color,old_color,transform;
    GfxPoint *queue;
    ScU32 queue_count,queue_capacity,queue_head,span_mode;
    ScI32 span_left,span_right,span_at;
    ScGfxRect rectangle;
    ScClipboardCandidate clipboard_candidate;
    ScClipboardSnapshot clipboard_snapshot;
    ScU32 clipboard_epoch,clipboard_total;
    void *draw;
};
static void gfx_record(void *p,ScU32 bytes){memset(p,0,bytes);
    ((ScU32 *)p)[0]=bytes;
    ((ScU32 *)p)[1]=1;
}
static int gfx_input(const void *p,ScU32 bytes){return p && ((const ScU32 *)p)[0]==bytes && ((const ScU32 *)p)[1]==1;
}
static int gfx_output(const void *p,ScU32 bytes){return p && ((const ScU32 *)p)[0]>=bytes && ((const ScU32 *)p)[1]==1;
}
static int gfx_surface(const ScGfxSurface *s){return s && s->magic==GFX_SURFACE_MAGIC && s->references;
}
static void gfx_epoch(ScGfxSurface *s){if(s->change_epoch!=UINT_MAX)s->change_epoch++;
}
static ScU32 gfx_u32(const unsigned char *p){return (ScU32)p[0]|((ScU32)p[1]<<8)|((ScU32)p[2]<<16)|((ScU32)p[3]<<24);
}
static ScU32 gfx_u16(const unsigned char *p){return (ScU32)p[0]|((ScU32)p[1]<<8);
}
static void gfx_put32(unsigned char *p,ScU32 v){for(ScU32 i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));
}
static void gfx_put16(unsigned char *p,ScU32 v){p[0]=(unsigned char)v;
    p[1]=(unsigned char)(v>>8);
}
static ScGfxRect gfx_rect(ScI32 x,ScI32 y,ScI32 w,ScI32 h){ScGfxRect r={x,y,w,h};
    return r;
}
static void gfx_union(ScGfxRect *r,ScI32 x,ScI32 y)
{if(!r->width || !r->height){*r=gfx_rect(x,y,1,1);
        return;
    }ScI32 right=r->x+r->width,bottom=r->y+r->height;
    if(x<r->x)r->x=x;
    if(y<r->y)r->y=y;
    if(x>=right)right=x+1;
    if(y>=bottom)bottom=y+1;
    r->width=right-r->x;
    r->height=bottom-r->y;
}
static int gfx_clip_rect(const ScGfxSurface *s,const ScGfxRect *in,ScGfxRect *out)
{if(!in || in->width<0 || in->height<0 || in->x>INT_MAX-in->width || in->y>INT_MAX-in->height)return 0;
    ScI32 left=in->x<0?0:in->x,top=in->y<0?0:in->y,right=in->x+in->width,bottom=in->y+in->height;
    if(right>(ScI32)s->width)right=(ScI32)s->width;
    if(bottom>(ScI32)s->height)bottom=(ScI32)s->height;
    if(left>(ScI32)s->width)left=(ScI32)s->width;
    if(top>(ScI32)s->height)top=(ScI32)s->height;
     *out=gfx_rect(left,top,right>left?right-left:0,bottom>top?bottom-top:0);
    return 1;
}
static void gfx_history_free(GfxHistory *);
static void gfx_edit_discard(ScGfxEdit *,ScU32 restore);
static ScStatus gfx_operation_new(ScU32 kind,ScGfxSurface *,ScGfxOperation **out);
static ScStatus gfx_operation_fail(ScGfxOperation *,ScStatus,ScStatus);
static ScStatus gfx_load_step(ScGfxOperation *,ScU32);
static ScStatus gfx_save_step(ScGfxOperation *,ScU32);
static ScStatus gfx_clipboard_step(ScGfxOperation *,ScU32);
static ScStatus gfx_lease_cleanup(ScGfxOperation *);
static ScStatus gfx_draw_step(ScGfxOperation *,ScU32);
static void gfx_draw_destroy(ScGfxOperation *);
#endif
