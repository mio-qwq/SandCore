static int ui_scroll_layout_node(const UiNode *n)
{
    return n->layout.mode==SC_UI_SCROLL_LAYOUT && !n->view
        && n->kind!=SC_UI_EDIT && n->kind!=SC_UI_TEXTBOX && n->kind!=SC_UI_COMBOBOX
        && n->kind!=SC_UI_TAB && n->kind!=SC_UI_MENUBAR && n->kind!=SC_UI_TOOLBAR
        && n->kind!=UI_KIND_POPUP;
}
static int ui_scroll_layout_bar(const UiNode *parent,const UiNode *child)
{
    return ui_scroll_layout_node(parent) && child->kind==SC_UI_SCROLL
        && !(child->private_flags&UI_NODE_VIEWPORT_CHILD)
        && child->layout.dock>=SC_UI_DOCK_TOP && child->layout.dock<=SC_UI_DOCK_RIGHT;
}
static int ui_layout_content_item(const UiNode *parent,const UiNode *child)
{
    return (child->effective&SC_UI_VISIBLE) && !(child->private_flags&UI_NODE_VIEWPORT_CHILD)
        && !ui_scroll_layout_bar(parent,child);
}
static int ui_layout_child_range(const UiNode *parent,const UiLayoutPlacement *placement,const UiNode *child)
{
    return ui_layout_content_item(parent,child) && (ui_scroll_layout_node(parent)
        || (placement->range_needed && !(parent->layout.flags&UI_LAYOUT_CLIP)
            && parent->kind!=SC_UI_TAB && parent->kind!=SC_UI_GROUPBOX));
}
static ScStatus ui_layout_inner(UiNode *n,const ScUiRect *rectangle,ScUiRect *inner)
{
    ScI32 pad[4],intrinsic[4],x,y;
    ui_intrinsic_padding(n,intrinsic);
    for(ScU32 i=0;i<4;i++)if(!ui_signed_add(ui_scale(n->owner,n->layout.padding[i]),intrinsic[i],pad+i))return SC_UI_E_RANGE;
    if(!ui_signed_add(pad[0],pad[2],&x) || !ui_signed_add(pad[1],pad[3],&y))return SC_UI_E_RANGE;
    *inner=*rectangle;
    if(!ui_signed_add(inner->x,pad[0],&inner->x) || !ui_signed_add(inner->y,pad[1],&inner->y))return SC_UI_E_RANGE;
    inner->width=inner->width>x?inner->width-x:0;
    inner->height=inner->height>y?inner->height-y:0;
    return 0;
}
static ScStatus ui_scroll_take_bar(UiNode *child,const ScUiSize *preferred,ScUiRect *area,ScUiRect *out)
{
    ScI32 m[4],outer,extent,margin_x,margin_y;
    for(ScU32 i=0;i<4;i++)m[i]=ui_scale(child->owner,child->layout.margin[i]);
    if(!ui_signed_add(m[0],m[2],&margin_x) || !ui_signed_add(m[1],m[3],&margin_y))return SC_UI_E_RANGE;
    *out=*area;
    if(child->layout.dock==SC_UI_DOCK_LEFT || child->layout.dock==SC_UI_DOCK_RIGHT){
        if(!ui_signed_add(preferred->width,m[0],&outer) || !ui_signed_add(outer,m[2],&outer))return SC_UI_E_RANGE;
        extent=outer<area->width?outer:area->width;
        if(child->layout.dock==SC_UI_DOCK_RIGHT){if(!ui_signed_add(area->x,area->width-extent,&out->x))return SC_UI_E_RANGE;}
        else if(!ui_signed_add(area->x,extent,&area->x))return SC_UI_E_RANGE;
        area->width-=extent;
        out->width=extent;
    }else {
        if(!ui_signed_add(preferred->height,m[1],&outer) || !ui_signed_add(outer,m[3],&outer))return SC_UI_E_RANGE;
        extent=outer<area->height?outer:area->height;
        if(child->layout.dock==SC_UI_DOCK_BOTTOM){if(!ui_signed_add(area->y,area->height-extent,&out->y))return SC_UI_E_RANGE;}
        else if(!ui_signed_add(area->y,extent,&area->y))return SC_UI_E_RANGE;
        area->height-=extent;
        out->height=extent;
    }
    if(!ui_signed_add(out->x,m[0],&out->x) || !ui_signed_add(out->y,m[1],&out->y))return SC_UI_E_RANGE;
    out->width=out->width>margin_x?out->width-margin_x:0;
    out->height=out->height>margin_y?out->height-margin_y:0;
    return 0;
}
static ScStatus ui_scroll_layout_area(UiNode *n,const ScUiRect *rectangle,ScArray *plan,ScHash *map,ScUiRect *out)
{
    ScStatus s=ui_layout_inner(n,rectangle,out);
    if(s<0)return s;
    for(UiNode *child=n->first_child;child;child=child->next)if((child->effective&SC_UI_VISIBLE) && ui_scroll_layout_bar(n,child)){
        ScUiSize preferred=child->preferred;
        if(plan){UiLayoutPlacement p;ScU32 index;s=ui_plan_get(plan,map,child->id,&p,&index);if(s<0)return s;preferred=p.preferred;}
        ScUiRect bar;s=ui_scroll_take_bar(child,&preferred,out,&bar);if(s<0)return s;
    }
    return 0;
}
static ScStatus ui_scroll_viewport(UiNode *n,ScUiRect *out)
{
    if(!ui_scroll_layout_node(n)){*out=n->rectangle;return 0;}
    return ui_scroll_layout_area(n,&n->rectangle,0,0,out);
}
static ScStatus ui_layout_outer(UiNode *child,const ScUiSize *preferred,ScI32 *width,ScI32 *height)
{
    ScUiContext *c=child->owner;
    if(!ui_signed_add(preferred->width,ui_scale(c,child->layout.margin[0]),width)
       || !ui_signed_add(*width,ui_scale(c,child->layout.margin[2]),width)
       || !ui_signed_add(preferred->height,ui_scale(c,child->layout.margin[1]),height)
       || !ui_signed_add(*height,ui_scale(c,child->layout.margin[3]),height))return SC_UI_E_RANGE;
    return 0;
}
/* 同一打包规则同时用于测量和安排。每项最多读两次，不能为每个
 * mouse move扫描整树；隐藏/私有viewport项也不偷偷增加一条空行。 */
