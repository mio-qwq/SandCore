static int ui_rect_nonempty(const ScUiRect *r){return r->width>0 && r->height>0;
    }
static ScI32 ui_rect_right(const ScUiRect *r)
{return r->width<=0?r->x:r->x>INT_MAX-r->width?INT_MAX:r->x+r->width;
    }
static ScI32 ui_rect_bottom(const ScUiRect *r)
{return r->height<=0?r->y:r->y>INT_MAX-r->height?INT_MAX:r->y+r->height;
    }
static void ui_rect_intersection(const ScUiRect *a,const ScUiRect *b,ScUiRect *out)
{
    ScI32 right=ui_rect_right(a),bottom=ui_rect_bottom(a),br=ui_rect_right(b),bb=ui_rect_bottom(b);
    out->x=a->x>b->x?a->x:b->x;
    out->y=a->y>b->y?a->y:b->y;
    if(br<right)right=br;
    if(bb<bottom)bottom=bb;
    {ScU32 width=right>out->x?(ScU32)right-(ScU32)out->x:0,height=bottom>out->y?(ScU32)bottom-(ScU32)out->y:0;
        out->width=width>(ScU32)INT_MAX?INT_MAX:(ScI32)width;
        out->height=height>(ScU32)INT_MAX?INT_MAX:(ScI32)height;
        }
}
static int ui_rect_contains(const ScUiRect *r,ScI32 x,ScI32 y)
{return x>=r->x && y>=r->y && x<ui_rect_right(r) && y<ui_rect_bottom(r);
    }
