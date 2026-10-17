#include "SCUIPRIVATE.H"

struct UiEdit;
typedef struct UiTabItem UiTabItem;
struct UiTabItem {UiTabItem *next;
    ScUiNode page;
    ScString *title;
    ScI32 x,width;
};
typedef struct {ScUiStatusPart description;
    ScString *text;
}
UiStatusItem;
typedef struct UiControl UiControl;
struct UiControl {
    struct UiEdit *edit;
    UiTabItem *tabs;
    UiStatusItem *parts;
    ScU32 part_count,selected_page;
    ScI32 minimum,maximum,step,split_position,drag_origin,drag_value;
    ScU32 pressed_key,pressed_part,progress,total,progress_mode,animation,timer,tooltip_timer,popup;
    UiControl *popup_previous,*popup_next;
    UiNode *node;
};
static UiControl *ui_control_popups;
static ScStatus ui_edit_init(UiNode *);
static void ui_edit_destroy(UiNode *);
static ScStatus ui_edit_measure(UiNode *,const ScUiSize *,ScUiSize *);
static ScStatus ui_edit_paint(UiNode *,ScUiPainter *);
static ScStatus ui_edit_event(UiNode *,const ScUiEvent *);
static void ui_edit_layout(UiNode *);
static ScStatus ui_edit_text(UiNode *,const ScUiText *);
static ScStatus ui_edit_copy_text(UiNode *,ScString **);
static ScStatus ui_edit_value(UiNode *,const ScValue *);
static void ui_control_spin_initial(UiNode *);

static ScU32 ui_control_fraction(ScU32 value,ScU32 scale,ScU32 total)
{
    ScWideCounter product={0,0},part={value,0};
    ScU32 remainder=0,answer=0;
    if(!total)return 0;
    for(ScU32 bit=0;bit<32;bit++){
        if(scale&(1u<<bit)){ScU32 old=product.lo;
            product.lo+=part.lo;
            product.hi+=part.hi+(product.lo<old);
        }
        part.hi=(part.hi<<1)|(part.lo>>31);
        part.lo<<=1;
    }
    for(ScU32 bit=64;bit;bit--){ScU32 at=bit-1,carry=remainder>>31;
        remainder=(remainder<<1)|((at>=32?product.hi>>(at-32):product.lo>>at)&1);
        if(carry || remainder>=total){remainder-=total;
            if(at>=32)return UINT_MAX;
            answer|=1u<<at;
        }
    }
    return answer;
}