static ScStatus ui_flow_row(UiNode *parent,UiNode *start,ScI32 available,ScArray *plan,ScHash *map,
                            UiNode **next,ScI32 *row_width,ScI32 *row_height,ScU32 *items)
{
    ScI32 gap=ui_scale(parent->owner,(ScI32)parent->layout.gap);
    *row_width=*row_height=0;*items=0;
    for(UiNode *child=start;child;child=child->next)if(ui_layout_content_item(parent,child)){
        UiLayoutPlacement p;ScU32 index;ScI32 w,h,total;
        ScStatus s=ui_plan_get(plan,map,child->id,&p,&index);if(s<0)return s;
        s=ui_layout_outer(child,&p.preferred,&w,&h);if(s<0)return s;
        if(!ui_signed_add(*row_width,*items?gap:0,&total) || !ui_signed_add(total,w,&total))return SC_UI_E_RANGE;
        if(*items && total>available){*next=child;return 0;}
        *row_width=total;if(h>*row_height)*row_height=h;(*items)++;
    }
    *next=0;return 0;
}
static ScStatus ui_flow_measure(UiNode *n,ScArray *plan,ScHash *map,ScI32 available,ScI32 *height)
{
    UiNode *start=n->first_child;ScU32 rows=0;
    ScI32 gap=ui_scale(n->owner,(ScI32)n->layout.gap);*height=0;
    while(start){UiNode *next;ScI32 width,line;ScU32 items;
        ScStatus s=ui_flow_row(n,start,available,plan,map,&next,&width,&line,&items);if(s<0)return s;
        if(items){if(!ui_signed_add(*height,rows?gap:0,height) || !ui_signed_add(*height,line,height))return SC_UI_E_RANGE;rows++;}
        start=next;
    }
    return 0;
}
static ScStatus ui_layout_place(const UiLayoutPlacement *parent,UiNode *child,UiLayoutPlacement *p,
                                const ScUiRect *base_clip,ScArray *plan,ScU32 index)
{
    p->range_needed=ui_layout_child_range(child->parent,parent,child);
    ui_rect_intersection(&p->rectangle,base_clip,&p->clip);
    p->child_clip=(child->layout.flags&UI_LAYOUT_CLIP)?p->clip:*base_clip;
    return ScArraySet(plan,index,p);
}
static ScStatus ui_arrange_flow(UiNode *n,const UiLayoutPlacement *parent,const ScUiRect *inner,ScArray *plan,ScHash *map)
{
    ScUiContext *c=n->owner;UiNode *start=n->first_child;
    ScI32 y=inner->y,gap=ui_scale(c,(ScI32)n->layout.gap);
    while(start){UiNode *next;ScI32 row_width,row_height,x=inner->x;ScU32 items,count=0;
        ScStatus s=ui_flow_row(n,start,inner->width,plan,map,&next,&row_width,&row_height,&items);if(s<0)return s;
        for(UiNode *child=start;child!=next;child=child->next){
            if(!child)break;
            if(!ui_layout_content_item(n,child))continue;
            UiLayoutPlacement p;ScU32 index;ScI32 outer_w,outer_h;
            s=ui_plan_get(plan,map,child->id,&p,&index);if(s<0)return s;
            s=ui_layout_outer(child,&p.preferred,&outer_w,&outer_h);if(s<0)return s;
            if(count && !ui_signed_add(x,gap,&x))return SC_UI_E_RANGE;
            p.rectangle.x=x;p.rectangle.y=y;
            p.rectangle.width=p.preferred.width;p.rectangle.height=p.preferred.height;
            if(!ui_signed_add(p.rectangle.x,ui_scale(c,child->layout.margin[0]),&p.rectangle.x)
               || !ui_signed_add(p.rectangle.y,ui_scale(c,child->layout.margin[1]),&p.rectangle.y))return SC_UI_E_RANGE;
            if(child->layout.align_v==SC_UI_CENTER){if(!ui_signed_add(p.rectangle.y,(row_height-outer_h)/2,&p.rectangle.y))return SC_UI_E_RANGE;}
            else if(child->layout.align_v==SC_UI_END_ALIGN){if(!ui_signed_add(p.rectangle.y,row_height-outer_h,&p.rectangle.y))return SC_UI_E_RANGE;}
            else if(child->layout.align_v==SC_UI_STRETCH){p.rectangle.height=ui_clamp_size(c,p.rectangle.height+(row_height-outer_h),child->layout.min_height,child->layout.max_height);}
            if(n->scroll_x.hi || n->scroll_x.lo>(ScU32)INT_MAX || !ui_signed_add(p.rectangle.x,-(ScI32)n->scroll_x.lo,&p.rectangle.x))return SC_UI_E_RANGE;
            if(n->scroll_y.hi || n->scroll_y.lo>(ScU32)INT_MAX || !ui_signed_add(p.rectangle.y,-(ScI32)n->scroll_y.lo,&p.rectangle.y))return SC_UI_E_RANGE;
            s=ui_layout_place(parent,child,&p,&parent->child_clip,plan,index);if(s<0)return s;
            if(!ui_signed_add(x,outer_w,&x))return SC_UI_E_RANGE;
            count++;
        }
        if(next && (!ui_signed_add(y,row_height,&y) || !ui_signed_add(y,gap,&y)))return SC_UI_E_RANGE;
        start=next;
    }
    for(UiNode *child=n->first_child;child;child=child->next)if(!ui_layout_content_item(n,child)){
        UiLayoutPlacement p;ScU32 index;ScStatus s=ui_plan_get(plan,map,child->id,&p,&index);if(s<0)return s;
        p.rectangle.x=inner->x;p.rectangle.y=inner->y;
        p.rectangle.width=0;p.rectangle.height=0;
        if((child->effective&SC_UI_VISIBLE) && (child->private_flags&UI_NODE_VIEWPORT_CHILD)){
            p.rectangle=child->viewport_rectangle;
            if(!ui_signed_add(p.rectangle.x,parent->rectangle.x,&p.rectangle.x) || !ui_signed_add(p.rectangle.y,parent->rectangle.y,&p.rectangle.y))return SC_UI_E_RANGE;
        }
        s=ui_layout_place(parent,child,&p,&parent->child_clip,plan,index);if(s<0)return s;
    }
    return 0;
}
static ScStatus ui_layout_union(ScUiRect *area,const ScUiRect *other)
{
    if(other->width<=0 || other->height<=0)return 0;
    if(area->width<=0 || area->height<=0){*area=*other;return 0;}
    ScI32 right,bottom,other_right,other_bottom;
    if(!ui_signed_add(area->x,area->width,&right) || !ui_signed_add(area->y,area->height,&bottom)
       || !ui_signed_add(other->x,other->width,&other_right) || !ui_signed_add(other->y,other->height,&other_bottom))return SC_UI_E_RANGE;
    if(other->x<area->x)area->x=other->x;
    if(other->y<area->y)area->y=other->y;
    if(other_right>right)right=other_right;
    if(other_bottom>bottom)bottom=other_bottom;
    ScU32 width=(ScU32)right-(ScU32)area->x,height=(ScU32)bottom-(ScU32)area->y;
    if(width>(ScU32)INT_MAX || height>(ScU32)INT_MAX)return SC_UI_E_RANGE;
    area->width=(ScI32)width;area->height=(ScI32)height;return 0;
}
/* 所有generic offset暂取0后，一次逆序传播实际未裁剪范围。内嵌
 * Edit/Table/Scroll自己的隐藏正文不会扩大外层范围；普通无clip的
 * HBox等溢出后代则不能丢掉。随后一次正序应用夹紧offset与新clip。 */