static void ui_rect_union(const ScUiRect *a,const ScUiRect *b,ScUiRect *out)
{
    ScI32 right=ui_rect_right(a),bottom=ui_rect_bottom(a),br=ui_rect_right(b),bb=ui_rect_bottom(b);
    out->x=a->x<b->x?a->x:b->x;
    out->y=a->y<b->y?a->y:b->y;
    if(br>right)right=br;
    if(bb>bottom)bottom=bb;
    {ScU32 width=(ScU32)right-(ScU32)out->x,height=(ScU32)bottom-(ScU32)out->y;
        out->width=width>(ScU32)INT_MAX?INT_MAX:(ScI32)width;
        out->height=height>(ScU32)INT_MAX?INT_MAX:(ScI32)height;
        }
}
static ScStatus ui_damage(UiNode *root,const ScUiRect *rectangle)
{
    UiRoot *w=root->window;
    ScUiRect full,area;
    ScContainerInfo info;
    ScU32 i;
    ScStatus s;
    if(!w || (root->private_flags&UI_NODE_RETIRED))return SC_UI_E_STALE;
    full.x=0;
    full.y=0;
    full.width=(ScI32)w->information.client_width;
    full.height=(ScI32)w->information.client_height;
    ui_rect_intersection(rectangle,&full,&area);
    if(!ui_rect_nonempty(&area))return SC_UI_OK;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(w->damage,&info);
    w->paint_pending=1;
    if(w->window){s=ScWindowInvalidate(w->window,area.x,area.y,(ScU32)area.width,(ScU32)area.height);
        if(s<0 && s!=SC32_E_STALE)return ui_cause(root->owner,s,UI_OP_PAINT,root->id);
        }
    /* OS损伤先存事实，私有细分缓存OOM不让已提交的正文永远停在旧画面。 */
    s=ScArrayReserve(w->damage,info.length+1);
    if(s<0)return ui_cause(root->owner,s,UI_OP_PAINT,root->id);
    for(i=0;i<info.length;){ScUiRect old,joined;
        ScArrayGet(w->damage,i,&old);
        ui_rect_union(&old,&area,&joined);
        if((joined.width<=old.width+area.width+2 && joined.height<=old.height+area.height+2) || info.length>=64){
            area=joined;
            ScArrayErase(w->damage,i,1);
            info.length--;
            i=0;
        }
        else i++;
    }
    s=ScArrayInsert(w->damage,info.length,&area,1);
    if(s<0)return ui_cause(root->owner,s,UI_OP_PAINT,root->id);
    return SC_UI_OK;
}
static int ui_painter_rect(ScUiPainter *p,const ScUiRect *local,ScUiRect *out)
{
    ScUiRect physical=*local;
    if(!ui_rect_nonempty(local))return 0;
    if(!ui_signed_add(physical.x,p->origin_x,&physical.x) || !ui_signed_add(physical.y,p->origin_y,&physical.y)){p->error=SC_UI_E_RANGE;
        return 0;
        }
    ui_rect_intersection(&physical,&p->clip,out);
    return ui_rect_nonempty(out);
}
static void ui_fill(ScUiPainter *p,const ScUiRect *local,ScU32 role)
{
    ScUiRect r;
    ScI32 y;
    ScU32 color=ui_color(p->context,role);
    if(!ui_painter_rect(p,local,&r))return;
    for(y=r.y;y<r.y+r.height;y++){ScU32 *row=p->root->pixels+(ScU32)y*p->root->width+(ScU32)r.x;
        ScI32 x;
        for(x=0;x<r.width;x++)row[x]=color;
        }
}
static void ui_border(ScUiPainter *p,const ScUiRect *r,int pressed)
{
    ScUiRect edge=*r;
    ScI32 thickness=ui_scale(p->context,1),i;
    ScU32 light=SC32_COLOR_LINE,dark=SC32_COLOR_LINE;
    if(!ui_rect_nonempty(r))return;
    if(thickness<1)thickness=1;
    if(thickness>r->width/2+(r->width&1))thickness=r->width/2+(r->width&1);
    if(thickness>r->height/2+(r->height&1))thickness=r->height/2+(r->height&1);
    if(p->context->theme.style==SC32_STYLE_CLASSIC){light=pressed?SC32_COLOR_DARK_EDGE:SC32_COLOR_HIGHLIGHT;
        dark=pressed?SC32_COLOR_HIGHLIGHT:SC32_COLOR_DARK_EDGE;
        }
    for(i=0;i<thickness;i++){
        edge.x=r->x+i;
        edge.y=r->y+i;
        edge.width=r->width-2*i;
        edge.height=1;
        ui_fill(p,&edge,light);
        edge.width=1;
        edge.height=r->height-2*i;
        ui_fill(p,&edge,light);
        edge.x=ui_rect_right(r)-1-i;
        ui_fill(p,&edge,dark);
        edge.x=r->x+i;
        edge.y=ui_rect_bottom(r)-1-i;
        edge.width=r->width-2*i;
        edge.height=1;
        ui_fill(p,&edge,dark);
    }
}
ScStatus ScUiPainterClip(ScUiPainter *p,const ScUiRect *r)
{
    ScUiRect physical,intersection;
    ScContainerInfo info;
    ScStatus s;
    if(!p || !r)return SC_UI_E_INVALID;
    physical=*r;
    if(r->width<0 || r->height<0 || !ui_signed_add(physical.x,p->origin_x,&physical.x) || !ui_signed_add(physical.y,p->origin_y,&physical.y)){p->error=SC_UI_E_RANGE;
        return p->error;
        }
    ui_rect_intersection(&p->clip,&physical,&intersection);
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(p->clips,&info);
    s=ScArrayInsert(p->clips,info.length,&p->clip,1);
    if(s<0){p->error=ui_cause(p->context,s,UI_OP_PAINT,p->node->id);
        return p->error;
        }
    p->clip=intersection;
    return SC_UI_OK;
}
ScStatus ScUiPainterRestore(ScUiPainter *p)
{
    ScContainerInfo info;
    if(!p)return SC_UI_E_INVALID;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(p->clips,&info);
    if(!info.length)return SC_UI_E_INVALID;
    ScArrayGet(p->clips,info.length-1,&p->clip);
    ScArrayErase(p->clips,info.length-1,1);
    return SC_UI_OK;
}
ScStatus ScUiPainterFill(ScUiPainter *p,const ScUiRect *r,ScU32 role)
{if(!p || !r || role>=24)return SC_UI_E_INVALID;
    if(r->width<0 || r->height<0)return SC_UI_E_RANGE;
    ui_fill(p,r,role);
    return p->error;
    }