static UiControl *ui_control(UiNode *n){return n?(UiControl *)n->control:0;
}
static int ui_control_left_edge(const ScUiEvent *e)
{return e->message && (e->message->data[3]&1u);
}
static int ui_control_point_inside(UiNode *n,const ScUiEvent *e)
{
    ScI32 x,y;
    if(!ui_signed_add(n->rectangle.x,e->x,&x) || !ui_signed_add(n->rectangle.y,e->y,&y))return 0;
    return ui_controls_contains(n,x,y);
}
static int ui_control_editor(UiNode *n){return n->kind==SC_UI_EDIT || n->kind==SC_UI_TEXTBOX || n->kind==SC_UI_SPINBOX || (n->kind==SC_UI_LABEL && (n->flags&SC_UI_LABEL_SELECTABLE));
}
static ScStatus ui_control_result(UiNode *n,ScStatus s)
{return s<0?ui_fail(n->owner,s,s,0,UI_OP_NODE,n->id):s;
}
static void ui_control_timer_cancel(UiNode *n,ScU32 *token)
{if(*token){ScUiTimerCancel(n->owner,*token);
        *token=0;
    }
}
static ScStatus ui_control_timer(UiNode *n,ScU32 delay,ScU32 period,ScU32 *token)
{
    ScUiTimerDesc d;
    ScStatus s;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.node=n->id;
    d.flags=SC_UI_TIMER_DRAW_ONLY;
    s=ui_now(&d.deadline);
    if(s<0)return ui_cause(n->owner,s,UI_OP_TIMER,n->id);
    ui_wide_add(&d.deadline,delay);
    d.period_ticks=period;
    return *token?ScUiTimerChange(n->owner,*token,&d):ScUiTimerSet(n->owner,&d,token);
}
static void ui_control_popup_close(UiNode *n)
{
    UiControl *control=ui_control(n);
    if(!control)return;
    if(control->popup_previous)control->popup_previous->popup_next=control->popup_next;
    else if(ui_control_popups==control)ui_control_popups=control->popup_next;
    if(control->popup_next)control->popup_next->popup_previous=control->popup_previous;
    control->popup_previous=control->popup_next=0;
    ScUiNode popup=control->popup;
    control->popup=0;
    n->state&=~SC_UI_EXPANDED;
    ui_tree_effective(n);
    if(popup)ui_node_damage(n);
    if(ui_node(n->owner,popup))ScUiNodeDestroy(n->owner,popup);
}
static ScStatus ui_controls_init(UiNode *n)
{
    UiControl *p;
    ScStatus s=0;
    if(n->custom)return 0;
    if(n->kind<SC_UI_WINDOW || (n->kind>SC_UI_PROPERTY_PAGE && n->kind!=SC_UI_SECRET))return SC_UI_E_INVALID;
    p=(UiControl *)calloc(1,sizeof(*p));
    if(!p)return SC_UI_E_MEMORY;
    n->control=p;
    p->minimum=INT_MIN;
    p->maximum=INT_MAX;
    p->step=1;
    p->total=100;
    p->split_position=-1;
    p->node=n;
    if(n->kind==SC_UI_BUTTON || n->kind==SC_UI_CHECKBOX || n->kind==SC_UI_RADIO || n->kind==SC_UI_TEXTBOX
        || n->kind==SC_UI_EDIT || n->kind==SC_UI_COMBOBOX || n->kind==SC_UI_SPINBOX || n->kind==SC_UI_TAB
        || n->kind==SC_UI_TREE || n->kind==SC_UI_LIST || n->kind==SC_UI_TABLE || n->kind==SC_UI_SCROLL || n->kind==SC_UI_SPLITTER)
    n->flags|=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    if(n->kind==SC_UI_TAB)n->layout.mode=SC_UI_OVERLAY;
    if(n->kind==SC_UI_LABEL && (n->flags&SC_UI_LABEL_SELECTABLE)){n->flags|=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
        n->state|=SC_UI_READONLY;
    }
    if(n->kind==SC_UI_SPLITTER){n->layout.width=4;
        n->layout.min_width=4;
    }
    if(n->kind==SC_UI_SECRET)s=ui_secret_init(n);
    else if(ui_control_editor(n))s=ui_edit_init(n);
    if(s==0 && n->kind==SC_UI_SPINBOX)ui_control_spin_initial(n);
    return s;
}
static void ui_controls_destroy(UiNode *n,int quiet)
{
    UiControl *p=ui_control(n);
    if(!p)return;
    if(n->kind==SC_UI_SECRET)ui_secret_destroy(n,quiet);
    if(p->edit)ui_edit_destroy(n);
    if(!quiet)ui_control_popup_close(n);
    else{if(p->popup_previous)p->popup_previous->popup_next=p->popup_next;
        else if(ui_control_popups==p)ui_control_popups=p->popup_next;
        if(p->popup_next)p->popup_next->popup_previous=p->popup_previous;
    }
    UiTabItem *tab=p->tabs;
    while(tab){UiTabItem *next=tab->next;
        ScStringRelease(tab->title);
        free(tab);
        tab=next;
    }
    for(ScU32 i=0;i<p->part_count;i++){ScStringRelease(p->parts[i].text);
        ScUiIconRelease(p->parts[i].description.icon);
    }
    free(p->parts);
    free(p);
    n->control=0;
}
static ScStatus ui_controls_set_text(UiNode *n,const ScUiText *text)
{
    if(!ui_control_editor(n))return 0;
    ScI32 number=0;
    int valid=0;
    if(n->kind==SC_UI_SPINBOX){ScString *candidate=0;
        const char *z;
        ScStatus s=ui_text_copy(text,&candidate);
        if(s<0)return ui_cause(n->owner,s,UI_OP_NODE,n->id);
        s=ScStringCStr(candidate,&z);
        if(s==0){char *tail;
            errno=0;
            long parsed=strtol(z,&tail,10);
            UiControl *c=ui_control(n);
            valid=!errno && tail!=z && !*tail && !memchr(z,0,text->bytes) && parsed>=c->minimum && parsed<=c->maximum;
            number=(ScI32)parsed;
        }
        ScStringRelease(candidate);
        if(s<0)return ui_cause(n->owner,s,UI_OP_NODE,n->id);
    }
    ScStatus s=ui_edit_text(n,text);
    if(s==0 && n->kind==SC_UI_SPINBOX){
        if(valid){ScValue value={sizeof(ScValue),1,SC_VALUE_I32,0,(ScU32)number,0,0,0};
            n->value.value=value;
            n->state&=~SC_UI_INVALID;
        }
        else n->state|=SC_UI_INVALID;
        ui_tree_effective(n);
    }
    return s;
}
static ScStatus ui_controls_copy_text(UiNode *n,ScString **out)
{return ui_control_editor(n)?ui_edit_copy_text(n,out):SC_UI_E_INVALID;
}
static void ui_control_spin_initial(UiNode *n)
{
    const char *z;
    ScUiText text;
    UiControl *control=ui_control(n);
    ui_text_view(n->text,&text);
    if(ScStringCStr(n->text,&z)<0){n->state|=SC_UI_INVALID;
        return;
    }
    char *tail;
    errno=0;
    long number=strtol(z,&tail,10);
    int valid=!errno && tail!=z && !*tail && !memchr(z,0,text.bytes) && number>=control->minimum && number<=control->maximum;
    ScValue value={sizeof(ScValue),1,SC_VALUE_I32,0,valid?(ScU32)number:0,0,0,0};
    n->value.value=value;
    if(!valid)n->state|=SC_UI_INVALID;
}
static ScStatus ui_controls_set_value(UiNode *n,const ScValue *value)
{
    UiControl *p=ui_control(n);
    if(!p)return 0;
    if(n->kind==SC_UI_SPINBOX){
        if(value->type!=SC_VALUE_I32 || value->hi || value->flags || value->data || value->bytes)return SC_UI_E_INVALID;
        ScI32 number=(ScI32)value->lo;
        if(number<p->minimum || number>p->maximum)return SC_UI_E_RANGE;
        return ui_edit_value(n,value);
    }
    if(n->kind==SC_UI_COMBOBOX && n->view){
        if(value->type!=SC_VALUE_U32 || value->flags || value->hi || value->data || value->bytes)return SC_UI_E_INVALID;
        return ScUiSelectionSet(n->view->selection,value->lo,SC_UI_REPLACE);
    }
    if(n->kind==SC_UI_CHECKBOX || n->kind==SC_UI_RADIO){
        if((value->type!=SC_VALUE_BOOL && value->type!=SC_VALUE_U32) || value->flags || value->hi || value->data || value->bytes
            || value->lo>(n->kind==SC_UI_RADIO?1u:2u))return SC_UI_E_INVALID;
        n->state&=~(SC_UI_CHECKED|SC_UI_MIXED);
        if(value->lo==1)n->state|=SC_UI_CHECKED;
        if(value->lo==2)n->state|=SC_UI_MIXED;
        ui_tree_effective(n);
    }
    return 0;
}
static ScStatus ui_controls_measure(UiNode *n,const ScUiSize *constraint,ScUiSize *out)
{
    ScUiText text;
    ScUiTextMetrics m;
    ScU32 line,baseline;
    ScStatus s;
    ScI32 pad=ui_scale(n->owner,8);
    out->width=out->height=0;
    ui_font_line(n->owner,n->font_role,&line,&baseline);
    (void)baseline;
    if(n->kind==SC_UI_SECRET)return ui_secret_measure(n,constraint,out);
    if(ui_control_editor(n))return ui_edit_measure(n,constraint,out);
    if(n->kind==SC_UI_TREE || n->kind==SC_UI_LIST || n->kind==SC_UI_TABLE){out->width=ui_scale(n->owner,180);
        out->height=(ScI32)line*6+pad;
        return 0;
    }
    ui_text_view(n->text,&text);
    memset(&m,0,sizeof(m));
    m.size=sizeof(m);
    m.version=1;
    s=ui_text_measure(n->owner,n->font_role,&text,SC_UI_AUTO,0,&m);
    if(s<0)return s;
    if((n->kind==SC_UI_LABEL || n->kind==SC_UI_TOOLTIP) && constraint->width>0){ScUiTextMetrics wrapped;
        ScI32 width=constraint->width;
        if(n->kind==SC_UI_TOOLTIP)width-=ui_scale(n->owner,8);
        if(n->icon)width-=ui_scale(n->owner,20);
        if(width<1)width=1;
        /* Label绘制使用真实物理宽度换行；自然宽度保持不变，
         * 只更新高度，使第二遍候选安排不会反过来改变水平分配。 */
        s=ui_text_measure(n->owner,n->font_role,&text,width,UI_TEXT_WRAP,&wrapped);
        if(s<0)return s;
        m.height=wrapped.height;
    }
    if(m.width>(ScU32)(INT_MAX-pad*2) || m.height>(ScU32)(INT_MAX-pad*2) || line>(ScU32)(INT_MAX-pad*2))return SC_UI_E_RANGE;
    switch(n->kind){
    case SC_UI_BUTTON:out->width=(ScI32)m.width+pad*3;
        if(out->width<ui_scale(n->owner,72))out->width=ui_scale(n->owner,72);
        out->height=(ScI32)line+pad;
        break;
    case SC_UI_CHECKBOX:case SC_UI_RADIO:out->width=(ScI32)m.width+(ScI32)line+pad;
        out->height=(ScI32)line+pad/2;
        break;
    case SC_UI_LABEL:case SC_UI_TOOLTIP:out->width=(ScI32)m.width;
        out->height=(ScI32)m.height;
        if(n->kind==SC_UI_TOOLTIP){ScI32 inset=ui_scale(n->owner,8);
            if(!ui_signed_add(out->width,inset,&out->width) || !ui_signed_add(out->height,inset,&out->height))return SC_UI_E_RANGE;
        }
        break;
    case SC_UI_COMBOBOX:out->width=(ScI32)m.width+pad*4;
        if(out->width<ui_scale(n->owner,140))out->width=ui_scale(n->owner,140);
        out->height=(ScI32)line+pad;
        break;
    case SC_UI_TAB:out->width=ui_scale(n->owner,120);
        out->height=ui_scale(n->owner,26);
        break;
    case SC_UI_STATUS:out->height=(ScI32)line+ui_scale(n->owner,6);
        break;
    case SC_UI_PROGRESS:out->width=ui_scale(n->owner,160);
        out->height=ui_scale(n->owner,18);
        break;
    case SC_UI_SCROLL:out->width=out->height=ui_scale(n->owner,16);
        break;
    case SC_UI_SPLITTER:out->width=out->height=ui_scale(n->owner,4);
        break;
    case SC_UI_GROUPBOX:out->width=(ScI32)m.width+pad*2;
        out->height=(ScI32)line+pad*2;
        break;
    default:break;
    }
    if(n->icon){ScI32 icon=ui_scale(n->owner,16);
        out->width+=icon+pad;
        if(out->height<icon+pad)out->height=icon+pad;
    }
    return 0;
}
static void ui_control_focus(ScUiPainter *p,const ScUiRect *area)
{
    ScUiRect pixel={0,0,1,1};
    if(area->width<2 || area->height<2)return;
    for(ScI32 x=area->x;x<area->x+area->width;x+=2){pixel.x=x;
        pixel.y=area->y;
        ui_fill(p,&pixel,SC32_COLOR_ACCENT);
        pixel.y=area->y+area->height-1;
        ui_fill(p,&pixel,SC32_COLOR_ACCENT);
    }
    for(ScI32 y=area->y+1;y<area->y+area->height;y+=2){pixel.y=y;
        pixel.x=area->x;
        ui_fill(p,&pixel,SC32_COLOR_ACCENT);
        pixel.x=area->x+area->width-1;
        ui_fill(p,&pixel,SC32_COLOR_ACCENT);
    }
}
static int ui_control_rounded(UiNode *n)
{return n->kind==SC_UI_SECRET || n->kind==SC_UI_BUTTON || n->kind==SC_UI_TEXTBOX || n->kind==SC_UI_EDIT || n->kind==SC_UI_SPINBOX || n->kind==SC_UI_COMBOBOX || n->kind==SC_UI_PROGRESS || n->kind==SC_UI_TOOLTIP;
}
static ScI32 ui_control_radius(UiNode *n,const ScUiRect *r)
{
    if(n->owner->theme.style==SC32_STYLE_CLASSIC || !ui_control_rounded(n))return 0;
    ScI32 radius=ui_scale(n->owner,(ScI32)n->owner->theme.radius);
    if(radius>r->width/2)radius=r->width/2;
    if(radius>r->height/2)radius=r->height/2;
    return radius;
}
static ScI32 ui_control_round_inset(ScI32 y,ScI32 height,ScI32 radius)
{
    if(y>=height-radius)y=height-1-y;
    if(y>=radius || radius<1)return 0;
    ScI32 dy=radius*2-1-y*2,inset=radius;
    while(inset>0){ScI32 dx=radius*2-1-(inset-1)*2;
        if(dx*dx+dy*dy>radius*radius*4)break;
        inset--;
    }
    return inset;
}
static void ui_control_round_fill(ScUiPainter *p,const ScUiRect *area,ScI32 radius,ScU32 role)
{
    for(ScI32 y=0;y<area->height;y++){ScI32 inset=ui_control_round_inset(y,area->height,radius);
        ScUiRect row={area->x+inset,area->y+y,area->width-inset*2,1};
        ui_fill(p,&row,role);
    }
}
static void ui_control_surface(UiNode *n,ScUiPainter *p,const ScUiRect *area,ScU32 role,int pressed)
{
    ScI32 radius=ui_control_radius(n,area);
    if(!radius){ui_fill(p,area,role);
        ui_border(p,area,pressed);
        return;
    }
    ScI32 edge=ui_scale(n->owner,1);
    if(edge<1)edge=1;
    ScUiRect inner={area->x+edge,area->y+edge,area->width-edge*2,area->height-edge*2};
    ui_control_round_fill(p,area,radius,SC32_COLOR_LINE);
    if(inner.width>0 && inner.height>0)ui_control_round_fill(p,&inner,radius>edge?radius-edge:0,role);
}
static int ui_controls_contains(UiNode *n,ScI32 x,ScI32 y)
{
    if(!ui_rect_contains(&n->rectangle,x,y))return 0;
    ScUiRect area={0,0,n->rectangle.width,n->rectangle.height};
    ScI32 radius=ui_control_radius(n,&area);
    ScI32 inset=ui_control_round_inset(y-n->rectangle.y,area.height,radius),local=x-n->rectangle.x;
    return local>=inset && local<area.width-inset;
}
static void ui_control_arrow(ScUiPainter *p,ScI32 x,ScI32 y,ScI32 size,int direction,ScU32 role)
{
    for(ScI32 i=0;i<size;i++){ScUiRect row;
        if(direction==0 || direction==1){row.x=x+i;
            row.y=direction?y+size-i-1:y+i;
            row.width=size*2-i*2-1;
            row.height=1;
        }
        else{row.x=direction==2?x+i:x+size-i-1;
            row.y=y+i;
            row.width=1;
            row.height=size*2-i*2-1;
        }
        ui_fill(p,&row,role);
    }
}
static ScStatus ui_control_label(UiNode *n,ScUiPainter *p,ScUiRect *r,ScU32 flags)
{
    ScUiText text;
    ui_text_view(n->text,&text);
    p->text_role=(n->effective&SC_UI_ENABLED)?SC32_COLOR_TEXT:SC32_COLOR_MUTED;
    if(n->icon){ScI32 size=ui_scale(n->owner,16);
        ScUiRect icon={r->x,r->y+(r->height-size)/2,size,size};
        ScUiPainterIcon(p,&icon,n->icon,n->effective);
        r->x+=size+ui_scale(n->owner,4);
        r->width-=size+ui_scale(n->owner,4);
    }
    return ScUiPainterText(p,r,&text,n->font_role,flags);
}
static ScStatus ui_controls_paint(UiNode *n,ScUiPainter *p)
{
    UiControl *c=ui_control(n);
    ScUiRect r={0,0,n->rectangle.width,n->rectangle.height},text=r;
    ScI32 pad=ui_scale(n->owner,4);
    if(n->kind==SC_UI_SECRET)return ui_secret_paint(n,p);
    if(ui_control_editor(n))return ui_edit_paint(n,p);
    if(n->kind==SC_UI_TREE || n->kind==SC_UI_LIST || n->kind==SC_UI_TABLE)return ui_view_paint(n,p);
    if(n->kind==SC_UI_BUTTON){
        UiNode *root=ui_root(n->owner,n->id);
        ui_control_surface(n,p,&r,(n->effective&SC_UI_HOVER)?SC32_COLOR_FACE_ALT:SC32_COLOR_FACE,(n->effective&SC_UI_PRESSED)?1:0);
        if(root && root->window->default_button==n->id){ScUiRect ring={1,1,r.width-2,r.height-2};
            ScUiPainterLine(p,ring.x,ring.y,ring.x+ring.width-1,ring.y,SC32_COLOR_ACCENT);
            ScUiPainterLine(p,ring.x,ring.y+ring.height-1,ring.x+ring.width-1,ring.y+ring.height-1,SC32_COLOR_ACCENT);
        }
        text.x=pad;
        text.y=pad/2;
        text.width-=pad*2;
        text.height-=pad;
        if(n->effective&SC_UI_PRESSED){text.x++;
            text.y++;
        }
        ScStatus s=ui_control_label(n,p,&text,UI_TEXT_CENTER|UI_TEXT_ELLIPSIS);
        if(n->effective&SC_UI_FOCUSED){ScUiRect focus={pad,pad,r.width-pad*2,r.height-pad*2};
            ui_control_focus(p,&focus);
        }
        return s;
    }
    if(n->kind==SC_UI_CHECKBOX || n->kind==SC_UI_RADIO){
        ScI32 side=ui_scale(n->owner,13);
        ScUiRect box={1,(r.height-side)/2,side,side};
        ui_fill(p,&r,SC32_COLOR_FACE);
        if(n->kind==SC_UI_RADIO){
            ScI32 middle=side/2;
            for(ScI32 y=0;y<side;y++){ScI32 dy=y-middle,half=middle;
                while(half && half*half+dy*dy>middle*middle)half--;
                ScUiRect row={box.x+middle-half,box.y+y,half*2+1,1};
                ui_fill(p,&row,SC32_COLOR_LINE);
                if(half>1){row.x++;
                    row.width-=2;
                    ui_fill(p,&row,SC32_COLOR_PAPER);
                }
            }
            if(n->effective&SC_UI_CHECKED){ScUiRect dot={box.x+middle-2,box.y+middle-2,5,5};
                ui_fill(p,&dot,(n->effective&SC_UI_ENABLED)?SC32_COLOR_TEXT:SC32_COLOR_MUTED);
            }
        }
        else{ui_fill(p,&box,SC32_COLOR_PAPER);
            ui_border(p,&box,1);
            if(n->effective&SC_UI_MIXED){ScUiRect mixed={box.x+3,box.y+side/2-1,side-6,3};
                ui_fill(p,&mixed,SC32_COLOR_MUTED);
            }
            else if(n->effective&SC_UI_CHECKED){ScUiPainterLine(p,box.x+3,box.y+side/2,box.x+side/2,box.y+side-4,SC32_COLOR_TEXT);
                ScUiPainterLine(p,box.x+side/2,box.y+side-4,box.x+side-3,box.y+3,SC32_COLOR_TEXT);
            }
        }
        text.x=side+pad*2;
        text.width-=text.x;
        ScStatus s=ui_control_label(n,p,&text,UI_TEXT_ELLIPSIS);
        if(n->effective&SC_UI_FOCUSED)ui_control_focus(p,&text);
        return s;
    }
    if(n->kind==SC_UI_COMBOBOX){ui_control_surface(n,p,&r,SC32_COLOR_PAPER,1);
        ScI32 side=r.height-2;
        ScUiRect arrow={r.width-side-1,1,side,r.height-2};
        ui_fill(p,&arrow,SC32_COLOR_FACE);
        ui_border(p,&arrow,(n->state&SC_UI_EXPANDED)!=0);
        ui_control_arrow(p,arrow.x+side/2-3,arrow.y+side/2-1,4,0,SC32_COLOR_TEXT);
        text.x=pad;
        text.width=r.width-side-pad*2;
        text.y=pad/2;
        text.height-=pad;
        return ui_control_label(n,p,&text,UI_TEXT_ELLIPSIS);
    }
    if(n->kind==SC_UI_TAB && c){
        ui_fill(p,&r,SC32_COLOR_FACE);
        ScI32 x=0,height=ui_scale(n->owner,26);
        for(UiTabItem *tab=c->tabs;tab;tab=tab->next){ScUiText title;
            ScUiTextMetrics metrics;
            ui_text_view(tab->title,&title);
            memset(&metrics,0,sizeof(metrics));
            metrics.size=sizeof(metrics);
            metrics.version=1;
            ScStatus measured=ui_text_measure(n->owner,n->font_role,&title,SC_UI_AUTO,0,&metrics);
            if(measured<0)return measured;
            if(metrics.width>(ScU32)(INT_MAX-pad*4) || x>INT_MAX-(ScI32)metrics.width-pad*4)return SC_UI_E_RANGE;
            tab->x=x;
            tab->width=(ScI32)metrics.width+pad*4;
            x+=tab->width;
            }
        n->content_width.lo=(ScU32)x;
        n->content_width.hi=0;
        for(UiTabItem *tab=c->tabs;tab;tab=tab->next)if(tab->page==c->selected_page){
            if((ScU32)tab->x<n->scroll_x.lo)n->scroll_x.lo=(ScU32)tab->x;
            else if(tab->x+tab->width>(ScI32)n->scroll_x.lo+r.width)n->scroll_x.lo=(ScU32)(tab->x+tab->width-r.width);
            }
        n->scroll_x.hi=0;
        for(UiTabItem *tab=c->tabs;tab;tab=tab->next){ScUiText title;
            ui_text_view(tab->title,&title);
            ScI32 width=tab->width;
            ScUiRect cap={tab->x-(ScI32)n->scroll_x.lo,0,width,height};
            ui_fill(p,&cap,tab->page==c->selected_page?SC32_COLOR_PAPER:SC32_COLOR_FACE_ALT);
            ui_border(p,&cap,0);
            cap.x+=pad;
            cap.width-=pad*2;
            cap.y+=pad;
            cap.height-=pad;
            p->text_role=SC32_COLOR_TEXT;
            ScStatus s=ScUiPainterText(p,&cap,&title,n->font_role,UI_TEXT_ELLIPSIS);
            if(s<0)return s;
        }
        n->content_width.lo=(ScU32)x;
        n->content_width.hi=0;
        return p->error;
    }
    if(n->kind==SC_UI_PROGRESS && c){ui_control_surface(n,p,&r,SC32_COLOR_PAPER,1);
        ScUiRect fill={2,2,r.width-4,r.height-4};
        if(fill.width>0){if(c->progress_mode==SC_UI_PROGRESS_INDETERMINATE){ScI32 width=fill.width/4;
                if(width<4)width=4;
                fill.x+=c->animation%(ScU32)fill.width;
                fill.width=width;
            }
            else fill.width=(ScI32)ui_control_fraction(c->progress,(ScU32)fill.width,c->total);
            if(fill.x+fill.width>r.width-2)fill.width=r.width-2-fill.x;
            ScI32 radius=ui_control_radius(n,&r);
            if(radius>2)radius-=2;
            else radius=0;
            if(radius>fill.width/2)radius=fill.width/2;
            if(radius>fill.height/2)radius=fill.height/2;
            if(fill.width>0 && fill.height>0)ui_control_round_fill(p,&fill,radius,SC32_COLOR_ACCENT);
        }
        return p->error;
    }
    if(n->kind==SC_UI_STATUS && c){ui_fill(p,&r,SC32_COLOR_FACE);
        ScI32 x=0;
        for(ScU32 i=0;i<c->part_count;i++){UiStatusItem *part=c->parts+i;
            ScI32 width=part->description.width<0?r.width-x:ui_scale(n->owner,part->description.width);
            if(width<0)width=0;
            ScUiRect area={x,1,width,r.height-2};
            ui_border(p,&area,1);
            area.x+=pad;
            area.width-=pad*2;
            if(part->description.icon){ScI32 side=ui_scale(n->owner,16);
                ScUiRect icon={area.x,area.y+(area.height-side)/2,side,side};
                ScUiPainterIcon(p,&icon,part->description.icon,n->effective);
                area.x+=side+pad;
                area.width-=side+pad;
            }
            ScUiText part_text;
            ui_text_view(part->text,&part_text);
            p->text_role=SC32_COLOR_TEXT;
            ScStatus s=ScUiPainterText(p,&area,&part_text,n->font_role,UI_TEXT_ELLIPSIS);
            if(s<0)return s;
            if(x>INT_MAX-width)return SC_UI_E_RANGE;
            x+=width;
        }
        return p->error;
    }
    if(n->kind==SC_UI_SCROLL && c){ui_fill(p,&r,SC32_COLOR_FACE_ALT);
        int vertical=r.height>=r.width;
        ScI32 side=vertical?r.width:r.height;
        ScUiRect first={0,0,side,side},last={0,0,side,side};
        if(vertical)last.y=r.height-side;
        else last.x=r.width-side;
        ui_border(p,&first,0);
        ui_border(p,&last,0);
        ui_control_arrow(p,first.x+side/2-3,first.y+side/2-3,4,vertical?1:2,SC32_COLOR_TEXT);
        ui_control_arrow(p,last.x+side/2-3,last.y+side/2-3,4,vertical?0:3,SC32_COLOR_TEXT);
        UiNode *target=n->parent;
        if(target){ScUiRect view;ScStatus measured=ui_scroll_viewport(target,&view);if(measured<0)return measured;
            ScU32 content=vertical?target->content_height.lo:target->content_width.lo,viewport=vertical?(ScU32)view.height:(ScU32)view.width;
            ScI32 track=(vertical?r.height:r.width)-side*2;
            if(track<0)track=0;
            ScI32 thumb=content>viewport && content?((ScI32)ui_control_fraction(viewport,(ScU32)track,content)):track;
            if(thumb<side)thumb=side;
            if(thumb>track)thumb=track;
            ScU32 offset=vertical?target->scroll_y.lo:target->scroll_x.lo;
            ScI32 at=content>viewport?(ScI32)ui_control_fraction(offset,(ScU32)(track-thumb),content-viewport):0;
            ScUiRect grip={1,1,thumb,thumb};
            if(vertical){grip.y=side+at;
                grip.width=r.width-2;
            }
            else{grip.x=side+at;
                grip.height=r.height-2;
            }
            ui_fill(p,&grip,SC32_COLOR_FACE);
            ui_border(p,&grip,0);
        }
        return p->error;
    }
    if(n->kind==SC_UI_SPLITTER){ui_fill(p,&r,SC32_COLOR_FACE_ALT);
        ScUiRect line={r.width/2,0,1,r.height};
        ui_fill(p,&line,SC32_COLOR_LINE);
        return p->error;
    }
    if(n->kind==SC_UI_GROUPBOX){ScUiRect box={0,ui_scale(n->owner,8),r.width,r.height-ui_scale(n->owner,8)};
        ui_border(p,&box,1);
        ScUiText caption;
        ScUiTextMetrics m;
        ui_text_view(n->text,&caption);
        memset(&m,0,sizeof(m));
        m.size=sizeof(m);
        m.version=1;
        ScStatus s=ui_text_measure(n->owner,n->font_role,&caption,SC_UI_AUTO,0,&m);
        if(s<0)return s;
        text.x=pad*2;
        text.width=(ScI32)m.width+pad*2;
        text.height=(ScI32)m.height;
        ui_fill(p,&text,SC32_COLOR_FACE);
        text.x+=pad;
        text.width-=pad*2;
        return ui_control_label(n,p,&text,0);
    }
    if(n->kind==SC_UI_LABEL || n->kind==SC_UI_TOOLTIP){if(n->kind==SC_UI_TOOLTIP){ui_control_surface(n,p,&r,SC32_COLOR_FACE_ALT,0);
            text.x=pad;
            text.y=pad;
            text.width-=pad*2;
            text.height-=pad*2;
        }
        return ui_control_label(n,p,&text,UI_TEXT_WRAP);
    }
    ui_fill(p,&r,SC32_COLOR_FACE);
    return p->error;
}

