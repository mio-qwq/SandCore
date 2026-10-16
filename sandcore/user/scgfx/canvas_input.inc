static ScStatus gfx_canvas_enqueue(ScGfxCanvas *v,ScI32 x,ScI32 y)
{
    if(v->point_count && v->points[v->point_count-1].x==x && v->points[v->point_count-1].y==y)return 0;
    if(v->point_count==v->point_capacity){ScU32 next=v->point_capacity?v->point_capacity*2u:32u;
        if(next<v->point_capacity || next>UINT_MAX/sizeof(GfxPoint))return SC_GFX_E_LIMIT;
        GfxPoint *p=realloc(v->points,next*sizeof(*p));
        if(!p)return SC_GFX_E_MEMORY;
        v->points=p;
        v->point_capacity=next;
    }
    v->points[v->point_count].x=x;
    v->points[v->point_count++].y=y;
    return 0;
}
static void gfx_canvas_line_advance(ScGfxCanvas *v)
{ScI32 twice=v->line_error*2;
    if(twice>=v->line_dy){v->line_error+=v->line_dy;
        v->line_x+=v->line_sx;
    }if(twice<=v->line_dx){v->line_error+=v->line_dx;
        v->line_y+=v->line_sy;
}}
static void gfx_canvas_abort_edit(ScGfxCanvas *v,ScStatus status)
{
    v->down=v->preview=v->drag=0;
    v->point_count=v->point_head=v->line_active=v->dab_active=0;
    if(v->node)ScUiCapture(v->ui,v->node,0);
    if(v->edit){ScGfxOperation *o=calloc(1,sizeof(*o));
        if(o){o->magic=GFX_OP_MAGIC;
            o->kind=GFX_CLEAR;
            o->state=SC_GFX_OP_RUNNING;
            o->surface=v->surface;
            ScGfxSurfaceRetain(o->surface);
            o->edit=v->edit;
            v->edit=0;
            o->surface->busy=o;
            v->operation=o;
            gfx_operation_fail(o,status,status);
        }
        else{ScGfxEditAbort(v->edit);
            v->edit=0;
            gfx_canvas_render_start(v,0);
    }}
    gfx_canvas_notify(v,status==SC_GFX_E_CANCELLED?SC_GFX_CANVAS_CHANGED:SC_GFX_CANVAS_ERROR,0,status,0,0);
    gfx_canvas_schedule(v);
}
static void gfx_canvas_stroke(ScGfxCanvas *v,ScU32 budget)
{
    if(!v->edit)return;
    ScGfxRect dirty={0};
    ScU32 work=0;
    while(work<budget){
        if(!v->line_active){if(v->point_head==v->point_count)break;
            GfxPoint goal=v->points[v->point_head++];
            v->line_x=v->have_last?v->last_x:goal.x;
            v->line_y=v->have_last?v->last_y:goal.y;
            v->line_end_x=goal.x;
            v->line_end_y=goal.y;
            v->line_dx=goal.x>v->line_x?goal.x-v->line_x:v->line_x-goal.x;
            v->line_dy=-(goal.y>v->line_y?goal.y-v->line_y:v->line_y-goal.y);
            v->line_sx=v->line_x<goal.x?1:-1;
            v->line_sy=v->line_y<goal.y?1:-1;
            v->line_error=v->line_dx+v->line_dy;
            if(v->have_last){if(v->line_x==goal.x && v->line_y==goal.y)continue;
                gfx_canvas_line_advance(v);
            }
            v->line_active=1;
            v->dab_active=0;
        }
        ScI32 radius=(ScI32)v->brush.radius;
        if(!v->dab_active){v->dab_x=v->dab_y=-radius;
            v->dab_active=1;
        }
        while(v->dab_y<=radius && work<budget){ScI32 dx=v->dab_x,dy=v->dab_y,x=v->line_x+dx,y=v->line_y+dy;
            ScI32 distance=dx*dx+dy*dy,outside=radius*radius,inside=(radius-1)*(radius-1);
            work++;
            if(distance<outside && x>=0 && y>=0 && (ScU32)x<v->surface->width && (ScU32)y<v->surface->height){
                ScU32 coverage=distance<=inside?255u:(ScU32)(outside-distance)*255u/(ScU32)(outside-inside);
                ScU32 alpha=((v->brush.argb>>24)*v->brush.opacity*coverage+32512u)/65025u;
                ScU32 color=(v->brush.argb&0xffffffu)|(alpha<<24),before=v->surface->pixels[(ScU32)y*v->surface->width+(ScU32)x];
                ScStatus status=ScGfxEditPixel(v->edit,x,y,color,v->tool==SC_GFX_TOOL_ERASER?SC_GFX_BLEND_ERASE:v->brush.blend);
                if(status<0){gfx_canvas_abort_edit(v,status);
                    return;
                }
                if(before!=v->surface->pixels[(ScU32)y*v->surface->width+(ScU32)x])gfx_union(&dirty,x,y);
            }
            if(++v->dab_x>radius){v->dab_x=-radius;
                v->dab_y++;
            }
        }
        if(v->dab_y<=radius)break;
        v->dab_active=0;
        if(v->line_x==v->line_end_x && v->line_y==v->line_end_y){v->last_x=v->line_end_x;
            v->last_y=v->line_end_y;
            v->have_last=1;
            v->line_active=0;
        }
        else gfx_canvas_line_advance(v);
    }
    if(dirty.width){gfx_epoch(v->surface);
        gfx_canvas_render_start(v,&dirty);
        gfx_canvas_notify(v,SC_GFX_CANVAS_CHANGED,&dirty,0,0,0);
    }
    if(v->edit && !v->down && !v->line_active && v->point_head==v->point_count){ScStatus status=ScGfxEditCommit(v->edit);
        if(status<0){gfx_canvas_abort_edit(v,status);
            return;
        }v->edit=0;
        v->point_head=v->point_count=0;
        v->render_epoch=v->surface->change_epoch;
        if(!v->render_rect.width)v->cache_epoch=v->render_epoch;
        gfx_canvas_notify(v,SC_GFX_CANVAS_CHANGED,&dirty,0,0,0);
    }
}
static ScStatus gfx_canvas_event(ScUiContext *,const ScUiEvent *,void *);
static void gfx_canvas_schedule(ScGfxCanvas *v)
{
    if(!v->node || v->closing)return;
    ScU32 cpu=v->operation || (v->edit && (v->line_active || v->point_head<v->point_count || !v->down));
    ScU32 flags=cpu?SC_UI_TIMER_BACKGROUND:SC_UI_TIMER_DRAW_ONLY;
    if(!cpu && !v->render_rect.width){if(v->timer)ScUiTimerCancel(v->ui,v->timer);
        v->timer=0;
        return;
    }
    if(v->timer && v->timer_flags==flags)return;
    ScUiTimerDesc d;
    gfx_record(&d,sizeof(d));
    d.node=v->node;
    d.flags=flags;
    d.period_ticks=1;
    ScMonotonicClock now;
    gfx_record(&now,sizeof(now));
    if(ScSystemMonotonic(&now)<0)return;
    d.deadline=now.ticks;
    if(++d.deadline.lo==0)d.deadline.hi++;
    ScStatus status=v->timer?ScUiTimerChange(v->ui,v->timer,&d):ScUiTimerSet(v->ui,&d,&v->timer);
    if(status==SC_UI_E_STALE){v->timer=0;
        status=ScUiTimerSet(v->ui,&d,&v->timer);
    }if(status==0)v->timer_flags=flags;
}
static void gfx_canvas_tick(ScGfxCanvas *v)
{
    if(v->operation){ScStatus status=ScGfxOperationStep(v->operation,v->quantum);
        ScGfxOperationInfo info;
        gfx_record(&info,sizeof(info));
        ScGfxOperationQuery(v->operation,&info);
        if(info.state!=SC_GFX_OP_RUNNING){ScStatus closed=ScGfxOperationClose(v->operation);
            if(closed==0){v->operation=0;
                gfx_canvas_render_start(v,0);
                gfx_canvas_notify(v,status<0?SC_GFX_CANVAS_ERROR:SC_GFX_CANVAS_CHANGED,0,status,0,0);
    }}}
    if(v->edit)gfx_canvas_stroke(v,v->quantum);
    gfx_canvas_render(v,v->quantum);
    gfx_canvas_schedule(v);
}
static ScStatus gfx_canvas_begin_stroke(ScGfxCanvas *v,ScI32 x,ScI32 y,const char *label)
{
    if(v->flags&SC_GFX_CANVAS_READONLY)return SC_GFX_E_BUSY;
    if(v->edit || v->operation)return SC_GFX_E_BUSY;
    ScStatus status=ScGfxEditBegin(v->surface,label,&v->edit);
    if(status<0)return status;
    v->have_last=v->line_active=v->dab_active=v->point_head=v->point_count=0;
    status=gfx_canvas_enqueue(v,x,y);
    if(status<0){gfx_canvas_abort_edit(v,status);
        return status;
    }return 0;
}
static void gfx_canvas_end(ScGfxCanvas *v,ScI32 px,ScI32 py)
{
    if(!v->down)return;
    ScI32 x=0,y=0;
    gfx_canvas_point(v,px,py,&x,&y,1);
    v->down=0;
    v->drag=0;
    if(v->preview){v->preview=0;
        if(v->tool==SC_GFX_TOOL_SELECT){v->selection.x=x<v->down_x?x:v->down_x;
            v->selection.y=y<v->down_y?y:v->down_y;
            v->selection.width=(x>v->down_x?x-v->down_x:v->down_x-x)+1;
            v->selection.height=(y>v->down_y?y-v->down_y:v->down_y-y)+1;
            gfx_canvas_notify(v,SC_GFX_CANVAS_SELECTION,&v->selection,0,x,y);
            ScUiInvalidate(v->ui,v->node,0);
        }else{ScStatus status=gfx_canvas_begin_stroke(v,v->down_x,v->down_y,v->tool==SC_GFX_TOOL_LINE?"Line":"Rectangle");
            if(status==0 && v->tool==SC_GFX_TOOL_RECTANGLE)status=gfx_canvas_enqueue(v,x,v->down_y);
            if(status==0)status=gfx_canvas_enqueue(v,x,y);
            if(status==0 && v->tool==SC_GFX_TOOL_RECTANGLE)status=gfx_canvas_enqueue(v,v->down_x,y);
            if(status==0 && v->tool==SC_GFX_TOOL_RECTANGLE)status=gfx_canvas_enqueue(v,v->down_x,v->down_y);
            if(status<0)gfx_canvas_abort_edit(v,status);
        }
    }else if(v->edit){ScStatus status=gfx_canvas_enqueue(v,x,y);
        if(status<0)gfx_canvas_abort_edit(v,status);
    }
    if(v->node)ScUiCapture(v->ui,v->node,0);
    gfx_canvas_tick(v);
}
static ScStatus gfx_canvas_event(ScUiContext *ui,const ScUiEvent *e,void *context)
{
    (void)ui;
    ScGfxCanvas *v=context;
    if(!v || v->magic!=GFX_CANVAS_MAGIC)return 0;
    v->depth++;
    ScStatus result=0;
    if(e->type==SC_UI_EVENT_DESTROY){v->node=v->timer=0;
        v->down=v->preview=v->drag=0;
        goto done;
    }
    if(v->closing)goto done;
    if(e->type==SC_UI_EVENT_TIMER){gfx_canvas_tick(v);
        goto done;
    }
    if(e->type==SC_UI_EVENT_CAPTURE_LOST || e->type==SC_UI_EVENT_INPUT_SYNC){if(v->down)gfx_canvas_end(v,v->last_mouse_x,v->last_mouse_y);
        goto done;
    }
    if(e->type==SC_UI_EVENT_KEY_DOWN){if(e->key==27 && (v->edit || v->operation || v->preview)){ScGfxCanvasCancel(v);
            result=SC_UI_HANDLED;
        }
        else if(e->key=='f' && !(e->modifiers&(SC32_MOD_CONTROL|SC32_MOD_ALT))){ScGfxCanvasSetMode(v,SC_GFX_VIEW_FIT,100);
            result=SC_UI_HANDLED;
        }
        else if(e->key=='1' && !(e->modifiers&(SC32_MOD_CONTROL|SC32_MOD_ALT))){ScGfxCanvasSetMode(v,SC_GFX_VIEW_PERCENT,100);
            result=SC_UI_HANDLED;
        }
        else if(e->key=='c' && !(e->modifiers&(SC32_MOD_CONTROL|SC32_MOD_ALT))){ScGfxCanvasCenter(v);
            result=SC_UI_HANDLED;
        }
        goto done;
    }
    if(e->type==SC_UI_EVENT_WHEEL){if(e->modifiers&SC32_MOD_CONTROL){ScU32 next=v->mode==SC_GFX_VIEW_PERCENT?v->percent:100u;
            ScI32 delta=e->message?(ScI32)e->message->data[3]:0;
            next=delta>0?next+25u:next>25u?next-25u:10u;
            ScGfxCanvasSetMode(v,SC_GFX_VIEW_PERCENT,next>800u?800u:next);
        }
        else{ScI32 delta=e->message?(ScI32)e->message->data[3]:0;
            ScGfxCanvasPan(v,v->pan_x,v->pan_y-delta*32);
        }result=SC_UI_HANDLED;
        goto done;
    }
    if(e->type==SC_UI_EVENT_POINTER_DOWN){ScU32 edge=e->message?e->message->data[3]:e->buttons;
        if(edge&2u){if(v->flags&SC_GFX_CANVAS_READONLY)ScGfxCanvasSetMode(v,v->mode==SC_GFX_VIEW_FIT?SC_GFX_VIEW_PERCENT:SC_GFX_VIEW_FIT,100);
            else gfx_canvas_notify(v,SC_GFX_CANVAS_CONTEXT,0,0,e->x,e->y);
            result=SC_UI_HANDLED;
            goto done;
        }
        if(!(edge&1u) || !v->surface)goto done;
        ScI32 x=0,y=0;
        if(!gfx_canvas_point(v,e->x,e->y,&x,&y,0))goto done;
        v->last_mouse_x=e->x;
        v->last_mouse_y=e->y;
        v->down_x=x;
        v->down_y=y;
        if(v->tool==SC_GFX_TOOL_PAN || (v->flags&SC_GFX_CANVAS_READONLY && v->tool!=SC_GFX_TOOL_SELECT && v->tool!=SC_GFX_TOOL_PICKER)){
            if(v->mode==SC_GFX_VIEW_FIT)goto handled;
            v->down=v->drag=1;
            ScUiCapture(v->ui,v->node,1);
            goto handled;
        }
        if(v->tool==SC_GFX_TOOL_PICKER){v->brush.argb=v->surface->pixels[(ScU32)y*v->surface->width+(ScU32)x];
            gfx_canvas_notify(v,SC_GFX_CANVAS_COLOR,0,0,x,y);
            goto handled;
        }
        if(v->tool==SC_GFX_TOOL_FILL){if(!(v->flags&SC_GFX_CANVAS_READONLY) && !v->operation){ScU32 color=(v->brush.argb&0xffffffu)|(((v->brush.argb>>24)*v->brush.opacity+127u)/255u<<24);
                ScStatus s=ScGfxFillBegin(v->surface,x,y,color,&v->operation);
                if(s<0)gfx_canvas_notify(v,SC_GFX_CANVAS_ERROR,0,s,x,y);
                else{gfx_canvas_notify(v,SC_GFX_CANVAS_BUSY,0,0,x,y);
                    gfx_canvas_schedule(v);
            }}goto handled;
        }
        if(v->tool==SC_GFX_TOOL_LINE || v->tool==SC_GFX_TOOL_RECTANGLE || v->tool==SC_GFX_TOOL_SELECT){v->down=v->preview=1;
            ScUiCapture(v->ui,v->node,1);
            ScUiInvalidate(v->ui,v->node,0);
            goto handled;
        }
        {ScStatus status=gfx_canvas_begin_stroke(v,x,y,v->tool==SC_GFX_TOOL_ERASER?"Eraser":"Brush stroke");
            if(status<0)gfx_canvas_notify(v,SC_GFX_CANVAS_ERROR,0,status,x,y);
            else{v->down=1;
                ScUiCapture(v->ui,v->node,1);
                gfx_canvas_tick(v);
        }}
        handled:result=SC_UI_HANDLED;
        goto done;
    }
    if(e->type==SC_UI_EVENT_POINTER_MOVE && v->down){
        if(v->drag){if(e->x!=v->last_mouse_x || e->y!=v->last_mouse_y)ScGfxCanvasPan(v,v->pan_x+v->last_mouse_x-e->x,v->pan_y+v->last_mouse_y-e->y);
        }
        else if(v->preview){if(e->x!=v->last_mouse_x || e->y!=v->last_mouse_y)ScUiInvalidate(v->ui,v->node,0);
        }
        else if(v->edit){ScI32 x,y;
            if(gfx_canvas_point(v,e->x,e->y,&x,&y,1)){ScStatus s=gfx_canvas_enqueue(v,x,y);
                if(s<0)gfx_canvas_abort_edit(v,s);
                else gfx_canvas_schedule(v);
        }}
        v->last_mouse_x=e->x;
        v->last_mouse_y=e->y;
        if(!(e->buttons&1u))gfx_canvas_end(v,e->x,e->y);
        result=SC_UI_HANDLED;
        goto done;
    }
    if(e->type==SC_UI_EVENT_POINTER_UP && v->down){ScU32 edge=e->message?e->message->data[3]:1u;
        if(edge&1u){gfx_canvas_end(v,e->x,e->y);
            result=SC_UI_HANDLED;
    }}
    done:gfx_canvas_leave(v);
    return result;
}
static ScStatus gfx_canvas_cleanup(ScUiContext *ui,const ScUiEvent *e,void *context)
{(void)ui;
    ScGfxCanvas *v=context;
    if(e->type==SC_UI_EVENT_TIMER && !e->node && v && v->closing)gfx_canvas_destroy(v);
    return 0;
}
static void gfx_canvas_destroy(ScGfxCanvas *v)
{
    if(!v || v->destroying || v->depth)return;
    v->destroying=1;
    if(v->node){ScUiNode node=v->node;
        ScUiNodeDestroy(v->ui,node);
        v->node=0;
    }
    ScStatus unregistered=v->kind?ScUiClassUnregister(v->ui,v->kind):0;
    if(unregistered==SC_UI_E_BUSY){
        if(!v->cleanup_listener){ScUiSubscription s;
            gfx_record(&s,sizeof(s));
            s.type=SC_UI_EVENT_TIMER;
            s.phases=SC_UI_TARGET_PHASE;
            s.procedure=gfx_canvas_cleanup;
            s.context=v;
            ScUiListen(v->ui,0,&s,&v->cleanup_listener);
        }
        if(!v->cleanup_timer){ScUiTimerDesc d;
            gfx_record(&d,sizeof(d));
            d.flags=SC_UI_TIMER_BACKGROUND;
            d.period_ticks=1;
            ScMonotonicClock now;
            gfx_record(&now,sizeof(now));
            if(ScSystemMonotonic(&now)==0){d.deadline=now.ticks;
                if(++d.deadline.lo==0)d.deadline.hi++;
                ScUiTimerSet(v->ui,&d,&v->cleanup_timer);
        }}
        v->destroying=0;
        return;
    }
    if(v->timer)ScUiTimerCancel(v->ui,v->timer);
    if(v->cleanup_timer)ScUiTimerCancel(v->ui,v->cleanup_timer);
    if(v->cleanup_listener)ScUiDisconnect(v->ui,v->cleanup_listener);
    if(v->edit)ScGfxEditAbort(v->edit);
    if(v->operation){ScGfxOperationCancel(v->operation);
        while(v->operation->state==SC_GFX_OP_RUNNING){ScGfxOperationStep(v->operation,16384);
            ScProcessYield();
        }ScGfxOperationClose(v->operation);
    }
    ScGfxSurfaceRelease(v->surface);
    free(v->cache);
    free(v->pending);
    free(v->scratch);
    free(v->points);
    if(v->user_drop)v->user_drop(v->user,0);
    ScUiContext *ui=v->ui;
    v->magic=0;
    free(v);
    ScUiContextRelease(ui);
}
ScStatus ScGfxCanvasCreate(const ScGfxCanvasDesc *d,ScGfxCanvas **out)
{
    if(!gfx_input(d,sizeof(*d)) || !out || !d->ui || !d->parent || d->flags&~3u || d->reserved[0] || d->reserved[1])return SC_GFX_E_INVALID;
    if(d->surface && !gfx_surface(d->surface))return SC_GFX_E_STALE;
    if(gfx_canvas_class_serial==UINT_MAX)return SC_GFX_E_LIMIT;
    ScGfxCanvas *v=calloc(1,sizeof(*v));
    if(!v)return SC_GFX_E_MEMORY;
    v->magic=GFX_CANVAS_MAGIC;
    v->references=1;
    v->ui=d->ui;
    v->flags=d->flags;
    v->procedure=d->procedure;
    v->user=d->context;
    v->user_drop=d->context_drop;
    v->surface=d->surface;
    v->mode=SC_GFX_VIEW_FIT;
    v->percent=100;
    v->tool=d->flags&SC_GFX_CANVAS_READONLY?SC_GFX_TOOL_PAN:SC_GFX_TOOL_BRUSH;
    v->cache_budget=d->cache_budget_bytes?d->cache_budget_bytes:12u*1024u*1024u;
    v->quantum=d->pixel_quantum?d->pixel_quantum:16384u;
    if(v->quantum>65536u)v->quantum=65536u;
    gfx_record(&v->brush,sizeof(v->brush));
    v->brush.argb=0xff000000u;
    v->brush.radius=3;
    v->brush.opacity=255;
    v->brush.blend=SC_GFX_BLEND_OVER;
    ScStatus s=ScUiContextRetain(v->ui);
    if(s<0){free(v);
        return s;
    }if(v->surface){s=ScGfxSurfaceRetain(v->surface);
        if(s<0){ScUiContextRelease(v->ui);
            free(v);
            return s;
    }}
    if(v->procedure)s=ScCallbackPin((const void *)v->procedure);
    if(s==0 && v->user_drop)s=ScCallbackPin((const void *)v->user_drop);
    v->scratch=malloc(65536);
    if(!v->scratch && s==0)s=SC_GFX_E_MEMORY;
    ScUiClassDesc cls;
    gfx_record(&cls,sizeof(cls));
    snprintf(v->class_name,sizeof(v->class_name),"SCGFX.NativeCanvas.%u",++gfx_canvas_class_serial);
    cls.name=v->class_name;
    cls.context=v;
    cls.measure=gfx_canvas_measure;
    cls.arrange=gfx_canvas_arrange;
    cls.paint=gfx_canvas_paint;
    cls.event=gfx_canvas_event;
    if(s==0)s=ScUiClassRegister(v->ui,&cls,&v->kind);
    ScUiWidgetDesc widget;
    gfx_record(&widget,sizeof(widget));
    widget.kind=v->kind;
    widget.parent=d->parent;
    widget.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    widget.text.size=sizeof(ScUiText);widget.text.version=1;
    widget.text.data="";widget.text.bytes=0;
    if(s==0)s=ScUiWidgetCreate(v->ui,&widget,&v->node);
    if(s<0){v->closing=1;
        v->references=0;
        gfx_canvas_destroy(v);
        return s;
    }*out=v;
    return 0;
}
ScStatus ScGfxCanvasRetain(ScGfxCanvas *v)
{if(!gfx_canvas_valid(v))return SC_GFX_E_STALE;
    if(v->references==UINT_MAX)return SC_GFX_E_LIMIT;
    v->references++;
    return 0;
}
void ScGfxCanvasRelease(ScGfxCanvas *v)
{if(!gfx_canvas_valid(v))return;
    if(--v->references)return;
    v->closing=1;
    if(!v->depth)gfx_canvas_destroy(v);
}
ScStatus ScGfxCanvasQuery(const ScGfxCanvas *v,ScGfxCanvasInfo *out)
{
    if(!gfx_canvas_valid(v) || !gfx_output(out,sizeof(*out)))return SC_GFX_E_INVALID;
    ScGfxCanvasInfo info;
    gfx_record(&info,sizeof(info));
    info.node=v->node;
    info.flags=v->flags;
    info.mode=v->mode;
    info.percent=v->percent;
    info.tool=v->tool;
    info.busy=v->edit || v->operation;
    info.brush=v->brush;
    info.selection=v->selection;
    info.pan_x=v->pan_x;
    info.pan_y=v->pan_y;
    info.viewport_width=(ScU32)v->width;
    info.viewport_height=(ScU32)v->height;
    info.cache_bytes=v->cache_bytes+v->pending_bytes;
    info.rendered_pixels=v->rendered_pixels;
    info.revision=v->surface?v->surface->revision:0;
    memcpy(out,&info,sizeof(info));
    return 0;
}
ScStatus ScGfxCanvasSetSurface(ScGfxCanvas *v,ScGfxSurface *surface)
{
    if(!gfx_canvas_valid(v) || (surface && !gfx_surface(surface)))return SC_GFX_E_STALE;
    if(v->edit || v->operation)return SC_GFX_E_BUSY;
    if(surface){ScStatus s=ScGfxSurfaceRetain(surface);
        if(s<0)return s;
    }ScGfxSurface *old=v->surface;
    v->surface=surface;
    ScGfxSurfaceRelease(old);
    v->selection=gfx_rect(0,0,0,0);
    v->pan_x=v->pan_y=0;
    v->cache_failed=0;
    gfx_canvas_render_start(v,0);
    return 0;
}
ScStatus ScGfxCanvasSetTool(ScGfxCanvas *v,ScU32 tool)
{if(!gfx_canvas_valid(v))return SC_GFX_E_STALE;
    if(tool<1 || tool>8)return SC_GFX_E_INVALID;
    if(v->down || v->edit || v->operation)return SC_GFX_E_BUSY;
    if(v->flags&SC_GFX_CANVAS_READONLY && tool!=SC_GFX_TOOL_PAN && tool!=SC_GFX_TOOL_SELECT && tool!=SC_GFX_TOOL_PICKER)return SC_GFX_E_BUSY;
    v->tool=tool;
    return 0;
}
ScStatus ScGfxCanvasSetBrush(ScGfxCanvas *v,const ScGfxBrush *brush)
{if(!gfx_canvas_valid(v) || !gfx_input(brush,sizeof(*brush)))return SC_GFX_E_INVALID;
    if(!brush->radius || brush->radius>64 || brush->opacity>255 || brush->blend>2 || brush->flags || brush->reserved)return SC_GFX_E_RANGE;
    if(v->down || v->edit || v->operation)return SC_GFX_E_BUSY;
    v->brush=*brush;
    return 0;
}
ScStatus ScGfxCanvasSetMode(ScGfxCanvas *v,ScU32 mode,ScU32 percent)
{if(!gfx_canvas_valid(v))return SC_GFX_E_STALE;
    if((mode!=1 && mode!=2) || percent<10 || percent>800)return SC_GFX_E_RANGE;
    if(v->mode==mode && v->percent==percent)return 0;
    v->depth++;
    if(v->mode==SC_GFX_VIEW_PERCENT && mode==SC_GFX_VIEW_PERCENT){v->pan_x=v->pan_x*(ScI32)percent/(ScI32)v->percent;
        v->pan_y=v->pan_y*(ScI32)percent/(ScI32)v->percent;
    }
    v->mode=mode;
    v->percent=percent;
    v->cache_failed=0;
    gfx_canvas_render_start(v,0);
    gfx_canvas_notify(v,SC_GFX_CANVAS_VIEW,0,0,0,0);
    gfx_canvas_leave(v);
    return 0;
}
ScStatus ScGfxCanvasSetSelection(ScGfxCanvas *v,const ScGfxRect *r)
{if(!gfx_canvas_valid(v) || !v->surface || !r)return SC_GFX_E_INVALID;
    ScGfxRect clipped;
    if(!gfx_clip_rect(v->surface,r,&clipped))return SC_GFX_E_RANGE;
    v->depth++;
    v->selection=clipped;
    if(v->node)ScUiInvalidate(v->ui,v->node,0);
    gfx_canvas_notify(v,SC_GFX_CANVAS_SELECTION,&clipped,0,0,0);
    gfx_canvas_leave(v);
    return 0;
}
ScStatus ScGfxCanvasCenter(ScGfxCanvas *v)
{if(!gfx_canvas_valid(v))return SC_GFX_E_STALE;
    v->depth++;
    gfx_canvas_mapping(v);
    v->pan_x=v->display_width>v->width?(v->display_width-v->width)/2:0;
    v->pan_y=v->display_height>v->height?(v->display_height-v->height)/2:0;
    gfx_canvas_render_start(v,0);
    gfx_canvas_notify(v,SC_GFX_CANVAS_VIEW,0,0,0,0);
    gfx_canvas_leave(v);
    return 0;
}
ScStatus ScGfxCanvasPan(ScGfxCanvas *v,ScI32 x,ScI32 y)
{if(!gfx_canvas_valid(v))return SC_GFX_E_STALE;
    gfx_canvas_mapping(v);
    x=gfx_clamp(x,0,v->display_width-v->width);
    y=gfx_clamp(y,0,v->display_height-v->height);
    if(v->pan_x==x && v->pan_y==y)return 0;
    v->depth++;
    v->pan_x=x;
    v->pan_y=y;
    gfx_canvas_render_start(v,0);
    gfx_canvas_notify(v,SC_GFX_CANVAS_VIEW,0,0,0,0);
    gfx_canvas_leave(v);
    return 0;
}
ScStatus ScGfxCanvasCancel(ScGfxCanvas *v)
{if(!gfx_canvas_valid(v))return SC_GFX_E_STALE;
    v->depth++;
    v->preview=v->down=v->drag=0;
    if(v->operation)ScGfxOperationCancel(v->operation);
    if(v->edit)gfx_canvas_abort_edit(v,SC_GFX_E_CANCELLED);
    if(v->node)ScUiCapture(v->ui,v->node,0);
    gfx_canvas_schedule(v);
    gfx_canvas_leave(v);
    return 0;
}