ScStatus ScUiPainterLine(ScUiPainter *p,ScI32 x1,ScI32 y1,ScI32 x2,ScI32 y2,ScU32 role)
{
    ScI32 dx,dy,sx,sy,error;
    ScUiRect pixel;
    if(!p || role>=24)return SC_UI_E_INVALID;
    if(x1<-32768 || x1>32768 || y1<-32768 || y1>32768 || x2<-32768 || x2>32768 || y2<-32768 || y2>32768)return SC_UI_E_RANGE;
    dx=x2>x1?x2-x1:x1-x2;
    dy=y2>y1?y2-y1:y1-y2;
    sx=x1<x2?1:-1;
    sy=y1<y2?1:-1;
    error=dx-dy;
    pixel.width=1;
    pixel.height=1;
    for(;;){ScI32 twice=error*2;
        pixel.x=x1;
        pixel.y=y1;
        ui_fill(p,&pixel,role);
        if(x1==x2 && y1==y2)break;
        if(twice>-dy){error-=dy;
            x1+=sx;
            }
        if(twice<dx){error+=dx;
            y1+=sy;
            }}
    return p->error;
}
static ScStatus ui_paint_text(ScUiPainter *p,const ScUiRect *r,const ScUiText *text,ScU32 role,ScU32 flags)
{
    ScUiTextMetrics m;
    ScU32 pos=0,line,baseline,dot_width=0;
    ScI32 pen=r->x,y=r->y;
    ScStatus s;
    int truncated=0;
    s=ui_text_measure(p->context,role,text,(flags&UI_TEXT_WRAP)?r->width:SC_UI_AUTO,flags,&m);
    if(s<0)return s;
    ui_font_line(p->context,role,&line,&baseline);
    if((flags&UI_TEXT_ELLIPSIS) && !(flags&UI_TEXT_WRAP) && r->width>=0 && m.width>(ScU32)r->width){const ScFontGlyph *dot;
        ScU32 lease;
        s=ui_glyph(p->context,role,'.',&dot,&lease);
        if(s<0)return s;
        dot_width=(ScU32)dot->advance*3;
        ui_glyph_release(p->context,lease);
        }
    if(!(flags&UI_TEXT_WRAP)){
        if((flags&UI_TEXT_CENTER) && m.width<(ScU32)r->width)pen+=((ScI32)r->width-(ScI32)m.width)/2;
        else if((flags&UI_TEXT_RIGHT) && m.width<(ScU32)r->width)pen+=r->width-(ScI32)m.width;
    }
    while(pos<text->bytes){ScU32 end,walk=pos,cluster=0;
        s=ui_grapheme_next(text->data,text->bytes,pos,&end);
        if(s<0)return s;
        while(walk<end){ScU32 scalar,lease;
            const ScFontGlyph *g;
            s=ScUtf8Decode(text->data,text->bytes,&walk,&scalar);
            if(s<0)return SC_UI_E_UTF8;
            if(scalar=='\r' || scalar=='\n' || scalar=='\t')continue;
            s=ui_glyph(p->context,role,scalar,&g,&lease);
            if(s<0)return s;
            cluster+=(ScU32)g->advance;
            ui_glyph_release(p->context,lease);
            }
        if(((const unsigned char *)text->data)[pos]=='\r' || ((const unsigned char *)text->data)[pos]=='\n'){pen=r->x;
            y+=(ScI32)line;
            pos=end;
            continue;
            }
        if((flags&UI_TEXT_WRAP) && pen>r->x && cluster>(ScU32)(r->x+r->width-pen)){pen=r->x;
            y+=(ScI32)line;
            }
        if(dot_width && (r->width<(ScI32)dot_width || pen-r->x+(ScI32)cluster>r->width-(ScI32)dot_width)){truncated=1;
            break;
            }
        walk=pos;
        while(walk<end){ScU32 scalar,lease,row,col;
            const ScFontGlyph *g;
            s=ScUtf8Decode(text->data,text->bytes,&walk,&scalar);
            if(s<0)return SC_UI_E_UTF8;
            if(scalar=='\t'){ScI32 cell=ui_scale(p->context,8)*4;
                pen=r->x+((pen-r->x)/cell+1)*cell;
                continue;
                }
            s=ui_glyph(p->context,role,scalar,&g,&lease);
            if(s<0)return s;
            for(row=0;row<g->height;row++){ScI32 py=p->origin_y+y+(ScI32)baseline-g->baseline+(ScI32)row;
                if(py<p->clip.y || py>=p->clip.y+p->clip.height)continue;
                for(col=0;col<g->width;col++)if(g->rows[row][col/32]&(1u<<(31-(col&31)))){
                    ScI32 px=p->origin_x+pen+g->left+(ScI32)col;
                    if(px>=p->clip.x && px<p->clip.x+p->clip.width)p->root->pixels[(ScU32)py*p->root->width+(ScU32)px]=ui_color(p->context,p->text_role);
                }
            }
            pen+=g->advance;
            ui_glyph_release(p->context,lease);
        }
        pos=end;
        if(y-r->y>=r->height)break;
    }
    if(truncated){ScUiText dots;
        ScUiRect rest=*r;
        dots.size=sizeof(dots);
        dots.version=1;
        dots.data="...";
        dots.bytes=3;
        rest.x=pen;
        rest.y=y;
        rest.width=r->width-(pen-r->x);
        if(rest.width>0){s=ui_paint_text(p,&rest,&dots,role,0);
            if(s<0)return s;
            }
        pen+=(ScI32)dot_width;
        }
    if((flags&UI_TEXT_UNDERLINE) && pen>r->x){ScUiRect edge;
        edge.x=r->x;
        edge.y=y+(ScI32)baseline+1;
        edge.width=pen-r->x;
        edge.height=1;
        ui_fill(p,&edge,p->text_role);
        }
    if((flags&UI_TEXT_STRIKE) && pen>r->x){ScUiRect edge;
        edge.x=r->x;
        edge.y=y+(ScI32)baseline/2;
        edge.width=pen-r->x;
        edge.height=1;
        ui_fill(p,&edge,p->text_role);
        }
    return p->error;
}
ScStatus ScUiPainterText(ScUiPainter *p,const ScUiRect *r,const ScUiText *text,ScU32 role,ScU32 flags)
{
    ScStatus s;
    if(!p || !r)return SC_UI_E_INVALID;
    if(r->width<=0 || r->height<=0){ScUiTextMetrics measured;
        return ui_text_measure(p->context,role,text,SC_UI_AUTO,flags,&measured);
        }
    s=ScUiPainterClip(p,r);
    if(s<0)return s;
    s=ui_paint_text(p,r,text,role,flags);
    ScUiPainterRestore(p);
    if(s<0)p->error=s;
    return s;
}
ScStatus ScUiPainterIcon(ScUiPainter *p,const ScUiRect *r,ScUiIcon *icon,ScU32 state)
{
    ScUiRect area;
    ScI32 x,y;
    if(!p || !r || !icon || icon->object.kind!=UI_ICON || icon->object.closing)return SC_UI_E_INVALID;
    if(!ui_painter_rect(p,r,&area))return p->error;
    for(y=area.y;y<area.y+area.height;y++)for(x=area.x;x<area.x+area.width;x++){
        ScU32 sx=(ScU32)(x-p->origin_x-r->x)*icon->width/(ScU32)r->width,sy=(ScU32)(y-p->origin_y-r->y)*icon->height/(ScU32)r->height;
        ScU32 color=icon->pixels[sy*icon->width+sx],alpha=color>>24,*target=p->root->pixels+(ScU32)y*p->root->width+(ScU32)x;
        if(!(state&SC_UI_ENABLED))color=(color&0xff000000u)|(ui_color(p->context,SC32_COLOR_MUTED)&0xffffffu);
        if(alpha==255)*target=color;
        else if(alpha){ScU32 shift,result=0xff000000u,old=*target;
            for(shift=0;shift<24;shift+=8)result|=(((((color>>shift)&255u)*alpha+((old>>shift)&255u)*(255-alpha)+127)/255)<<shift);
            *target=result;
            }
    }
    return SC_UI_OK;
}
#include "ui_shell_paint.inc"