ScStatus ScUiButtonSetDefault(ScUiContext *context,ScUiNode root_id,ScUiNode id)
{
    UiNode *root=ui_root(context,root_id),*button=id?ui_node(context,id):0;
    if(!root || !root->window || (id && !button))return SC_UI_E_STALE;
    if(button && (button->kind!=SC_UI_BUTTON || button->root!=root->id))return SC_UI_E_INVALID;
    UiNode *old=ui_node(context,root->window->default_button);
    root->window->default_button=id;
    if(old)ui_node_damage(old);
    if(button)ui_node_damage(button);
    return 0;
}
ScStatus ScUiLabelAssociate(ScUiContext *context,ScUiNode label,ScUiNode target)
{
    UiNode *n=ui_node(context,label),*to=ui_node(context,target);
    if(!n || !to)return SC_UI_E_STALE;
    if(n->kind!=SC_UI_LABEL || n->root!=to->root || !(to->flags&SC_UI_FOCUSABLE))return SC_UI_E_INVALID;
    n->label_target=target;
    return 0;
}
ScStatus ScUiCheckSet(ScUiContext *context,ScUiNode id,ScU32 checked)
{
    UiNode *n=ui_node(context,id);
    if(!n)return SC_UI_E_STALE;
    if(n->kind!=SC_UI_CHECKBOX || checked>2)return SC_UI_E_INVALID;
    if(n->state&SC_UI_READONLY)return SC_UI_E_CANCELLED;
    ScValue value={sizeof(value),1,SC_VALUE_U32,0,checked,0,0,0};
    return ScUiNodeSetValue(context,id,&value);
}
ScStatus ScUiRadioSelect(ScUiContext *context,ScUiNode group_id,ScUiNode id)
{
    UiNode *group=ui_node(context,group_id),*radio=ui_node(context,id);
    ScStatus s;
    ScUiEvent event;
    if(!group || !radio)return SC_UI_E_STALE;
    if(radio->kind!=SC_UI_RADIO || radio->parent!=group)return SC_UI_E_INVALID;
    if(radio->state&SC_UI_READONLY)return SC_UI_E_CANCELLED;
    if(context->revision==UINT_MAX)return SC_UI_E_RANGE;
    ui_node_hold(group);
    ui_node_hold(radio);
    ui_event_record(&event,radio,SC_UI_EVENT_VALUE_CHANGING);
    event.flags=SC_UI_EVENT_CANCELABLE|SC_UI_EVENT_PROGRAMMATIC;
    ScValue value={sizeof(value),1,SC_VALUE_BOOL,0,1,0,0,0};
    event.data=&value;
    event.bytes=sizeof(value);
    ScU32 revision=context->revision;
    s=ui_emit(context,radio,&event);
    if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
    else if(s>0)s=0;
    if(s==0 && ((group->private_flags|radio->private_flags)&UI_NODE_RETIRED || context->revision!=revision))s=SC_UI_E_STALE;
    if(s==0){
        for(UiNode *n=group->first_child;n;n=n->next)if(n->kind==SC_UI_RADIO){
            n->state=(n->state&~SC_UI_CHECKED)|(n==radio?SC_UI_CHECKED:0);
            n->value.value=value;
            n->value.value.lo=n==radio;
            if(n->value_revision<UINT_MAX)n->value_revision++;
            ui_tree_effective(n);
            ui_node_damage(n);
        }
        context->revision++;
        ScStatus notice=ui_event(radio,SC_UI_EVENT_VALUE_CHANGED,&value,sizeof(value));
        if(notice<0)context->notification_error=notice;
    }
    ui_node_leave(radio);
    ui_node_leave(group);
    return s;
}
ScStatus ScUiSpinRange(ScUiContext *context,ScUiNode id,ScI32 minimum,ScI32 maximum,ScI32 step)
{
    UiNode *n=ui_node(context,id);
    UiControl *c=ui_control(n);
    ScStatus s;
    if(!n)return SC_UI_E_STALE;
    if(n->kind!=SC_UI_SPINBOX || minimum>maximum || step<=0)return SC_UI_E_INVALID;
    ScI32 number=n->value.value.type==SC_VALUE_I32?(ScI32)n->value.value.lo:0;
    if(number<minimum)number=minimum;
    if(number>maximum)number=maximum;
    ScI32 old_min=c->minimum,old_max=c->maximum,old_step=c->step;
    c->minimum=minimum;
    c->maximum=maximum;
    c->step=step;
    ScValue value={sizeof(value),1,SC_VALUE_I32,0,(ScU32)number,0,0,0};
    ui_node_hold(n);
    s=ScUiNodeSetValue(context,id,&value);
    if(s<0 && !(n->private_flags&UI_NODE_RETIRED)){c->minimum=old_min;
        c->maximum=old_max;
        c->step=old_step;
    }
    ui_node_leave(n);
    return s;
}
static ScStatus ui_control_spin(UiNode *n,int direction)
{
    if(n->state&SC_UI_READONLY)return SC_UI_E_CANCELLED;
    UiControl *c=ui_control(n);
    ScI32 old=n->value.value.type==SC_VALUE_I32?(ScI32)n->value.value.lo:0,next=old;
    if(direction>0){if(old>INT_MAX-c->step)next=c->maximum;
        else next=old+c->step;
    }
    else{if(old<INT_MIN+c->step)next=c->minimum;
        else next=old-c->step;
    }
    if(next<c->minimum)next=c->minimum;
    if(next>c->maximum)next=c->maximum;
    ScValue value={sizeof(value),1,SC_VALUE_I32,0,(ScU32)next,0,0,0};
    return ScUiNodeSetValue(n->owner,n->id,&value);
}
ScStatus ScUiTabAdd(ScUiContext *context,ScUiNode tabs_id,ScUiNode page_id,const ScUiText *title)
{
    UiNode *tabs=ui_node(context,tabs_id),*page=ui_node(context,page_id);
    UiControl *c=ui_control(tabs);
    ScStatus s;
    if(!tabs || !page)return SC_UI_E_STALE;
    if(tabs->kind!=SC_UI_TAB || page->window || page==tabs)return SC_UI_E_INVALID;
    for(UiTabItem *item=c->tabs;item;item=item->next)if(item->page==page_id)return SC_UI_E_EXISTS;
    UiTabItem *candidate=(UiTabItem *)calloc(1,sizeof(*candidate));
    if(!candidate)return SC_UI_E_MEMORY;
    s=ui_text_copy(title,&candidate->title);
    if(s==0 && page->parent!=tabs)s=ScUiNodeMove(context,page_id,tabs_id,0);
    if(s<0){ScStringRelease(candidate->title);
        free(candidate);
        return s;
    }
    candidate->page=page_id;
    UiTabItem **slot=&c->tabs;
    while(*slot)slot=&(*slot)->next;
    *slot=candidate;
    if(!c->selected_page){ScValue value={sizeof(ScValue),1,SC_VALUE_U32,0,page_id,0,0,0};
        c->selected_page=page_id;
        tabs->value.value=value;
    }
    if(c->selected_page!=page_id)page->state&=~SC_UI_VISIBLE;
    else page->state|=SC_UI_VISIBLE;
    ui_tree_effective(page);
    ui_node_layout_dirty(tabs);
    return 0;
}
ScStatus ScUiTabSelect(ScUiContext *context,ScUiNode id,ScUiNode page)
{
    UiNode *tabs=ui_node(context,id);
    UiControl *c=ui_control(tabs);
    ScStatus s;
    UiTabItem *item;
    if(!tabs)return SC_UI_E_STALE;
    if(tabs->kind!=SC_UI_TAB)return SC_UI_E_INVALID;
    for(item=c->tabs;item && item->page!=page;item=item->next){}
    if(!item || !ui_node(context,page))return SC_UI_E_NOT_FOUND;
    if(c->selected_page==page)return 0;
    ui_node_hold(tabs);
    ScUiEvent event;
    ui_event_record(&event,tabs,SC_UI_EVENT_SELECTION_CHANGING);
    event.flags=SC_UI_EVENT_CANCELABLE|SC_UI_EVENT_PROGRAMMATIC;
    event.data=&page;
    event.bytes=sizeof(page);
    ScU32 old=c->selected_page;
    s=ui_emit(context,tabs,&event);
    if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
    else if(s>0)s=0;
    if(s==0 && (tabs->private_flags&UI_NODE_RETIRED || c->selected_page!=old))s=SC_UI_E_STALE;
    if(s==0){ScValue value={sizeof(ScValue),1,SC_VALUE_U32,0,page,0,0,0};
        c->selected_page=page;
        tabs->value.value=value;
        for(item=c->tabs;item;item=item->next){UiNode *n=ui_node(context,item->page);
            if(!n)continue;
            n->state=(n->state&~SC_UI_VISIBLE)|(item->page==page?SC_UI_VISIBLE:0);
            ui_tree_effective(n);
        }
        ui_node_layout_dirty(tabs);
        ScStatus notice=ui_event(tabs,SC_UI_EVENT_SELECTION_CHANGED,&page,sizeof(page));
        if(notice<0)context->notification_error=notice;
    }
    ui_node_leave(tabs);
    return s;
}
static ScStatus ui_combo_selected(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    UiNode *combo=ui_node(context,(ScU32)opaque);
    if(!combo)return 0;
    UiControl *c=ui_control(combo);
    ScStatus s=0;
    if(combo->private_flags&UI_NODE_RETIRED)return 0;
    ui_node_hold(combo);
    if(event->type==SC_UI_EVENT_SELECTION_CHANGED && combo->view){
        ScValue value;
        memset(&value,0,sizeof(value));
        value.size=sizeof(value);
        value.version=1;
        s=ui_provider_cell(combo->view->provider,combo->view->selection->current,0,&value);
        if(s==0 && value.type==SC_VALUE_UTF8){ScUiText text={sizeof(text),1,(const char *)value.data,value.bytes};
            s=ScUiNodeSetText(context,combo->id,&text);
        }
        if(s<0)context->notification_error=s;
    }
    if(combo->private_flags&UI_NODE_RETIRED){ui_node_leave(combo);
        return 0;
        }
    if(event->type==SC_UI_EVENT_KEY_DOWN && (event->key==27 || event->key=='\n')){ui_control_popup_close(combo);
        ScUiFocus(context,combo->id);
        ui_node_leave(combo);
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_UP && ui_control_left_edge(event) && c && c->popup){ui_control_popup_close(combo);
        ScUiFocus(context,combo->id);
        ui_node_leave(combo);
        return SC_UI_HANDLED;
    }
    ui_node_leave(combo);
    return 0;
}
ScStatus ScUiComboOpen(ScUiContext *context,ScUiNode id,ScU32 open)
{
    UiNode *n=ui_node(context,id),*popup=0;
    UiControl *c=ui_control(n);
    ScStatus s;
    ScU32 token;
    ScUiNode list;
    ScUiProvider *provider=0;
    ScUiSelection *selection=0;
    if(!n)return SC_UI_E_STALE;
    if(n->kind!=SC_UI_COMBOBOX || open>1)return SC_UI_E_INVALID;
    if(open && (n->state&SC_UI_READONLY))return SC_UI_E_CANCELLED;
    if(!open){ui_control_popup_close(n);
        return 0;
    }
    if(c->popup){if(ui_node(context,c->popup))return 0;
        ui_control_popup_close(n);
    }
    if(!n->view || !n->view->provider)return SC_UI_E_NOT_FOUND;
    ui_node_hold(n);
    UiView *view=n->view;
    provider=view->provider;
    selection=view->selection;
    s=ScUiProviderRetain(provider);
    if(s<0){provider=0;selection=0;goto done;}
    if(selection){s=ScUiSelectionRetain(selection);
        if(s<0){selection=0;goto done;}
    }
    ScU32 count=0,revision=0;
    s=ui_provider_count(provider,0,&count,&revision);
    if(s!=0)goto done;
    if((n->private_flags&UI_NODE_RETIRED) || n->view!=view || view->provider!=provider || view->selection!=selection || c->popup){s=SC_UI_E_STALE;goto done;}
    if(!count){s=SC_UI_E_NOT_FOUND;goto done;}
    ScUiMetrics metrics;
    ScDisplayInfo display;
    memset(&metrics,0,sizeof(metrics));
    metrics.size=sizeof(metrics);
    metrics.version=1;
    s=ScUiMetricsQuery(context,n->root,&metrics);
    if(s!=0)goto done;
    memset(&display,0,sizeof(display));
    display.size=sizeof(display);
    display.version=1;
    s=ScDisplayQuery(&display);
    if(s!=0)goto done;
    /* 行高已经是当前字体的物理像素；先截到真实屏幕预算再乘，
     * 十万项仍只生成可见行，不让少数项留下固定180逻辑像素的空白。 */
    ScU32 budget=display.height/2;
    if(!metrics.row_height || budget<metrics.row_height || n->rectangle.width<=0){s=SC_UI_E_RANGE;goto done;}
    ScU32 rows=budget/metrics.row_height;
    if(rows>count)rows=count;
    ScU32 height=rows*metrics.row_height;
    ScUiRect anchor={0,0,n->rectangle.width,n->rectangle.height};
    s=ui_popup_root(n,SC32_POPUP_COMBO,&anchor,(ScU32)n->rectangle.width,height,SC32_POPUP_NO_ACTIVATE,&popup);
    if(s<0)goto done;
    ui_node_hold(popup);
    if((n->private_flags&UI_NODE_RETIRED) || n->view!=view){s=SC_UI_E_STALE;goto rollback;}
    /* 原生popup的客户矩形是物理像素；Overlay填充它，避免把
     * 同一宽高再作为子控件逻辑尺寸按DPI缩放第二次。 */
    popup->layout.mode=SC_UI_OVERLAY;
    ScUiWidgetDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.kind=SC_UI_LIST;
    d.parent=popup->id;
    d.text.size=sizeof(d.text);
    d.text.version=1;
    s=ScUiWidgetCreate(context,&d,&list);
    if(s==0)s=ScUiViewBind(context,list,provider,selection);
    if(s==0){UiNode *child=ui_node(context,list);
        child->layout.width=SC_UI_AUTO;
        child->layout.height=SC_UI_AUTO;
        child->layout.mode=SC_UI_ABSOLUTE;
        ScUiSubscription sub;
        memset(&sub,0,sizeof(sub));
        sub.size=sizeof(sub);
        sub.version=1;
        sub.phases=SC_UI_TARGET_PHASE;
        sub.procedure=ui_combo_selected;
        sub.context=(void *)n->id;
        s=ScUiListen(context,list,&sub,&token);
    }
    if(s==0 && !(n->private_flags&UI_NODE_RETIRED)){c->popup=popup->id;
        c->popup_next=ui_control_popups;
        if(ui_control_popups)ui_control_popups->popup_previous=c;
        ui_control_popups=c;
        n->state|=SC_UI_EXPANDED;
        ui_tree_effective(n);
        ui_node_damage(n);
        s=ScUiLayout(context,popup->id);
        if(s==0)s=ScUiFocus(context,list);
    }
    rollback:if(s<0 || n->private_flags&UI_NODE_RETIRED){if(c->popup==popup->id)ui_control_popup_close(n);
        else ScUiNodeDestroy(context,popup->id);
        if(s==0)s=SC_UI_E_CANCELLED;
    }
    done:s=ui_cause(context,s,UI_OP_NODE,id);
    if(selection)ScUiSelectionRelease(selection);
    if(provider)ScUiProviderRelease(provider);
    if(popup)ui_node_leave(popup);
    ui_node_leave(n);
    return s;
}
ScStatus ScUiProgressSet(ScUiContext *context,ScUiNode id,ScU32 value,ScU32 total,ScU32 mode)
{
    UiNode *n=ui_node(context,id);
    UiControl *c=ui_control(n);
    ScStatus s=0;
    if(!n)return SC_UI_E_STALE;
    if(n->kind!=SC_UI_PROGRESS || mode>1 || !total || value>total)return SC_UI_E_INVALID;
    if(mode==SC_UI_PROGRESS_INDETERMINATE){s=ui_control_timer(n,5,5,&c->timer);
        if(s<0)return s;
    }
    else ui_control_timer_cancel(n,&c->timer);
    c->progress=value;
    c->total=total;
    c->progress_mode=mode;
    ui_node_damage(n);
    return 0;
}
ScStatus ScUiStatusSetParts(ScUiContext *context,ScUiNode id,const ScUiStatusPart *parts,ScU32 count)
{
    UiNode *n=ui_node(context,id);
    UiControl *c=ui_control(n);
    UiStatusItem *candidate=0;
    ScStatus s=0;
    ScU32 bytes;
    if(!n)return SC_UI_E_STALE;
    if(n->kind!=SC_UI_STATUS || (!parts && count))return SC_UI_E_INVALID;
    if(!ui_multiply(count,sizeof(*candidate),&bytes))return SC_UI_E_RANGE;
    if(bytes){candidate=(UiStatusItem *)calloc(1,bytes);
        if(!candidate)return SC_UI_E_MEMORY;
    }
    for(ScU32 i=0;i<count;i++){
        s=ui_input(parts+i,sizeof(*parts));
        if(s<0 || parts[i].width<-1 || parts[i].flags || !ui_data_reserved(parts[i].reserved,2)){s=s<0?s:SC_UI_E_INVALID;
            break;
        }
        candidate[i].description=parts[i];
        candidate[i].description.icon=0;
        s=ui_text_copy(&parts[i].text,&candidate[i].text);
        if(s<0)break;
        if(parts[i].icon){s=ScUiIconRetain(parts[i].icon);
            if(s<0)break;
            candidate[i].description.icon=parts[i].icon;
        }
    }
    if(s==0){UiStatusItem *old=c->parts;
        ScU32 old_count=c->part_count;
        c->parts=candidate;
        c->part_count=count;
        candidate=old;
        count=old_count;
        ui_node_damage(n);
    }
    for(ScU32 i=0;i<count;i++){ScStringRelease(candidate[i].text);
        ScUiIconRelease(candidate[i].description.icon);
    }
    free(candidate);
    return s;
}
ScStatus ScUiTooltipSet(ScUiContext *context,ScUiNode id,const ScUiText *text)
{
    UiNode *n=ui_node(context,id);
    ScString *candidate=0;
    ScStatus s;
    if(!n)return SC_UI_E_STALE;
    s=ui_text_copy(text,&candidate);
    if(s<0)return s;
    ScStringRelease(n->tooltip);
    n->tooltip=candidate;
    UiControl *c=ui_control(n);
    if(c){ui_control_timer_cancel(n,&c->tooltip_timer);
        if(n->kind!=SC_UI_COMBOBOX)ui_control_popup_close(n);
    }
    return 0;
}
ScStatus ScUiSplitterSet(ScUiContext *context,ScUiNode id,ScI32 position)
{
    UiNode *n=ui_node(context,id);
    UiControl *c=ui_control(n);
    UiNode *a,*b;
    ScStatus s;
    ScUiEvent event;
    if(!n)return SC_UI_E_STALE;
    if(n->kind!=SC_UI_SPLITTER || position<0 || !n->parent)return SC_UI_E_INVALID;
    a=n->previous;
    b=n->next;
    if(!a || !b)return SC_UI_E_NOT_FOUND;
    int vertical=n->parent->layout.mode!=SC_UI_VBOX;
    ScI32 total=vertical?a->rectangle.width+b->rectangle.width:a->rectangle.height+b->rectangle.height;
    ScI32 min_a=ui_scale(context,vertical?a->layout.min_width:a->layout.min_height),min_b=ui_scale(context,vertical?b->layout.min_width:b->layout.min_height);
    if(position<min_a)position=min_a;
    if(position>total-min_b)position=total-min_b;
    if(position<0)return SC_UI_E_RANGE;
    ScU32 revision=context->revision;
    ui_node_hold(n);
    ui_node_hold(a);
    ui_node_hold(b);
    ui_event_record(&event,n,SC_UI_EVENT_VALUE_CHANGING);
    event.flags=SC_UI_EVENT_CANCELABLE|SC_UI_EVENT_PROGRAMMATIC;
    event.data=&position;
    event.bytes=sizeof(position);
    s=ui_emit(context,n,&event);
    if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
    else if(s>0)s=0;
    if(s==0 && ((n->private_flags|a->private_flags|b->private_flags)&UI_NODE_RETIRED || context->revision!=revision || n->previous!=a || n->next!=b))s=SC_UI_E_STALE;
    if(s==0){ScI32 logical=(ScI32)ui_control_fraction((ScU32)position,100,(ScU32)ui_scale(context,100));
        if(vertical){a->layout.width=logical;
            b->layout.width=SC_UI_AUTO;
        }
        else{a->layout.height=logical;
            b->layout.height=SC_UI_AUTO;
        }
        a->layout.flex_weight=0;
        b->layout.flex_weight=1;
        c->split_position=position;
        ui_node_layout_dirty(n->parent);
        ScStatus notice=ui_event(n,SC_UI_EVENT_VALUE_CHANGED,&position,sizeof(position));
        if(notice<0)context->notification_error=notice;
    }
    ui_node_leave(b);
    ui_node_leave(a);
    ui_node_leave(n);
    return s;
}

