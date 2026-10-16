#include "GFXPRIVATE.H"
#include "SCGFXDRAW.H"
typedef struct {ScU32 kind;
    ScI32 p[6];
} GfxPathCommand;
struct ScGfxPath {ScU32 magic,references,readers,count,capacity,revision,open;
    GfxPathCommand *commands;
    ScGfxRect bounds;
};
typedef struct {ScI32 x0,y0,x1,y1;
} GfxEdge;
typedef struct {ScI32 p[8];
    ScU32 kind,depth;
} GfxCurve;
typedef struct {
    ScGfxPath *path;
    ScGfxPathDrawDesc desc;
    ScU32 command,edge_count,edge_capacity,stack_count,phase,edge,have_subpath;
    ScI32 current_x,current_y,start_x,start_y,row,column,winding,dx,dy,sx,sy,error,x,y,end_x,end_y,dab_x,dab_y;
    ScU32 line_active,dab_active;
    GfxEdge *edges;
    GfxCurve stack[17];
    ScI32 *crossings;
    ScU32 color;
} GfxDraw;
enum {GFX_PATH_MAGIC=0x50464731u};
static int gfx_path_valid(const ScGfxPath *p){return p && p->magic==GFX_PATH_MAGIC && p->references;
}
ScStatus ScGfxPathCreate(ScGfxPath **out)
{if(!out)return SC_GFX_E_INVALID;
    ScGfxPath *p=calloc(1,sizeof(*p));
    if(!p)return SC_GFX_E_MEMORY;
    p->magic=GFX_PATH_MAGIC;
    p->references=p->revision=1;
     *out=p;
    return 0;
}
ScStatus ScGfxPathRetain(ScGfxPath *p)
{if(!gfx_path_valid(p))return SC_GFX_E_STALE;
    if(p->references==UINT_MAX)return SC_GFX_E_LIMIT;
    p->references++;
    return 0;
}
void ScGfxPathRelease(ScGfxPath *p)
{if(!gfx_path_valid(p) || --p->references)return;
    p->magic=0;
    free(p->commands);
    free(p);
}
ScStatus ScGfxPathQuery(const ScGfxPath *p,ScGfxPathInfo *out)
{if(!gfx_path_valid(p) || !gfx_output(out,sizeof(*out)))return SC_GFX_E_INVALID;
    ScGfxPathInfo i={sizeof(i),1,p->count,p->revision,p->bounds};
    memcpy(out,&i,sizeof(i));
    return 0;
}
static ScStatus gfx_path_append(ScGfxPath *p,ScU32 kind,const ScI32 *values,ScU32 count)
{
    if(!gfx_path_valid(p))return SC_GFX_E_STALE;
    if(p->readers)return SC_GFX_E_BUSY;
    if(p->revision==UINT_MAX)return SC_GFX_E_LIMIT;
    if(kind!=1 && !p->open)return SC_GFX_E_INVALID;
    for(ScU32 i=0;i<count;i++)if(values[i]<-32768 || values[i]>32767)return SC_GFX_E_RANGE;
    if(p->count==p->capacity){ScU32 next=p->capacity?p->capacity*2u:32u;
        if(next<p->capacity || next>UINT_MAX/sizeof(GfxPathCommand))return SC_GFX_E_LIMIT;
        GfxPathCommand *c=realloc(p->commands,next*sizeof(*c));
        if(!c)return SC_GFX_E_MEMORY;
        p->commands=c;
        p->capacity=next;
    }
    GfxPathCommand *c=p->commands+p->count++;
    memset(c,0,sizeof(*c));
    c->kind=kind;
    for(ScU32 i=0;i<count;i++)c->p[i]=values[i];
    for(ScU32 i=0;i<count;i+=2)gfx_union(&p->bounds,values[i],values[i+1]);
    p->revision++;
    p->open=kind==5?0:1;
    return 0;
}
ScStatus ScGfxPathMoveTo(ScGfxPath *p,ScI32 x,ScI32 y){ScI32 v[]={x,y};
    return gfx_path_append(p,1,v,2);
}
ScStatus ScGfxPathLineTo(ScGfxPath *p,ScI32 x,ScI32 y){ScI32 v[]={x,y};
    return gfx_path_append(p,2,v,2);
}
ScStatus ScGfxPathQuadraticTo(ScGfxPath *p,ScI32 cx,ScI32 cy,ScI32 x,ScI32 y){ScI32 v[]={cx,cy,x,y};
    return gfx_path_append(p,3,v,4);
}
ScStatus ScGfxPathCubicTo(ScGfxPath *p,ScI32 cx1,ScI32 cy1,ScI32 cx2,ScI32 cy2,ScI32 x,ScI32 y){ScI32 v[]={cx1,cy1,cx2,cy2,x,y};
    return gfx_path_append(p,4,v,6);
}
ScStatus ScGfxPathClose(ScGfxPath *p){return gfx_path_append(p,5,0,0);
}
ScStatus ScGfxPathClear(ScGfxPath *p)
{if(!gfx_path_valid(p))return SC_GFX_E_STALE;
    if(p->readers)return SC_GFX_E_BUSY;
    if(p->revision==UINT_MAX)return SC_GFX_E_LIMIT;
    p->count=p->open=0;
    p->bounds=gfx_rect(0,0,0,0);
    p->revision++;
    return 0;
}
static ScStatus gfx_edge_add(GfxDraw *d,ScI32 x0,ScI32 y0,ScI32 x1,ScI32 y1)
{
    if(x0==x1 && y0==y1)return 0;
    if(d->edge_count==d->edge_capacity){ScU32 next=d->edge_capacity?d->edge_capacity*2u:64u;
        if(next<d->edge_capacity || next>UINT_MAX/sizeof(GfxEdge))return SC_GFX_E_LIMIT;
        GfxEdge *e=realloc(d->edges,next*sizeof(*e));
        if(!e)return SC_GFX_E_MEMORY;
        d->edges=e;
        d->edge_capacity=next;
    }
    GfxEdge *edge=d->edges+d->edge_count;
    edge->x0=x0;edge->y0=y0;edge->x1=x1;edge->y1=y1;
    d->edge_count++;
    return 0;
}
static ScI32 gfx_abs(ScI32 value){return value<0?-value:value;
}
static ScStatus gfx_curve_step(GfxDraw *d)
{
    GfxCurve c=d->stack[--d->stack_count];
    ScI32 *p=c.p;
    ScI32 flat=gfx_abs(p[0]-2*p[2]+p[4])+gfx_abs(p[1]-2*p[3]+p[5]);
    if(c.kind==4){ScI32 other=gfx_abs(p[2]-2*p[4]+p[6])+gfx_abs(p[3]-2*p[5]+p[7]);
        if(other>flat)flat=other;
    }
    if(flat<=128 || c.depth==16)return gfx_edge_add(d,p[0],p[1],p[c.kind==4?6:4],p[c.kind==4?7:5]);
    /* 自适应de Casteljau；先压右再压左，最多17层，保留路径次序。
     * 坐标范围使所有Q8中间和都落在有符号32位，不借浮点或libgcc。 */
    GfxCurve left,right;
    /* 首成员是数组；显式整对象清零保留旧语义，避免依赖聚合省略花括号。 */
    memset(&left,0,sizeof(left));memset(&right,0,sizeof(right));
    left.kind=right.kind=c.kind;
    left.depth=right.depth=c.depth+1;
    ScI32 a[6],b[4],m[2];
    for(ScU32 i=0;i<(c.kind==4?6u:4u);i++)a[i]=(p[i]+p[i+2])/2;
    if(c.kind==3){m[0]=(a[0]+a[2])/2;
        m[1]=(a[1]+a[3])/2;
        for(ScU32 j=0;j<2;j++){left.p[j]=p[j];
            left.p[2+j]=a[j];
            left.p[4+j]=m[j];
            right.p[j]=m[j];
            right.p[2+j]=a[2+j];
            right.p[4+j]=p[4+j];
    }}
    else{for(ScU32 i=0;i<4;i++)b[i]=(a[i]+a[i+2])/2;
        m[0]=(b[0]+b[2])/2;
        m[1]=(b[1]+b[3])/2;
        for(ScU32 j=0;j<2;j++){left.p[j]=p[j];
            left.p[2+j]=a[j];
            left.p[4+j]=b[j];
            left.p[6+j]=m[j];
            right.p[j]=m[j];
            right.p[2+j]=b[2+j];
            right.p[4+j]=a[4+j];
            right.p[6+j]=p[6+j];
    }}
    d->stack[d->stack_count++]=right;
    d->stack[d->stack_count++]=left;
    return 0;
}
static ScStatus gfx_path_command(GfxDraw *d)
{
    if(d->command==d->path->count){if(d->have_subpath && d->desc.mode!=SC_GFX_PATH_STROKE){d->have_subpath=0;
            return gfx_edge_add(d,d->current_x,d->current_y,d->start_x,d->start_y);
        }d->phase=1;
        return 0;
    }
    GfxPathCommand *c=d->path->commands+d->command++;
    if(c->kind==1){ScStatus s=0;
        if(d->have_subpath && d->desc.mode!=SC_GFX_PATH_STROKE)s=gfx_edge_add(d,d->current_x,d->current_y,d->start_x,d->start_y);
        d->start_x=d->current_x=c->p[0]*256;
        d->start_y=d->current_y=c->p[1]*256;
        d->have_subpath=1;
        return s;
    }
    if(c->kind==5){d->have_subpath=0;
        ScStatus s=gfx_edge_add(d,d->current_x,d->current_y,d->start_x,d->start_y);
        d->current_x=d->start_x;
        d->current_y=d->start_y;
        return s;
    }
    if(c->kind==2){ScStatus s=gfx_edge_add(d,d->current_x,d->current_y,c->p[0]*256,c->p[1]*256);
        d->current_x=c->p[0]*256;
        d->current_y=c->p[1]*256;
        return s;
    }
    GfxCurve curve;
    memset(&curve,0,sizeof(curve));
    curve.kind=c->kind;
    curve.p[0]=d->current_x;
    curve.p[1]=d->current_y;
    ScU32 count=c->kind==3?4:6;
    for(ScU32 i=0;i<count;i++)curve.p[i+2]=c->p[i]*256;
    d->current_x=curve.p[count];
    d->current_y=curve.p[count+1];
    d->stack[d->stack_count++]=curve;
    return 0;
}
ScStatus ScGfxDrawPathBegin(ScGfxSurface *s,ScGfxPath *path,const ScGfxPathDrawDesc *desc,ScGfxOperation **out)
{
    if(!gfx_surface(s) || !gfx_path_valid(path) || !gfx_input(desc,sizeof(*desc)) || !out || desc->mode<1 || desc->mode>3 || desc->flags || desc->reserved[0] || desc->reserved[1] || desc->reserved[2])return SC_GFX_E_INVALID;
    const ScGfxBrush *b=&desc->brush;
    if(!gfx_input(b,sizeof(*b)) || b->radius<1 || b->radius>64 || b->opacity>255 || b->blend>2 || b->flags || b->reserved)return SC_GFX_E_INVALID;
    ScGfxRect clip;
    if(!gfx_clip_rect(s,&desc->clip,&clip))return SC_GFX_E_RANGE;
    GfxDraw *d=calloc(1,sizeof(*d));
    if(!d)return SC_GFX_E_MEMORY;
    d->desc=*desc;
    d->desc.clip=clip;
    d->path=path;
    d->row=clip.y;
    d->column=clip.x;
    d->color=(b->argb&0xffffffu)|(((b->argb>>24)*b->opacity+127u)/255u<<24);
    ScStatus status=ScGfxPathRetain(path);
    if(status<0){free(d);
        return status;
    }
    d->crossings=calloc(s->width+1u,sizeof(ScI32));
    if(!d->crossings){ScGfxPathRelease(path);
        free(d);
        return SC_GFX_E_MEMORY;
    }
    ScGfxOperation *o=0;
    status=gfx_edit_operation(s,GFX_PATH,desc->mode==1?"Stroke path":"Fill path",&o);
    if(status<0){free(d->crossings);
        ScGfxPathRelease(path);
        free(d);
        return status;
    }path->readers++;
    o->draw=d;
    o->total=s->width*s->height;
     *out=o;
    return 0;
}
static ScI32 gfx_edge_intersection(const GfxEdge *e,ScI32 y)
{
    ScI32 low_y=e->y0,low_x=e->x0,high_y=e->y1,high_x=e->x1;
    if(low_y>high_y){low_y=e->y1;
        low_x=e->x1;
        high_y=e->y0;
        high_x=e->x0;
    }
    ScU32 denominator=(ScU32)(high_y-low_y),remainder=(ScU32)(y-low_y),fraction=0;
    for(ScU32 i=0;i<16;i++){remainder*=2u;
        fraction<<=1;
        if(remainder>=denominator){remainder-=denominator;
            fraction|=1u;
    }}
    ScI32 delta=high_x-low_x;
    ScU32 magnitude=(ScU32)gfx_abs(delta);
    ScU32 amount=(magnitude/65536u)*fraction+((magnitude%65536u)*fraction)/65536u;
    return low_x+(delta<0?-(ScI32)amount:(ScI32)amount);
}
static ScI32 gfx_q8_round(ScI32 x){return x>=0?(x+128)/256:-((-x+127)/256);
}
static ScStatus gfx_path_stroke_step(ScGfxOperation *o,GfxDraw *d,ScU32 *work,ScU32 budget)
{
    ScI32 radius=(ScI32)d->desc.brush.radius;
    ScGfxRect clip=d->desc.clip;
    while(*work<budget){if(!d->line_active){if(d->edge==d->edge_count)return gfx_edit_finish(o);
            GfxEdge e=d->edges[d->edge++];
            (*work)++;
            d->x=gfx_q8_round(e.x0);
            d->y=gfx_q8_round(e.y0);
            d->end_x=gfx_q8_round(e.x1);
            d->end_y=gfx_q8_round(e.y1);
            if((d->x<clip.x-radius && d->end_x<clip.x-radius) || (d->x>=clip.x+clip.width+radius && d->end_x>=clip.x+clip.width+radius)
            || (d->y<clip.y-radius && d->end_y<clip.y-radius) || (d->y>=clip.y+clip.height+radius && d->end_y>=clip.y+clip.height+radius))continue;
            d->dx=gfx_abs(d->end_x-d->x);
            d->dy=-gfx_abs(d->end_y-d->y);
            d->sx=d->x<d->end_x?1:-1;
            d->sy=d->y<d->end_y?1:-1;
            d->error=d->dx+d->dy;
            d->line_active=1;
        }
        if(!d->dab_active){d->dab_x=d->dab_y=-radius;
            d->dab_active=1;
        }
        while(d->dab_y<=radius && *work<budget){ScI32 dx=d->dab_x,dy=d->dab_y,x=d->x+dx,y=d->y+dy;
            (*work)++;
            ScI32 squared=dx*dx+dy*dy,outer=radius*radius,inner=(radius-1)*(radius-1);
            if(squared<outer && x>=clip.x && y>=clip.y && x<clip.x+clip.width && y<clip.y+clip.height){ScU32 coverage=squared<=inner?255u:(ScU32)(outer-squared)*255u/(ScU32)(outer-inner);
                ScU32 color=(d->color&0xffffffu)|(((d->color>>24)*coverage+127u)/255u<<24);
                ScStatus s=ScGfxEditPixel(o->edit,x,y,color,d->desc.brush.blend);
                if(s<0)return gfx_operation_fail(o,s,s);
            }
            if(++d->dab_x>radius){d->dab_x=-radius;
                d->dab_y++;
        }}
        if(d->dab_y<=radius)return SC_GFX_PENDING;
        d->dab_active=0;
        if(d->x==d->end_x && d->y==d->end_y){d->line_active=0;
            continue;
        }
        ScI32 twice=d->error*2;
        if(twice>=d->dy){d->error+=d->dy;
            d->x+=d->sx;
        }if(twice<=d->dx){d->error+=d->dx;
            d->y+=d->sy;
    }}
    return SC_GFX_PENDING;
}
static ScStatus gfx_draw_step(ScGfxOperation *o,ScU32 budget)
{
    if(o->cancel)return gfx_operation_fail(o,SC_GFX_E_CANCELLED,0);
    GfxDraw *d=o->draw;
    ScU32 work=0;
    while(!d->phase && work<budget){ScStatus s=d->stack_count?gfx_curve_step(d):gfx_path_command(d);
        if(s<0)return gfx_operation_fail(o,s,s);
        work++;
    }
    if(!d->phase)return SC_GFX_PENDING;
    if(!d->desc.clip.width || !d->desc.clip.height)return gfx_edit_finish(o);
    ScStatus result=SC_GFX_PENDING;
    if(d->desc.mode==SC_GFX_PATH_STROKE)result=gfx_path_stroke_step(o,d,&work,budget);
    else{ScGfxRect clip=d->desc.clip;
        while(work<budget){if(d->row>=clip.y+clip.height)return gfx_edit_finish(o);
            if(d->phase==1){if(d->edge<d->edge_count){GfxEdge *e=d->edges+d->edge++;
                    ScI32 y=d->row*256+128,low=e->y0<e->y1?e->y0:e->y1,high=e->y0>e->y1?e->y0:e->y1;
                    work++;
                    if(y>=low && y<high){ScI32 value=gfx_edge_intersection(e,y)-128;
                        ScI32 x=value>=0?(value+255)/256:-((-value)/256);
                        if(x<clip.x)x=clip.x;
                        if(x>clip.x+clip.width)x=clip.x+clip.width;
                        d->crossings[x]+=e->y1>e->y0?1:-1;
                    }continue;
                }d->phase=2;
                d->column=clip.x;
                d->winding=0;
            }
            if(d->column<clip.x+clip.width){ScI32 x=d->column++;
                d->winding+=d->crossings[x];
                d->crossings[x]=0;
                work++;
                o->progress++;
                if(d->desc.mode==SC_GFX_PATH_FILL_NONZERO?d->winding!=0:(d->winding&1)!=0){ScStatus s=ScGfxEditPixel(o->edit,x,d->row,d->color,d->desc.brush.blend);
                    if(s<0)return gfx_operation_fail(o,s,s);
            }}
            else{d->crossings[clip.x+clip.width]=0;
                d->row++;
                d->phase=1;
                d->edge=0;
        }}
    }
    if(o->edit && o->edit->changed)gfx_epoch(o->surface);
    return result;
}
static void gfx_draw_destroy(ScGfxOperation *o)
{GfxDraw *d=o->draw;
    if(!d)return;
    d->path->readers--;
    ScGfxPathRelease(d->path);
    free(d->edges);
    free(d->crossings);
    free(d);
    o->draw=0;
}