static int ui_root_can_paint(const UiRoot *w)
{
    /* PREPARING只来自内核已授角色的两surface，仍保持visible=0；这里只
     * 给真实首帧绘制资格，输入/动画timer继续沿普通实际可见性判断。 */
    return w && (w->visible || (w->information.state&SC32_WINDOW_PREPARING));
}
static ScStatus ui_root_surface(UiNode *n)
{
    UiRoot *w=n->window;
    ScWindowInfo info;
    ScU32 pixels,bytes;
    ScStatus s;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScWindowQuery(w->window,&info);
    if(s<0)return ui_cause(n->owner,s,UI_OP_PAINT,n->id);
    w->information=info;
    w->visible=(info.state&SC32_WINDOW_VISIBLE)!=0;
    if(w->width==info.client_width && w->height==info.client_height && w->pixels){w->geometry=info.geometry_epoch;
        return SC_UI_OK;
        }
    if(!ui_multiply(info.client_width,info.client_height,&pixels) || !ui_multiply(pixels,4,&bytes))return SC_UI_E_RANGE;
    {ScU32 *next=(ScU32 *)calloc(1,bytes),row,width=w->width<info.client_width?w->width:info.client_width,height=w->height<info.client_height?w->height:info.client_height;
        if(!next){n->private_flags|=UI_ROOT_LAYOUT_DEFERRED;
            return SC_UI_E_MEMORY;
            }
        for(row=0;row<height;row++)memcpy(next+row*info.client_width,w->pixels+row*w->width,width*4);
        free(w->pixels);
        w->pixels=next;
        w->bytes=bytes;
        w->width=info.client_width;
        w->height=info.client_height;
        w->geometry=info.geometry_epoch;
    }
    n->private_flags|=UI_NODE_LAYOUT_DIRTY;
    n->private_flags&=~UI_ROOT_LAYOUT_DEFERRED;
    return SC_UI_OK;
}
static int ui_paint_observed(ScUiContext *c,UiNode *n)
{
    UiListener *listener;
    UiNode *parent;
    if(n->custom && n->custom->description.event)return 1;
    for(listener=c->listeners;listener;listener=listener->next)if(!listener->retired && (!listener->description.type || listener->description.type==SC_UI_EVENT_PAINT)){
        for(parent=n;parent;parent=parent->parent)if(parent->id==listener->node)return 1;
    }
    return 0;
}
static ScStatus ui_paint_root(UiNode *root)
{
    ScUiContext *c=root->owner;
    UiRoot *w=root->window;
    ScPaintInfo paint;
    ScSubmitDesc submit;
    ScSubmitRect submissions[64];
    ScUiRect rectangles[64],claimed,bounds;
    ScU32 count=0,i,revision;
    ScArray *clips=0;
    ScStatus s;
    ScWideCounter now;
    int began=0;
    if(!w || (root->private_flags&UI_NODE_RETIRED))return SC_UI_E_STALE;
    if(!w->paint_pending || !ui_root_can_paint(w))return SC_UI_PENDING;
    if(ui_now(&now)==0 && ui_wide_compare(&now,&w->retry)<0)return SC_UI_PENDING;
    ui_node_hold(root);
    c->walking++;
    s=ui_root_surface(root);
    if(s<0)goto failed;
    if(root->private_flags&UI_NODE_LAYOUT_DIRTY){s=ui_layout_root(root);
        if(s<0)goto failed;
        }
    s=ui_array_new(sizeof(ScUiRect),&clips);
    if(s<0)goto failed;
    memset(&paint,0,sizeof(paint));
    paint.size=sizeof(paint);
    paint.version=1;
    s=ScPaintBegin(w->window,&paint);
    if(s==SC32_E_AGAIN || s==SC32_E_BUSY){s=SC_UI_PENDING;
        goto finish;
        }
    if(s<0){s=ui_cause(c,s,UI_OP_PAINT,root->id);
        goto failed;
        }
    began=1;
    claimed.x=paint.x;
    claimed.y=paint.y;
    claimed.width=(ScI32)paint.width;
    claimed.height=(ScI32)paint.height;
    {ScContainerInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(w->damage,&info);
        for(i=0;i<info.length && count<64;i++){ScUiRect r;
            ScArrayGet(w->damage,i,&r);
            ui_rect_intersection(&r,&claimed,&rectangles[count]);
            if(ui_rect_nonempty(&rectangles[count]))count++;
            }}
    if(!count){rectangles[0]=claimed;
        count=1;
        }
    else{bounds=rectangles[0];
        for(i=1;i<count;i++){ScUiRect next;
            ui_rect_union(&bounds,&rectangles[i],&next);
            bounds=next;
            }
        if(bounds.x!=claimed.x || bounds.y!=claimed.y || bounds.width!=claimed.width || bounds.height!=claimed.height){rectangles[0]=claimed;
            count=1;
            }}
    /* 隐藏准备surface必须提交一次真正完整首帧，不能把几块局部损伤
     * 的包围框当完整覆盖。WM以同geometry实际Submit核对，然后撤资格。 */
    if(w->information.state&SC32_WINDOW_PREPARING){rectangles[0]=claimed;count=1;}
    revision=c->revision;
    for(i=0;i<count;i++){UiNode *n=root;
        while(n){ScUiPainter p;
            ScUiRect intersection;
            ui_rect_intersection(&n->clip,&rectangles[i],&intersection);
            if((n->effective&SC_UI_VISIBLE) && ui_rect_nonempty(&intersection) && !(n->private_flags&UI_NODE_RETIRED)){
                memset(&p,0,sizeof(p));
                p.context=c;
                p.node=n;
                p.root=w;
                p.clip=intersection;
                p.origin_x=n->rectangle.x;
                p.origin_y=n->rectangle.y;
                p.clips=clips;
                ui_node_hold(n);
                p.text_role=(n->effective&SC_UI_ENABLED)?SC32_COLOR_TEXT:SC32_COLOR_MUTED;
                ScArrayClear(clips);
                if(n==root){ScUiRect local;
                    local.x=0;
                    local.y=0;
                    local.width=n->rectangle.width;
                    local.height=n->rectangle.height;
                    ui_fill(&p,&local,SC32_COLOR_FACE);
                    }
                if(n->kind==UI_KIND_POPUP || n->kind==SC_UI_MENUBAR || n->kind==SC_UI_TOOLBAR)s=ui_menu_paint(n,&p);
                else if(n->kind==SC_UI_TABLE || n->kind==SC_UI_TREE || n->kind==SC_UI_LIST)s=ui_view_paint(n,&p);
                else if(n->custom && n->custom->description.paint){n->custom->depth++;
                    s=n->custom->description.paint(c,n->id,&p,n->custom->description.context);
                    n->custom->depth--;
                    }
                else s=ui_controls_paint(n,&p);
                if(s>=0 && s!=SC_UI_PENDING && !(n->private_flags&UI_NODE_RETIRED) && ui_paint_observed(c,n)){ScUiEvent event;
                    ui_event_record(&event,n,SC_UI_EVENT_PAINT);
                    event.data=&p;
                    event.bytes=sizeof(p);
                    event.flags=SC_UI_EVENT_PROGRAMMATIC;
                    s=ui_emit(c,n,&event);
                    }
                if(s>=0 && p.error<0)s=p.error;
                if(s==SC_UI_PENDING)s=SC_UI_E_BUSY;
                ui_node_leave(n);
                if(s<0 || c->revision!=revision || c->object.closing || (root->private_flags&UI_NODE_RETIRED) || (n->private_flags&UI_NODE_RETIRED)){if(s>=0)s=SC_UI_E_CANCELLED;
                    goto failed;
                    }
            }
            if(n->first_child && (n->effective&SC_UI_VISIBLE)){ScUiRect child_damage;
                ui_rect_intersection(&n->child_clip,&rectangles[i],&child_damage);
                if(ui_rect_nonempty(&child_damage)){n=n->first_child;
                    continue;
                    }}
            while(n!=root && !n->next)n=n->parent;
            if(n==root)break;
            n=n->next;
        }
        submissions[i].source_x=(ScU32)rectangles[i].x;
        submissions[i].source_y=(ScU32)rectangles[i].y;
        submissions[i].destination_x=(ScU32)rectangles[i].x;
        submissions[i].destination_y=(ScU32)rectangles[i].y;
        submissions[i].width=(ScU32)rectangles[i].width;
        submissions[i].height=(ScU32)rectangles[i].height;
    }
    memset(&submit,0,sizeof(submit));
    submit.size=sizeof(submit);
    submit.version=1;
    submit.window=w->window;
    submit.generation=paint.generation;
    submit.geometry_epoch=paint.geometry_epoch;
    submit.pixels=w->pixels;
    submit.bytes=w->bytes;
    submit.width=w->width;
    submit.height=w->height;
    submit.stride=w->width*4;
    submit.rectangles=submissions;
    submit.rectangle_count=count;
    s=ScWindowSubmit(&submit);
    if(s<0){s=ui_cause(c,s,UI_OP_PAINT,root->id);
        goto failed;
        }
    s=ScPaintEnd(&paint);
    if(s<0){s=ui_cause(c,s,UI_OP_PAINT,root->id);
        goto failed;
        }
    began=0;
    if(w->information.state&SC32_WINDOW_PREPARING){s=ui_root_surface(root);if(s<0)goto failed;}
    w->paint_pending=0;
    w->paint_failures=0;
    w->retry.lo=w->retry.hi=0;
    ScArrayClear(w->damage);
    ui_wide_add(&c->paint_count,1);
    for(i=0;i<count;i++)ui_wide_add(&c->submitted_bytes,submissions[i].width*submissions[i].height*4);
    s=SC_UI_OK;
    goto finish;
failed:
    if(began)ScPaintCancel(&paint);
    if(!(root->private_flags&UI_NODE_RETIRED) && w){ScUiRect full;
        w->paint_pending=1;
        w->paint_failures++;
        full.x=0;
        full.y=0;
        full.width=(ScI32)w->information.client_width;
        full.height=(ScI32)w->information.client_height;
        ScArrayClear(w->damage);
        ScArrayInsert(w->damage,0,&full,1);
        if(ui_now(&w->retry)==0)ui_wide_add(&w->retry,16u<<(w->paint_failures>4?4:w->paint_failures));
        }
finish:ScArrayRelease(clips);
    c->walking--;
    ui_node_leave(root);
    return s;
}
ScStatus ScUiInvalidate(ScUiContext *c,ScUiNode id,const ScUiRect *local)
{
    UiNode *n=ui_node(c,id),*r;
    ScUiRect rectangle;
    if(!n)return SC_UI_E_STALE;
    r=ui_root(c,id);
    if(!local){ui_node_damage(n);
        return SC_UI_OK;
        }
    rectangle=*local;
    if(local->width<0 || local->height<0 || !ui_signed_add(rectangle.x,n->rectangle.x,&rectangle.x) || !ui_signed_add(rectangle.y,n->rectangle.y,&rectangle.y))return SC_UI_E_RANGE;
    ui_rect_intersection(&rectangle,&n->clip,&rectangle);
    return ui_damage(r,&rectangle);
}