static ScStatus ui_control_tooltip(UiNode *n)
{
    UiControl *c=ui_control(n);
    ScUiText text;
    ScUiTextMetrics m;
    UiNode *popup=0;
    ScUiNode label;
    ScStatus s;
    if(!n->tooltip || !(n->effective&SC_UI_HOVER) || (n->private_flags&UI_NODE_RETIRED))return 0;
    /* hover属于上次已分发的消息；鼠标经过非聚焦窗后可直接离开客户区，
     * 不一定再有LEAVE。延迟提示必须重新核实当前输入和命中，不能拿旧
     * hover向内核申请后台popup，把正常失焦变成应用ERROR。 */
    UiNode *native=ui_native_owner(ui_root(n->owner,n->id));
    ScInputSnapshot input;memset(&input,0,sizeof(input));input.size=sizeof(input);input.version=1;
    if(!native || !native->window)return 0;
    s=ScInputQuery(native->window->window,&input);if(s<0)return s;
    if(!input.focused || !input.visible || input.captured || input.buttons)return 0;
    ScUiNode hit=0;
    if(ScUiHitTest(n->owner,native->id,input.x,input.y,&hit)<0 || hit!=n->id)return 0;
    ui_text_view(n->tooltip,&text);
    if(!text.bytes)return 0;
    memset(&m,0,sizeof(m));
    m.size=sizeof(m);
    m.version=1;
    s=ui_text_measure(n->owner,SC_UI_FONT_BODY,&text,ui_scale(n->owner,320),UI_TEXT_WRAP,&m);
    if(s<0)return s;
    ScUiRect anchor={0,0,n->rectangle.width,n->rectangle.height};
    s=ui_popup_root(n,SC32_POPUP_TOOLTIP,&anchor,m.width+(ScU32)ui_scale(n->owner,8),m.height+(ScU32)ui_scale(n->owner,8),SC32_POPUP_NO_ACTIVATE|SC32_POPUP_HIT_TRANSPARENT,&popup);
    if(s==SC_UI_E_CANCELLED)return 0;
    if(s<0)return s;
    ScUiWidgetDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.kind=SC_UI_TOOLTIP;
    d.parent=popup->id;
    d.text=text;
    s=ScUiWidgetCreate(n->owner,&d,&label);
    if(s==0){UiNode *child=ui_node(n->owner,label);
        child->layout.width=SC_UI_AUTO;
        child->layout.height=SC_UI_AUTO;
        child->layout.flex_weight=1;
        popup->layout.mode=SC_UI_OVERLAY;
        s=ScUiLayout(n->owner,popup->id);
    }
    if(s<0)ScUiNodeDestroy(n->owner,popup->id);
    else c->popup=popup->id;
    return s;
}
static ScStatus ui_control_invoke(UiNode *n)
{
    if(n->kind==SC_UI_CHECKBOX){ScU32 value=(n->state&SC_UI_MIXED)?2u:(n->state&SC_UI_CHECKED)?1u:0u;
        return ScUiCheckSet(n->owner,n->id,(value+1)%((n->flags&SC_UI_THREE_STATE)?3u:2u));
    }
    if(n->kind==SC_UI_RADIO)return ScUiRadioSelect(n->owner,n->parent?n->parent->id:0,n->id);
    if(n->kind==SC_UI_LABEL && n->label_target)return ScUiFocus(n->owner,n->label_target);
    return n->command?ScUiCommand(n->owner,n->id,n->command):0;
}
static ScStatus ui_control_scroll_event(UiNode *n,const ScUiEvent *e)
{
    UiNode *target=n->parent;
    UiControl *c=ui_control(n);
    if(!target)return SC_UI_E_NOT_FOUND;
    int vertical=n->rectangle.height>=n->rectangle.width;
    ScI32 side=vertical?n->rectangle.width:n->rectangle.height;
    ScI32 coordinate=vertical?e->y:e->x,length=vertical?n->rectangle.height:n->rectangle.width;
    ScWideCounter x=target->scroll_x,y=target->scroll_y,*offset=vertical?&y:&x;
    ScU32 step=vertical?target->scroll_step_y:target->scroll_step_x,content=vertical?target->content_height.lo:target->content_width.lo;
    ScUiRect view;ScStatus measured=ui_scroll_viewport(target,&view);if(measured<0)return measured;
    ScU32 viewport=vertical?(ScU32)view.height:(ScU32)view.width,max=content>viewport?content-viewport:0;
    if(e->type==SC_UI_EVENT_POINTER_DOWN && ui_control_left_edge(e) && (e->buttons&1)){
        if(coordinate<side){if(!offset->hi && offset->lo<step)offset->lo=0;
            else{ScU32 old=offset->lo;
                offset->lo-=step;
                offset->hi-=old<step;
            }
        }
        else if(coordinate>=length-side)ui_wide_add(offset,step);
        else{c->pressed_part=1;
            c->drag_origin=coordinate;
            c->drag_value=(ScI32)offset->lo;
            ScUiCapture(n->owner,n->id,1);
        }
        ScUiFocus(n->owner,n->id);
        return ScUiScrollTo(n->owner,target->id,&x,&y)<0?SC_UI_E_FAILED:SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_POINTER_MOVE && c->pressed_part){
        ScI32 track=length-side*2;
        if(ui_scroll_layout_node(target))track=ui_scroll_bar_travel(length,side,content,viewport);
        ScI32 delta=coordinate-c->drag_origin;
        ScU32 magnitude=delta<0?0u-(ScU32)delta:(ScU32)delta;
        ScU32 change=ui_scroll_layout_node(target) && track<=0?0:ui_control_fraction(magnitude,max,(ScU32)(track>0?track:1));
        ScU32 old=(ScU32)c->drag_value;
        if(delta<0)offset->lo=change>old?0:old-change;
        else offset->lo=change>max-old?max:old+change;
        offset->hi=0;
        ScStatus s=ScUiScrollTo(n->owner,target->id,&x,&y);
        return s<0?s:SC_UI_HANDLED;
    }
    if((e->type==SC_UI_EVENT_POINTER_UP && ui_control_left_edge(e)) || e->type==SC_UI_EVENT_CAPTURE_LOST || e->type==SC_UI_EVENT_INPUT_SYNC){c->pressed_part=0;
        if(e->type==SC_UI_EVENT_POINTER_UP)ScUiCapture(n->owner,n->id,0);
        return SC_UI_HANDLED;
    }
    return 0;
}
static ScStatus ui_controls_event(UiNode *n,const ScUiEvent *e)
{
    UiControl *c=ui_control(n);
    ScStatus s=0;
    if(!c)return 0;
    if(e->type==SC_UI_EVENT_POINTER_ENTER && n->tooltip && n->kind!=SC_UI_COMBOBOX)ui_control_timer(n,50,0,&c->tooltip_timer);
    if(e->type==SC_UI_EVENT_POINTER_LEAVE || e->type==SC_UI_EVENT_FOCUS_OUT || e->type==SC_UI_EVENT_INPUT_SYNC){
        ui_control_timer_cancel(n,&c->tooltip_timer);
        if(n->kind!=SC_UI_COMBOBOX)ui_control_popup_close(n);
    }
    if(e->type==SC_UI_EVENT_TIMER){
        if(e->command==c->tooltip_timer){c->tooltip_timer=0;
            return ui_control_tooltip(n);
        }
        if(e->command==c->timer && n->kind==SC_UI_PROGRESS){c->animation+=(ScU32)ui_scale(n->owner,4);
            ui_node_damage(n);
            return SC_UI_HANDLED;
        }
    }
    if(n->kind==SC_UI_SPINBOX){
        if(e->type==SC_UI_EVENT_KEY_DOWN && (e->key==SC32_KEY_UP || e->key==SC32_KEY_DOWN)){s=ui_control_spin(n,e->key==SC32_KEY_UP?1:-1);
            return s<0?s:SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_POINTER_DOWN && ui_control_left_edge(e) && e->x>=n->rectangle.width-ui_scale(n->owner,18) && (e->buttons&1)){s=ui_control_spin(n,e->y<n->rectangle.height/2?1:-1);
            return s<0?s:SC_UI_HANDLED;
        }
    }
    if(n->kind==SC_UI_SECRET)return ui_secret_event(n,e);
    if(ui_control_editor(n))return ui_edit_event(n,e);
    if(n->kind==SC_UI_COMBOBOX){
        if(e->type==SC_UI_EVENT_POINTER_DOWN && ui_control_left_edge(e) && (e->buttons&1)){s=ScUiComboOpen(n->owner,n->id,c->popup?0:1);
            return s<0?s:SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_KEY_DOWN && ((e->key==SC32_KEY_DOWN && (e->modifiers&SC32_MOD_ALT)) || e->key==' ' || e->key==SC32_KEY_F1+3)){
            s=ScUiComboOpen(n->owner,n->id,c->popup?0:1);
            return s<0?s:SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_FOCUS_OUT && c->popup){UiNode *popup=ui_node(n->owner,c->popup);
            if(!popup || !popup->window->focus)ui_control_popup_close(n);
        }
        return 0;
    }
    if(n->kind==SC_UI_TAB){
        if(e->type==SC_UI_EVENT_POINTER_DOWN && ui_control_left_edge(e) && (e->buttons&1) && e->y<ui_scale(n->owner,26)){
            for(UiTabItem *tab=c->tabs;tab;tab=tab->next)if(e->x+(ScI32)n->scroll_x.lo>=tab->x && e->x+(ScI32)n->scroll_x.lo<tab->x+tab->width){s=ScUiTabSelect(n->owner,n->id,tab->page);
                return s<0?s:SC_UI_HANDLED;
            }
        }
        if(e->type==SC_UI_EVENT_KEY_DOWN && (e->key==SC32_KEY_LEFT || e->key==SC32_KEY_RIGHT || (e->key=='\t' && (e->modifiers&SC32_MOD_CONTROL)))){
            UiTabItem *current=0,*previous=0,*last=0;
            for(UiTabItem *tab=c->tabs;tab;tab=tab->next){if(tab->page==c->selected_page){current=tab;
                    break;
                }
                previous=tab;
            }
            for(last=c->tabs;last && last->next;last=last->next){}
            int backwards=e->key==SC32_KEY_LEFT || (e->modifiers&SC32_MOD_SHIFT);
            UiTabItem *next=backwards?(previous?previous:last):(current && current->next?current->next:c->tabs);
            if(next){s=ScUiTabSelect(n->owner,n->id,next->page);
                return s<0?s:SC_UI_HANDLED;
            }
        }
        return 0;
    }
    if(n->kind==SC_UI_SCROLL)return ui_control_scroll_event(n,e);
    if(n->kind==SC_UI_SPLITTER){
        int vertical=n->parent && n->parent->layout.mode!=SC_UI_VBOX;
        ScI32 coordinate=vertical?e->x:e->y;
        if(e->type==SC_UI_EVENT_POINTER_DOWN && ui_control_left_edge(e) && (e->buttons&1)){c->pressed_part=1;
            c->drag_origin=coordinate+(vertical?n->rectangle.x:n->rectangle.y);
            c->drag_value=c->split_position>=0?c->split_position:n->previous?(vertical?n->previous->rectangle.width:n->previous->rectangle.height):0;
            ScUiCapture(n->owner,n->id,1);
            ScUiFocus(n->owner,n->id);
            return SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_POINTER_MOVE && c->pressed_part){ScI32 value=c->drag_value+coordinate+(vertical?n->rectangle.x:n->rectangle.y)-c->drag_origin;
            if(value<0)value=0;
            s=ScUiSplitterSet(n->owner,n->id,value);
            return s<0?s:SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_KEY_DOWN && (e->key==SC32_KEY_LEFT || e->key==SC32_KEY_RIGHT || e->key==SC32_KEY_UP || e->key==SC32_KEY_DOWN)){
            ScI32 value=c->split_position+(e->key==SC32_KEY_LEFT || e->key==SC32_KEY_UP?-ui_scale(n->owner,4):ui_scale(n->owner,4));
            if(value<0)value=0;
            s=ScUiSplitterSet(n->owner,n->id,value);
            return s<0?s:SC_UI_HANDLED;
        }
        if((e->type==SC_UI_EVENT_POINTER_UP && ui_control_left_edge(e)) || e->type==SC_UI_EVENT_CAPTURE_LOST || e->type==SC_UI_EVENT_INPUT_SYNC){c->pressed_part=0;
            if(e->type==SC_UI_EVENT_POINTER_UP)ScUiCapture(n->owner,n->id,0);
            return SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_POINTER_ENTER){UiNode *root=ui_root(n->owner,n->id);
            ScPointerSetCursor(root->window->window,vertical?SC32_CURSOR_RESIZE_H:SC32_CURSOR_RESIZE_V);
        }
        return 0;
    }
    if(n->kind==SC_UI_BUTTON || n->kind==SC_UI_CHECKBOX || n->kind==SC_UI_RADIO || (n->kind==SC_UI_LABEL && n->label_target)){
        if(e->type==SC_UI_EVENT_POINTER_DOWN && ui_control_left_edge(e) && (e->buttons&1)){
            s=ScUiCapture(n->owner,n->id,1);
            if(s<0)return s;
            ScUiFocus(n->owner,n->id);
            n->state|=SC_UI_PRESSED;
            ui_tree_effective(n);
            ui_node_damage(n);
            return SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_POINTER_MOVE && (n->state&SC_UI_PRESSED)){int inside=ui_control_point_inside(n,e);
            ScU32 effective=(n->effective&~SC_UI_PRESSED)|(inside?SC_UI_PRESSED:0);
            if(effective!=n->effective){n->effective=effective;
                ui_node_damage(n);
            }
            return SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_POINTER_UP && ui_control_left_edge(e) && (n->state&SC_UI_PRESSED)){
            int activate=ui_control_point_inside(n,e);
            n->state&=~SC_UI_PRESSED;
            ui_tree_effective(n);
            ui_node_damage(n);
            ScUiCapture(n->owner,n->id,0);
            s=activate?ui_control_invoke(n):0;
            return s<0?s:SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_KEY_DOWN && (e->key==' ' || e->key=='\n')){if(!c->pressed_key){c->pressed_key=e->key;
                n->state|=SC_UI_PRESSED;
                ui_tree_effective(n);
                ui_node_damage(n);
            }
            return SC_UI_HANDLED;
        }
        /* 未由Space/Enter置位时，修饰键等零符号释放不能触发按钮。 */
        if(e->type==SC_UI_EVENT_KEY_UP && c->pressed_key && c->pressed_key==e->key){c->pressed_key=0;
            n->state&=~SC_UI_PRESSED;
            ui_tree_effective(n);
            ui_node_damage(n);
            s=ui_control_invoke(n);
            return s<0?s:SC_UI_HANDLED;
        }
        if(e->type==SC_UI_EVENT_CAPTURE_LOST || e->type==SC_UI_EVENT_FOCUS_OUT || e->type==SC_UI_EVENT_INPUT_SYNC){c->pressed_key=0;
            n->state&=~SC_UI_PRESSED;
            ui_tree_effective(n);
            ui_node_damage(n);
        }
        if(n->kind==SC_UI_RADIO && e->type==SC_UI_EVENT_KEY_DOWN && (e->key==SC32_KEY_LEFT || e->key==SC32_KEY_UP || e->key==SC32_KEY_RIGHT || e->key==SC32_KEY_DOWN)){
            int backwards=e->key==SC32_KEY_LEFT || e->key==SC32_KEY_UP;
            UiNode *next=backwards?n->previous:n->next;
            while(next && (next->kind!=SC_UI_RADIO || (next->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))!=(SC_UI_VISIBLE|SC_UI_ENABLED)))next=backwards?next->previous:next->next;
            if(!next && n->parent){next=backwards?n->parent->last_child:n->parent->first_child;
                while(next && next!=n && next->kind!=SC_UI_RADIO)next=backwards?next->previous:next->next;
            }
            if(next){s=ScUiRadioSelect(n->owner,n->parent->id,next->id);
                if(s==0)ScUiFocus(n->owner,next->id);
                return s<0?s:SC_UI_HANDLED;
            }
        }
    }
    return ui_control_result(n,s);
}
static void ui_controls_layout(UiNode *n)
{
    if(ui_control_editor(n))ui_edit_layout(n);
    if(n->kind==SC_UI_TAB){UiControl *c=ui_control(n);
        if(c){UiTabItem **slot=&c->tabs;
            while(*slot){UiTabItem *tab=*slot;
                if(!ui_node(n->owner,tab->page)){*slot=tab->next;
                    ScStringRelease(tab->title);
                    free(tab);
                }
                else slot=&tab->next;
            }
        }
    }
}
static ScStatus ui_controls_popup_event(UiNode *root,const ScUiEvent *event)
{
    if(event->type!=SC_UI_EVENT_POINTER_DOWN)return 0;
    UiNode *owner=ui_root(root->owner,root->id);
    if(!owner || !owner->window)return 0;
    for(UiControl *control=ui_control_popups;control;control=control->popup_next){UiNode *n=control->node;
        if(n->owner!=root->owner || n->root!=owner->id || n->kind!=SC_UI_COMBOBOX)continue;
        UiNode *popup=ui_node(n->owner,control->popup);
        if(!popup){ui_control_popup_close(n);
            return SC_UI_HANDLED;
        }
        ScWindowInfo *info=&owner->window->information,*pop=&popup->window->information;
        ScI32 left=(info->width-(ScI32)info->client_width)/2,top=info->height-(ScI32)info->client_height-left;
        ScI32 x=info->x+left+event->x,y=info->y+top+event->y;
        if(ui_rect_contains(&n->rectangle,event->x,event->y))return 0;
        if(x>=pop->x && y>=pop->y && x<pop->x+pop->width && y<pop->y+pop->height)return 0;
        ui_control_popup_close(n);
        return SC_UI_HANDLED;
    }
    return 0;
}

#include "scui_tabs.inc"