static ScStatus ui_layout_scroll_ranges(ScUiContext *c,ScArray *plan,ScHash *map,ScU32 count)
{
    for(ScU32 i=count;i;i--){UiLayoutPlacement p;ScStatus s=ScArrayGet(plan,i-1,&p);if(s<0)return s;
        UiNode *n=ui_node(c,p.node);if(!n)return SC_UI_E_CANCELLED;
        p.overflow=p.rectangle;
        if(!(n->effective&SC_UI_VISIBLE))p.overflow.width=p.overflow.height=0;
        if(ui_scroll_layout_node(n) && !(n->effective&SC_UI_VISIBLE)){
            p.content_width=n->content_width;p.content_height=n->content_height;
            p.scroll_x=n->scroll_x;p.scroll_y=n->scroll_y;p.scroll_changed=0;
        }else if(ui_scroll_layout_node(n)){
            ScUiRect viewport;s=ui_scroll_layout_area(n,&p.rectangle,plan,map,&viewport);if(s<0)return s;
            ScU32 width=(ScU32)viewport.width,height=(ScU32)viewport.height;
            for(UiNode *child=n->first_child;child;child=child->next)if(ui_layout_content_item(n,child)){
                UiLayoutPlacement q;ScU32 index;s=ui_plan_get(plan,map,child->id,&q,&index);if(s<0)return s;
                ScI32 right,bottom;
                if(!ui_signed_add(q.overflow.x,q.overflow.width,&right) || !ui_signed_add(q.overflow.y,q.overflow.height,&bottom)
                   || !ui_signed_add(right,ui_scale(c,child->layout.margin[2]),&right) || !ui_signed_add(bottom,ui_scale(c,child->layout.margin[3]),&bottom))return SC_UI_E_RANGE;
                if(right>viewport.x){ScU32 value=(ScU32)right-(ScU32)viewport.x;if(value>(ScU32)INT_MAX)return SC_UI_E_RANGE;if(value>width)width=value;}
                if(bottom>viewport.y){ScU32 value=(ScU32)bottom-(ScU32)viewport.y;if(value>(ScU32)INT_MAX)return SC_UI_E_RANGE;if(value>height)height=value;}
            }
            p.content_width.lo=width;p.content_width.hi=0;
            p.content_height.lo=height;p.content_height.hi=0;
            ScU32 maximum_x=width-(ScU32)viewport.width,maximum_y=height-(ScU32)viewport.height;
            p.scroll_x.lo=n->scroll_x.hi || n->scroll_x.lo>maximum_x?maximum_x:n->scroll_x.lo;
            p.scroll_y.lo=n->scroll_y.hi || n->scroll_y.lo>maximum_y?maximum_y:n->scroll_y.lo;
            p.scroll_x.hi=0;p.scroll_y.hi=0;
            p.scroll_changed=ui_wide_compare(&p.scroll_x,&n->scroll_x) || ui_wide_compare(&p.scroll_y,&n->scroll_y);
            if(p.scroll_changed && n->value_revision==UINT_MAX)return SC_UI_E_RANGE;
        }else if(p.range_needed && !(n->layout.flags&UI_LAYOUT_CLIP) && n->kind!=SC_UI_TAB && n->kind!=SC_UI_GROUPBOX && (n->effective&SC_UI_VISIBLE)){
            for(UiNode *child=n->first_child;child;child=child->next)if(child->effective&SC_UI_VISIBLE){
                UiLayoutPlacement q;ScU32 index;s=ui_plan_get(plan,map,child->id,&q,&index);if(s<0)return s;
                s=ui_layout_union(&p.overflow,&q.overflow);if(s<0)return s;
            }
        }
        s=ScArraySet(plan,i-1,&p);if(s<0)return s;
    }
    return 0;
}
static ScU32 ui_scroll_wheel_amount(ScI32 delta,ScU32 step)
{
    ScU32 magnitude=delta<0?0u-(ScU32)delta:(ScU32)delta;
    if(step>UINT_MAX/3)return UINT_MAX;
    step*=3;
    return step && magnitude>UINT_MAX/step?UINT_MAX:magnitude*step;
}
static ScI32 ui_scroll_bar_travel(ScI32 length,ScI32 side,ScU32 content,ScU32 viewport)
{
    if(length<=0 || side<0 || (ScU32)length<=(ScU32)side*2u)return 0;
    ScI32 track=length-side*2;
    ScI32 thumb=content>viewport && content?(ScI32)ui_fraction(viewport,(ScU32)track,content):track;
    if(thumb<side)thumb=side;
    if(thumb>track)thumb=track;
    return track-thumb;
}
static ScU32 ui_scroll_wheel_next(ScU32 current,ScU32 maximum,ScI32 delta,ScU32 step)
{
    ScU32 amount=ui_scroll_wheel_amount(delta,step);
    if(current>maximum)current=maximum;
    return delta>0?(amount>current?0:current-amount):(amount>maximum-current?maximum:current+amount);
}
static ScStatus ui_scroll_layout_wheel(UiNode *target,const ScUiEvent *event)
{
    if(event->type!=SC_UI_EVENT_WHEEL || !event->message)return 0;
    ScI32 vertical=(ScI32)event->message->data[2],horizontal=(ScI32)event->message->data[3];
    if(!vertical && !horizontal)return 0;
    for(UiNode *n=target;n;n=n->parent)if(ui_scroll_layout_node(n)){
        ScUiRect viewport;ScStatus s=ui_scroll_viewport(n,&viewport);if(s<0)return s;
        ScU32 max_x=n->content_width.lo>(ScU32)viewport.width?n->content_width.lo-(ScU32)viewport.width:0;
        ScU32 max_y=n->content_height.lo>(ScU32)viewport.height?n->content_height.lo-(ScU32)viewport.height:0;
        ScWideCounter x={horizontal?ui_scroll_wheel_next(n->scroll_x.lo,max_x,horizontal,(ScU32)ui_scale(n->owner,16)):n->scroll_x.lo,0};
        ScWideCounter y={vertical?ui_scroll_wheel_next(n->scroll_y.lo,max_y,vertical,(ScU32)ui_scale(n->owner,20)):n->scroll_y.lo,0};
        if(ui_wide_compare(&x,&n->scroll_x) || ui_wide_compare(&y,&n->scroll_y)){
            s=ScUiScrollTo(n->owner,n->id,&x,&y);return s<0?s:SC_UI_HANDLED;
        }
    }
    return 0;
}
