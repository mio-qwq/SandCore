#include "GFXPRIVATE.H"

struct ScGfxCanvas {
    ScU32 magic,references,depth,closing,destroying,node,kind,timer,timer_flags,cleanup_timer,cleanup_listener;
    ScUiContext *ui;
    ScGfxSurface *surface;
    ScGfxCanvasProc procedure;
    void *user;
    ScDropProc user_drop;
    ScU32 flags,mode,percent,tool,cache_budget,quantum;
    char class_name[48];
    ScGfxBrush brush;
    ScGfxRect selection;
    ScI32 width,height,display_width,display_height,origin_x,origin_y,pan_x,pan_y;
    ScU32 *cache,*pending,*scratch,cache_bytes,pending_bytes,cache_epoch,render_epoch,render_at,render_full,cache_failed;
    ScGfxRect render_rect;
    ScU32 rendered_pixels;
    ScI32 down_x,down_y,last_mouse_x,last_mouse_y;
    ScU32 down,drag,preview;
    ScGfxEdit *edit;
    ScGfxOperation *operation;
    GfxPoint *points;
    ScU32 point_count,point_capacity,point_head;
    ScI32 line_x,line_y,line_end_x,line_end_y,line_dx,line_dy,line_sx,line_sy,line_error,dab_x,dab_y;
    ScU32 line_active,dab_active,have_last;
    ScI32 last_x,last_y;
};
enum {GFX_CANVAS_MAGIC=0x56464731u};
static ScU32 gfx_canvas_class_serial;
static void gfx_canvas_schedule(ScGfxCanvas *);
static void gfx_canvas_destroy(ScGfxCanvas *);
static void gfx_canvas_leave(ScGfxCanvas *v){if(v->depth)v->depth--;
    if(!v->depth && v->closing)gfx_canvas_destroy(v);
}
static int gfx_canvas_valid(const ScGfxCanvas *v){return v && v->magic==GFX_CANVAS_MAGIC && !v->closing && v->references;
}
static ScStatus gfx_canvas_notify(ScGfxCanvas *v,ScU32 type,const ScGfxRect *dirty,ScStatus status,ScI32 x,ScI32 y)
{
    if(!v->procedure || v->closing)return 0;
    ScGfxCanvasEvent e;
    gfx_record(&e,sizeof(e));
    e.type=type;
    e.node=v->node;
    e.status=status;
    e.x=x;
    e.y=y;
    e.argb=v->brush.argb;
    if(v->surface){e.revision=v->surface->revision;
        e.change_epoch=v->surface->change_epoch;
    }if(dirty)e.dirty=*dirty;
    v->depth++;
    ScStatus result=v->procedure(v,&e,v->user);
    v->depth--;
    return result;
}
static int gfx_canvas_visible(ScGfxCanvas *v)
{ScWindowInfo w;
    gfx_record(&w,sizeof(w));
    return v->node && ScUiWindowNative(v->ui,v->node,&w)==0 && (w.state&SC32_WINDOW_VISIBLE) && !(w.state&SC32_WINDOW_MINIMIZED);
}
static ScI32 gfx_clamp(ScI32 value,ScI32 low,ScI32 high){if(high<low)high=low;
    return value<low?low:value>high?high:value;
}
static void gfx_canvas_mapping(ScGfxCanvas *v)
{
    if(!v->surface || v->width<=0 || v->height<=0){v->display_width=v->display_height=0;
        return;
    }
    ScI32 w=(ScI32)v->surface->width,h=(ScI32)v->surface->height;
    if(v->mode==SC_GFX_VIEW_FIT){ScI32 dw=v->width,dh=h*dw/w;
        if(dh>v->height){dh=v->height;
            dw=w*dh/h;
        }
        if(dw<1)dw=1;
        if(dh<1)dh=1;
        v->display_width=dw;
        v->display_height=dh;
        v->origin_x=(v->width-dw)/2;
        v->origin_y=(v->height-dh)/2;
    }else{v->display_width=w*(ScI32)v->percent/100;
        v->display_height=h*(ScI32)v->percent/100;
        if(v->display_width<1)v->display_width=1;
        if(v->display_height<1)v->display_height=1;
        v->pan_x=gfx_clamp(v->pan_x,0,v->display_width-v->width);
        v->pan_y=gfx_clamp(v->pan_y,0,v->display_height-v->height);
        v->origin_x=v->display_width<v->width?(v->width-v->display_width)/2:-v->pan_x;
        v->origin_y=v->display_height<v->height?(v->height-v->display_height)/2:-v->pan_y;
    }
}
static ScI32 gfx_canvas_coordinate(ScI32 position,ScI32 origin,ScI32 display,ScU32 source)
{
    /* 拆商/余数后乘：display<=15360，source<=1920，余数*source*128
     * <2^32。避免800%时(2*x+1)*source*128回绕，也不引入64位除法。 */
    ScI32 at=position-origin;
    if(at<0)return -256;
    if(at>=display)return (ScI32)source*256;
    ScU32 a=(ScU32)at*2u+1u,b=source*128u,d=(ScU32)display;
    return (ScI32)((a/d)*b+((a%d)*b)/d)-128;
}
static int gfx_canvas_point(ScGfxCanvas *v,ScI32 px,ScI32 py,ScI32 *x,ScI32 *y,int clamp)
{
    if(!v->surface || !v->display_width || !v->display_height)return 0;
    ScI32 dx=px-v->origin_x,dy=py-v->origin_y;
    if(!clamp && (dx<0 || dy<0 || dx>=v->display_width || dy>=v->display_height))return 0;
    dx=gfx_clamp(dx,0,v->display_width-1);
    dy=gfx_clamp(dy,0,v->display_height-1);
     *x=dx*(ScI32)v->surface->width/v->display_width;
     *y=dy*(ScI32)v->surface->height/v->display_height;
    return 1;
}
static ScUiRect gfx_canvas_damage(ScGfxCanvas *v,const ScGfxRect *r)
{
    ScUiRect d={0,0,v->width,v->height};
    if(!r || !v->surface)return d;
    ScI32 left=r->x*v->display_width/(ScI32)v->surface->width+v->origin_x-2;
    ScI32 top=r->y*v->display_height/(ScI32)v->surface->height+v->origin_y-2;
    ScI32 right=(r->x+r->width)*v->display_width/(ScI32)v->surface->width+v->origin_x+3;
    ScI32 bottom=(r->y+r->height)*v->display_height/(ScI32)v->surface->height+v->origin_y+3;
    left=gfx_clamp(left,0,v->width);
    top=gfx_clamp(top,0,v->height);
    right=gfx_clamp(right,left,v->width);
    bottom=gfx_clamp(bottom,top,v->height);
    d.x=left;
    d.y=top;
    d.width=right-left;
    d.height=bottom-top;
    return d;
}
static void gfx_canvas_render_start(ScGfxCanvas *v,const ScGfxRect *dirty)
{
    gfx_canvas_mapping(v);
    if(!v->node || v->width<=0 || v->height<=0)return;
    ScUiRect damage=gfx_canvas_damage(v,dirty);
    if(!damage.width || !damage.height)return;
    if(!dirty || !v->cache || v->pending){
        v->render_rect=gfx_rect(0,0,v->width,v->height);
        v->render_full=1;
        ScU32 bytes=(ScU32)v->width*(ScU32)v->height*4u;
        if(bytes>v->cache_budget){v->cache_failed=1;
            free(v->pending);
            v->pending=0;
            v->pending_bytes=0;
        }
        else if(!v->pending && !v->cache_failed){
            if(bytes+v->cache_bytes>v->cache_budget){free(v->cache);
                v->cache=0;
                v->cache_bytes=0;
            }
            v->pending=malloc(bytes);
            v->pending_bytes=v->pending?bytes:0;
            if(!v->pending)v->cache_failed=1;
        }
    }else{
        ScGfxRect d=gfx_rect(damage.x,damage.y,damage.width,damage.height);
        if(v->render_rect.width && !v->render_full){ScI32 right=v->render_rect.x+v->render_rect.width,bottom=v->render_rect.y+v->render_rect.height;
            if(d.x<v->render_rect.x)v->render_rect.x=d.x;
            if(d.y<v->render_rect.y)v->render_rect.y=d.y;
            if(d.x+d.width>right)right=d.x+d.width;
            if(d.y+d.height>bottom)bottom=d.y+d.height;
            v->render_rect.width=right-v->render_rect.x;
            v->render_rect.height=bottom-v->render_rect.y;
        }else v->render_rect=d;
    }
    v->render_at=0;
    v->render_epoch=v->surface?v->surface->change_epoch:0;
    ScUiInvalidate(v->ui,v->node,&damage);
    gfx_canvas_schedule(v);
}
ScStatus ScGfxCanvasInvalidate(ScGfxCanvas *v,const ScGfxRect *dirty)
{if(!gfx_canvas_valid(v))return SC_GFX_E_STALE;
    v->depth++;
    gfx_canvas_render_start(v,dirty);
    gfx_canvas_leave(v);
    return 0;
}
static void gfx_canvas_render(ScGfxCanvas *v,ScU32 budget)
{
    if(!v->surface || !v->render_rect.width || !gfx_canvas_visible(v))return;
    if(v->render_epoch!=v->surface->change_epoch){gfx_canvas_render_start(v,v->surface->edit?&v->surface->edit->dirty:0);
    }
    ScU32 *out=v->render_full?v->pending:v->cache;
    if(!out){v->render_rect.width=v->render_rect.height=0;
        return;
    }
    ScGfxPixels source;
    gfx_record(&source,sizeof(source));
    ScGfxSurfacePixels(v->surface,&source);
    ScU32 total=(ScU32)v->render_rect.width*(ScU32)v->render_rect.height,start=v->render_at,end=total-v->render_at;
    if(end>budget)end=budget;
    end+=v->render_at;
    for(;v->render_at<end;v->render_at++){ScI32 x=v->render_rect.x+(ScI32)(v->render_at%(ScU32)v->render_rect.width),y=v->render_rect.y+(ScI32)(v->render_at/(ScU32)v->render_rect.width);
        ScU32 color=0;
        if(x>=v->origin_x && x-v->origin_x<v->display_width && y>=v->origin_y && y-v->origin_y<v->display_height){
            ScI32 fx=gfx_canvas_coordinate(x,v->origin_x,v->display_width,source.width),fy=gfx_canvas_coordinate(y,v->origin_y,v->display_height,source.height);
            ScGfxSample(&source,fx,fy,SC_GFX_FILTER_BILINEAR,&color);
        }
        out[(ScU32)y*(ScU32)v->width+(ScU32)x]=color;
    }
    v->rendered_pixels+=end-start;
    if(v->render_at<total)return;
    if(v->render_full){free(v->cache);
        v->cache=v->pending;
        v->cache_bytes=v->pending_bytes;
        v->pending=0;
        v->pending_bytes=0;
    }
    v->render_full=0;
    v->cache_epoch=v->render_epoch;
    ScUiRect r={v->render_rect.x,v->render_rect.y,v->render_rect.width,v->render_rect.height};
    v->render_rect.width=v->render_rect.height=0;
    ScUiInvalidate(v->ui,v->node,&r);
}
static ScStatus gfx_canvas_paint(ScUiContext *ui,ScUiNode node,ScUiPainter *p,void *context)
{
    (void)ui;
    (void)node;
    ScGfxCanvas *v=context;
    if(!v || v->closing)return 0;
    v->depth++;
    ScUiRect clip;
    ScStatus status=ScUiPainterBounds(p,&clip);
    if(status<0){gfx_canvas_leave(v);
        return status;
    }
    ScUiPainterFill(p,&clip,SC32_COLOR_FACE_ALT);
    ScI32 left=v->origin_x>clip.x?v->origin_x:clip.x,top=v->origin_y>clip.y?v->origin_y:clip.y;
    ScI32 right=v->origin_x+v->display_width,bottom=v->origin_y+v->display_height;
    if(right>clip.x+clip.width)right=clip.x+clip.width;
    if(bottom>clip.y+clip.height)bottom=clip.y+clip.height;
    if(v->surface && right>left && bottom>top){
        ScI32 start_x=left/16*16,start_y=top/16*16;
        for(ScI32 y=start_y;y<bottom;y+=16)for(ScI32 x=start_x;x<right;x+=16){ScUiRect r={x,y,16,16};
            if(r.x<left){r.width-=left-r.x;
                r.x=left;
            }if(r.y<top){r.height-=top-r.y;
                r.y=top;
            }
            if(r.x+r.width>right)r.width=right-r.x;
            if(r.y+r.height>bottom)r.height=bottom-r.y;
            ScUiPainterFill(p,&r,((x/16+y/16)&1)?SC32_COLOR_FACE_ALT:SC32_COLOR_PAPER);
        }
        if(v->cache && !v->render_full && v->cache_bytes==(ScU32)v->width*(ScU32)v->height*4u){
            ScUiArgbBlit b;
            gfx_record(&b,sizeof(b));
            b.pixels=v->cache;
            b.width=(ScU32)v->width;
            b.height=(ScU32)v->height;
            b.stride=(ScU32)v->width*4u;
            b.bytes=v->cache_bytes;
            b.source.x=0;b.source.y=0;
            b.source.width=v->width;b.source.height=v->height;
            b.destination=b.source;
            status=ScUiPainterArgb(p,&b);
        }else if(v->scratch){
            /* 缓存OOM或正在重建时只采当前损伤的最近邻原图。完整源
             * 从不降采样回写；不为每次hover重试分配同一失败大缓存。 */
            ScI32 stripe_rows=16384/v->width;
            if(stripe_rows<1)stripe_rows=1;
            if(stripe_rows>8)stripe_rows=8;
            for(ScI32 y=top;y<bottom;y+=stripe_rows){ScI32 rows=bottom-y;
                if(rows>stripe_rows)rows=stripe_rows;
                for(ScI32 yy=0;yy<rows;yy++)for(ScI32 x=left;x<right;x++){ScU32 sx=(ScU32)(x-v->origin_x)*v->surface->width/(ScU32)v->display_width;
                    ScU32 sy=(ScU32)(y+yy-v->origin_y)*v->surface->height/(ScU32)v->display_height;
                    v->scratch[(ScU32)yy*(ScU32)v->width+(ScU32)(x-left)]=v->surface->pixels[sy*v->surface->width+sx];
                }
                ScUiArgbBlit b;
                gfx_record(&b,sizeof(b));
                b.pixels=v->scratch;
                b.width=(ScU32)(right-left);
                b.height=(ScU32)rows;
                b.stride=(ScU32)v->width*4u;
                b.bytes=65536;
                b.source.x=0;b.source.y=0;
                b.source.width=right-left;b.source.height=rows;
                b.destination.x=left;b.destination.y=y;
                b.destination.width=right-left;b.destination.height=rows;
                status=ScUiPainterArgb(p,&b);
                if(status<0)break;
            }
        }
        ScGfxRect r=v->selection;
        if(v->preview && (v->tool==SC_GFX_TOOL_LINE || v->tool==SC_GFX_TOOL_RECTANGLE || v->tool==SC_GFX_TOOL_SELECT)){
            ScI32 x,y;
            if(gfx_canvas_point(v,v->last_mouse_x,v->last_mouse_y,&x,&y,1)){r.x=x<v->down_x?x:v->down_x;
                r.y=y<v->down_y?y:v->down_y;
                r.width=(x>v->down_x?x-v->down_x:v->down_x-x)+1;
                r.height=(y>v->down_y?y-v->down_y:v->down_y-y)+1;
        }}
        if(v->preview && v->tool==SC_GFX_TOOL_LINE){ScI32 x,y;
            if(gfx_canvas_point(v,v->last_mouse_x,v->last_mouse_y,&x,&y,1)){
                ScUiPainterLine(p,v->down_x*v->display_width/(ScI32)v->surface->width+v->origin_x,v->down_y*v->display_height/(ScI32)v->surface->height+v->origin_y,
                x*v->display_width/(ScI32)v->surface->width+v->origin_x,y*v->display_height/(ScI32)v->surface->height+v->origin_y,SC32_COLOR_ACCENT);
            }r.width=0;
        }
        if(r.width && r.height){ScI32 x=r.x*v->display_width/(ScI32)v->surface->width+v->origin_x,y=r.y*v->display_height/(ScI32)v->surface->height+v->origin_y;
            ScI32 w=r.width*v->display_width/(ScI32)v->surface->width,h=r.height*v->display_height/(ScI32)v->surface->height;
            if(w<1)w=1;
            if(h<1)h=1;
            ScUiPainterLine(p,x,y,x+w-1,y,SC32_COLOR_ACCENT);
            ScUiPainterLine(p,x,y+h-1,x+w-1,y+h-1,SC32_COLOR_ACCENT);
            ScUiPainterLine(p,x,y,x,y+h-1,SC32_COLOR_ACCENT);
            ScUiPainterLine(p,x+w-1,y,x+w-1,y+h-1,SC32_COLOR_ACCENT);
        }
    }
    if(!v->surface){ScUiText text={sizeof(text),1,"Open an image",13};
        ScUiPainterText(p,&clip,&text,SC_UI_FONT_BODY,SC_UI_TEXT_CENTER);
    }
    gfx_canvas_leave(v);
    return status;
}
static ScStatus gfx_canvas_measure(ScUiContext *ui,ScUiNode node,const ScUiSize *available,ScUiSize *out,void *context)
{(void)ui;
    (void)node;
    (void)context;
    out->width=available->width<160?160:available->width;
    out->height=available->height<100?100:available->height;
    return 0;
}
static ScStatus gfx_canvas_arrange(ScUiContext *ui,ScUiNode node,const ScUiRect *rect,void *context)
{
    (void)ui;
    (void)node;
    ScGfxCanvas *v=context;
    if(!v || v->closing)return 0;
    if(rect->width<0 || rect->height<0 || rect->width>16384 || rect->height>16384)return SC_GFX_E_RANGE;
    if(v->width!=rect->width || v->height!=rect->height){v->width=rect->width;
        v->height=rect->height;
        free(v->cache);
        free(v->pending);
        v->cache=v->pending=0;
        v->cache_bytes=v->pending_bytes=0;
        v->cache_failed=0;
        gfx_canvas_render_start(v,0);
    }
    return 0;
}

#include "canvas_input.inc"
