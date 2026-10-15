static ScStatus ui_layout_valid(ScUiContext *c,const ScUiLayoutDesc *d)
{
    ScU32 i;
    ScI32 values[12];
    ScStatus s=ui_input(d,sizeof(*d));
    if(s<0)return s;
    if(!c || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_STALE;
    if(d->mode>SC_UI_SCROLL_LAYOUT || d->flags&~UI_LAYOUT_CLIP || d->reserved[0] || d->reserved[1]
        || d->align_h>SC_UI_STRETCH || d->align_v>SC_UI_STRETCH || d->dock>SC_UI_DOCK_FILL)return SC_UI_E_INVALID;
    if(d->width<-1 || d->height<-1 || d->min_width<0 || d->min_height<0 || d->max_width<-1 || d->max_height<-1
        || (d->max_width>=0 && d->min_width>d->max_width) || (d->max_height>=0 && d->min_height>d->max_height)
        || !d->column_span || !d->row_span || d->column>UINT_MAX-d->column_span || d->row>UINT_MAX-d->row_span)return SC_UI_E_RANGE;
    values[0]=d->width;
    values[1]=d->height;
    values[2]=d->min_width;
    values[3]=d->min_height;
    values[4]=d->max_width;
    values[5]=d->max_height;
    values[6]=d->left;
    values[7]=d->top;
    values[8]=d->margin[0];
    values[9]=d->margin[1];
    values[10]=d->margin[2];
    values[11]=d->margin[3];
    for(i=0;i<12;i++)if(values[i]!=-1 && (ui_scale(c,values[i])==INT_MAX || ui_scale(c,values[i])==-INT_MAX))return SC_UI_E_RANGE;
    for(i=0;i<4;i++)if(d->padding[i]<0 || d->margin[i]<0 || ui_scale(c,d->padding[i])==INT_MAX)return SC_UI_E_RANGE;
    {ScI32 sum;
        if(!ui_signed_add(ui_scale(c,d->padding[0]),ui_scale(c,d->padding[2]),&sum)
            || !ui_signed_add(ui_scale(c,d->padding[1]),ui_scale(c,d->padding[3]),&sum)
            || !ui_signed_add(ui_scale(c,d->margin[0]),ui_scale(c,d->margin[2]),&sum)
            || !ui_signed_add(ui_scale(c,d->margin[1]),ui_scale(c,d->margin[3]),&sum))return SC_UI_E_RANGE;
        }
    if(d->gap>(ScU32)INT_MAX || ui_scale(c,(ScI32)d->gap)==INT_MAX || d->flex_weight>0x7fffffu)return SC_UI_E_RANGE;
    return SC_UI_OK;
}
ScStatus ScUiNodeSetLayout(ScUiContext *c,ScUiNode id,const ScUiLayoutDesc *d)
{
    UiNode *n;
    ScStatus s=ui_layout_valid(c,d);
    if(s<0)return s;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    n=ui_node(c,id);
    if(!n)s=SC_UI_E_STALE;
    else if(c->revision==UINT_MAX)s=SC_UI_E_RANGE;
    else{n->layout=*d;
        c->revision++;
        ui_node_layout_dirty(n);
        s=SC_UI_OK;
        }
    s=ui_cause(c,s,UI_OP_LAYOUT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiNodeGetLayout(ScUiContext *c,ScUiNode id,ScUiLayoutDesc *out)
{UiNode *n;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    n=ui_node(c,id);
    if(!n)return SC_UI_E_STALE;
    memcpy(out,&n->layout,sizeof(*out));
    return SC_UI_OK;
    }
static ScStatus ui_plan_get(ScArray *plan,ScHash *map,ScU32 id,UiPlacement *out,ScU32 *index)
{
    ScHashEntry e;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    if(ScHashGet(map,&id,sizeof(id),&e)<0)return SC_UI_E_STALE;
    *index=*(const ScU32 *)e.value;
    return ScArrayGet(plan,*index,out);
}
static ScI32 ui_clamp_size(ScUiContext *c,ScI32 value,ScI32 minimum,ScI32 maximum)
{ScI32 low=ui_scale(c,minimum),high=maximum<0?INT_MAX:ui_scale(c,maximum);
    if(value<low)value=low;
    if(value>high)value=high;
    return value;
    }
static void ui_intrinsic_padding(UiNode *n,ScI32 *padding)
{
    ScU32 i;
    for(i=0;i<4;i++)padding[i]=0;
    /* 控件标题和页签保留区不是用户padding，替换Layout仍不能覆盖它。 */
    if(n->kind==SC_UI_TAB)padding[1]=ui_scale(n->owner,26);
    else if(n->kind==SC_UI_GROUPBOX){padding[0]=padding[2]=padding[3]=ui_scale(n->owner,8);
        padding[1]=ui_scale(n->owner,22);
        }
}
static ScStatus ui_measure_node(UiNode *n,ScArray *plan,ScHash *map,ScUiSize *out,int wrapped_width)
{
    ScUiContext *c=n->owner;
    ScUiSize constraint,preferred;
    UiNode *child;
    ScStatus s;
    ScI32 total=0,cross=0,gap=ui_scale(c,(ScI32)n->layout.gap);
    ScU32 children=0;
    constraint.width=(ScI32)ui_root(c,n->id)->window->information.client_width;
    constraint.height=(ScI32)ui_root(c,n->id)->window->information.client_height;
    if(wrapped_width && (n->kind==SC_UI_LABEL || n->kind==SC_UI_TOOLTIP)){UiPlacement candidate;
        ScU32 index;
        s=ui_plan_get(plan,map,n->id,&candidate,&index);
        if(s<0)return s;
        constraint.width=candidate.rectangle.width;
    }
    preferred.width=0;
    preferred.height=0;
    if(n->custom && n->custom->description.measure){n->custom->depth++;
        s=n->custom->description.measure(c,n->id,&constraint,&preferred,n->custom->description.context);
        n->custom->depth--;
        }
    else if(n->kind==UI_KIND_POPUP || n->kind==SC_UI_MENUBAR || n->kind==SC_UI_TOOLBAR)s=ui_menu_measure(n,&preferred);
    else s=ui_controls_measure(n,&constraint,&preferred);
    if(s!=0)return s;
    if(preferred.width<0 || preferred.height<0)return SC_UI_E_RANGE;
    for(child=n->first_child;child;child=child->next)if(child->effective&SC_UI_VISIBLE){UiPlacement p;
        ScU32 index;
        ScI32 w,h;
        s=ui_plan_get(plan,map,child->id,&p,&index);
        if(s<0)return s;
        if(!ui_signed_add(p.preferred.width,ui_scale(c,child->layout.margin[0]),&w) || !ui_signed_add(w,ui_scale(c,child->layout.margin[2]),&w)
            || !ui_signed_add(p.preferred.height,ui_scale(c,child->layout.margin[1]),&h) || !ui_signed_add(h,ui_scale(c,child->layout.margin[3]),&h))return SC_UI_E_RANGE;
        if(w<0 || h<0)return SC_UI_E_RANGE;
        if(n->layout.mode==SC_UI_HBOX){if(total>INT_MAX-w)return SC_UI_E_RANGE;
            total+=w;
            if(h>cross)cross=h;
            }
        else if(n->layout.mode==SC_UI_VBOX || n->layout.mode==SC_UI_SCROLL_LAYOUT){if(total>INT_MAX-h)return SC_UI_E_RANGE;
            total+=h;
            if(w>cross)cross=w;
            }
        else{if(w>preferred.width)preferred.width=w;
            if(h>preferred.height)preferred.height=h;
            }
        children++;
    }
    if(children>1 && gap>0){if(children-1>(ScU32)INT_MAX/(ScU32)gap || total>INT_MAX-(ScI32)(children-1)*gap)return SC_UI_E_RANGE;
        total+=(ScI32)(children-1)*gap;
        }
    if(n->layout.mode==SC_UI_HBOX){if(total>preferred.width)preferred.width=total;
        if(cross>preferred.height)preferred.height=cross;
        }
    if(n->layout.mode==SC_UI_VBOX || n->layout.mode==SC_UI_SCROLL_LAYOUT){if(total>preferred.height)preferred.height=total;
        if(cross>preferred.width)preferred.width=cross;
        }
    {ScI32 intrinsic[4],px=ui_scale(c,n->layout.padding[0])+ui_scale(c,n->layout.padding[2]),py=ui_scale(c,n->layout.padding[1])+ui_scale(c,n->layout.padding[3]);
        ui_intrinsic_padding(n,intrinsic);
        if(!ui_signed_add(px,intrinsic[0]+intrinsic[2],&px) || !ui_signed_add(py,intrinsic[1]+intrinsic[3],&py))return SC_UI_E_RANGE;
        if(preferred.width>INT_MAX-px || preferred.height>INT_MAX-py)return SC_UI_E_RANGE;
        preferred.width+=px;
        preferred.height+=py;
        }
    if(n->layout.width>=0)preferred.width=ui_scale(c,n->layout.width);
    if(n->layout.height>=0)preferred.height=ui_scale(c,n->layout.height);
    preferred.width=ui_clamp_size(c,preferred.width,n->layout.min_width,n->layout.max_width);
    preferred.height=ui_clamp_size(c,preferred.height,n->layout.min_height,n->layout.max_height);
    if(!(n->effective&SC_UI_VISIBLE))preferred.width=preferred.height=0;
    *out=preferred;
    return SC_UI_OK;
}
static ScStatus ui_arrange_children(UiNode *n,ScArray *plan,ScHash *map)
{
    ScUiContext *c=n->owner;
    UiPlacement parent;
    ScU32 pi,children=0,total_weight=0;
    UiNode *child;
    ScUiRect inner,remaining;
    ScI32 shrink_space=0,shrink_total=0;
    ScI32 main_total=0,free_space=0,cursor_x=0,cursor_y=0,line_height=0,gap=ui_scale(c,(ScI32)n->layout.gap),padding[4],intrinsic[4],padding_x,padding_y;
    ScStatus s;
    ScU32 grid_rows=1,i;
    s=ui_plan_get(plan,map,n->id,&parent,&pi);
    if(s<0)return s;
    inner=parent.rectangle;
    ui_intrinsic_padding(n,intrinsic);
    for(i=0;i<4;i++)if(!ui_signed_add(ui_scale(c,n->layout.padding[i]),intrinsic[i],&padding[i]))return SC_UI_E_RANGE;
    if(!ui_signed_add(inner.x,padding[0],&inner.x) || !ui_signed_add(inner.y,padding[1],&inner.y))return SC_UI_E_RANGE;
    if(!ui_signed_add(padding[0],padding[2],&padding_x) || !ui_signed_add(padding[1],padding[3],&padding_y))return SC_UI_E_RANGE;
    inner.width-=padding_x;
    inner.height-=padding_y;
    if(inner.width<0)inner.width=0;
    if(inner.height<0)inner.height=0;
    remaining=inner;
    if((n->layout.flags&UI_LAYOUT_CLIP) || n->kind==SC_UI_TAB || n->kind==SC_UI_GROUPBOX){ui_rect_intersection(&inner,&parent.child_clip,&parent.child_clip);
        s=ScArraySet(plan,pi,&parent);
        if(s<0)return s;
        }
    if(n->layout.mode==SC_UI_GRID_LAYOUT)for(child=n->first_child;child;child=child->next)if((child->effective&SC_UI_VISIBLE) && child->layout.row+child->layout.row_span>grid_rows)grid_rows=child->layout.row+child->layout.row_span;
    if(n->layout.mode==SC_UI_HBOX || n->layout.mode==SC_UI_VBOX){
        for(child=n->first_child;child;child=child->next)if(child->effective&SC_UI_VISIBLE){UiPlacement p;
            ScU32 ci;
            ScI32 size;
            s=ui_plan_get(plan,map,child->id,&p,&ci);
            if(s<0)return s;
            if(n->layout.mode==SC_UI_HBOX){if(!ui_signed_add(p.preferred.width,ui_scale(c,child->layout.margin[0]),&size) || !ui_signed_add(size,ui_scale(c,child->layout.margin[2]),&size))return SC_UI_E_RANGE;
                }
            else if(!ui_signed_add(p.preferred.height,ui_scale(c,child->layout.margin[1]),&size) || !ui_signed_add(size,ui_scale(c,child->layout.margin[3]),&size))return SC_UI_E_RANGE;
            if(size>INT_MAX-main_total || child->layout.flex_weight>UINT_MAX-total_weight)return SC_UI_E_RANGE;
            main_total+=size;
            total_weight+=child->layout.flex_weight;
            if(child->layout.flex_weight){ScI32 capacity;
                capacity=n->layout.mode==SC_UI_HBOX
                    ?p.preferred.width-ui_scale(c,child->layout.min_width)
                    :p.preferred.height-ui_scale(c,child->layout.min_height);
                if(capacity<0 || capacity>INT_MAX-shrink_total)return SC_UI_E_RANGE;
                shrink_total+=capacity;
                }
            children++;
        }
        if(children>1){if(gap>0 && children-1>(ScU32)INT_MAX/(ScU32)gap)return SC_UI_E_RANGE;
            if(!ui_signed_add(main_total,(ScI32)(children-1)*gap,&main_total))return SC_UI_E_RANGE;
            }
        free_space=(n->layout.mode==SC_UI_HBOX?inner.width:inner.height)-main_total;
        if(free_space<0){
            /* 空间不足只收缩有flex的控件，并保留声明的最小尺寸。
             * 按可收缩量分配亏空，将整数余数交给后续兄弟，保证
             * 线性遍历后的像素守恒；固定侧栏、margin与gap不变。 */
            shrink_space=-free_space;
            if(shrink_space>shrink_total)shrink_space=shrink_total;
            free_space=0;
            }
    }
    cursor_x=inner.x;
    cursor_y=inner.y;
    for(child=n->first_child;child;child=child->next){UiPlacement p;
        ScU32 ci;
        ScUiRect r;
        ScI32 ml,mt,mr,mb;
        s=ui_plan_get(plan,map,child->id,&p,&ci);
        if(s<0)return s;
        ml=ui_scale(c,child->layout.margin[0]);
        mt=ui_scale(c,child->layout.margin[1]);
        mr=ui_scale(c,child->layout.margin[2]);
        mb=ui_scale(c,child->layout.margin[3]);
        if(child->private_flags&UI_NODE_VIEWPORT_CHILD){r=child->viewport_rectangle;
            if(!ui_signed_add(r.x,parent.rectangle.x,&r.x) || !ui_signed_add(r.y,parent.rectangle.y,&r.y))return SC_UI_E_RANGE;
            goto placed;
            }
        if(!ui_signed_add(inner.x,ml,&r.x) || !ui_signed_add(inner.y,mt,&r.y))return SC_UI_E_RANGE;
        r.width=p.preferred.width;
        r.height=p.preferred.height;
        if(!(child->effective&SC_UI_VISIBLE)){r.width=r.height=0;
            }
        else if(n->layout.mode==SC_UI_HBOX || n->layout.mode==SC_UI_VBOX || n->layout.mode==SC_UI_SCROLL_LAYOUT){
            ScI32 extra=0;
            if(shrink_space && shrink_total && child->layout.flex_weight){ScI32 capacity;
                capacity=n->layout.mode==SC_UI_HBOX
                    ?p.preferred.width-ui_scale(c,child->layout.min_width)
                    :p.preferred.height-ui_scale(c,child->layout.min_height);
                ScI32 take=(ScI32)ui_fraction((ScU32)shrink_space,(ScU32)capacity,(ScU32)shrink_total);
                shrink_space-=take;
                shrink_total-=capacity;
                extra=-take;
                }
            else if(total_weight && child->layout.flex_weight){ScU32 weight=child->layout.flex_weight;
                /* 余数在后续兄弟间继续分配，最后一个flex恰好吃完。 */
                extra=(ScI32)ui_fraction((ScU32)free_space,weight,total_weight);
                free_space-=extra;
                total_weight-=weight;
                }
            if(n->layout.mode==SC_UI_HBOX){ScI32 advance;
                if(!ui_signed_add(cursor_x,ml,&r.x) || !ui_signed_add(r.width,extra,&r.width))return SC_UI_E_RANGE;
                if(child->layout.align_v==SC_UI_STRETCH)r.height=inner.height-mt-mb;
                if(!ui_signed_add(r.width,ml,&advance) || !ui_signed_add(advance,mr,&advance) || !ui_signed_add(advance,gap,&advance) || !ui_signed_add(cursor_x,advance,&cursor_x))return SC_UI_E_RANGE;
                }
            else{ScI32 advance;
                if(!ui_signed_add(cursor_y,mt,&r.y) || !ui_signed_add(r.height,extra,&r.height))return SC_UI_E_RANGE;
                if(child->layout.align_h==SC_UI_STRETCH || child->layout.width==SC_UI_AUTO)r.width=inner.width-ml-mr;
                if(!ui_signed_add(r.height,mt,&advance) || !ui_signed_add(advance,mb,&advance) || !ui_signed_add(advance,gap,&advance) || !ui_signed_add(cursor_y,advance,&cursor_y))return SC_UI_E_RANGE;
                }
        }
        else if(n->layout.mode==SC_UI_DOCK_LAYOUT){
            ScI32 advance,edge;
            ScU32 dock=child->layout.dock?child->layout.dock:SC_UI_DOCK_FILL;
            r=remaining;
            if(!ui_signed_add(r.x,ml,&r.x) || !ui_signed_add(r.y,mt,&r.y))return SC_UI_E_RANGE;
            r.width-=ml+mr;
            r.height-=mt+mb;
            if(dock==SC_UI_DOCK_TOP || dock==SC_UI_DOCK_BOTTOM){r.height=p.preferred.height;
                if(!ui_signed_add(r.height,mt+mb,&advance) || !ui_signed_add(advance,gap,&advance))return SC_UI_E_RANGE;
                if(dock==SC_UI_DOCK_BOTTOM){if(!ui_signed_add(remaining.y,remaining.height,&edge) || !ui_signed_add(edge,-mb,&edge) || !ui_signed_add(edge,-r.height,&r.y))return SC_UI_E_RANGE;
                    }
                else if(!ui_signed_add(remaining.y,advance,&remaining.y))return SC_UI_E_RANGE;
                remaining.height=remaining.height>advance?remaining.height-advance:0;
                }
            else if(dock==SC_UI_DOCK_LEFT || dock==SC_UI_DOCK_RIGHT){r.width=p.preferred.width;
                if(!ui_signed_add(r.width,ml+mr,&advance) || !ui_signed_add(advance,gap,&advance))return SC_UI_E_RANGE;
                if(dock==SC_UI_DOCK_RIGHT){if(!ui_signed_add(remaining.x,remaining.width,&edge) || !ui_signed_add(edge,-mr,&edge) || !ui_signed_add(edge,-r.width,&r.x))return SC_UI_E_RANGE;
                    }
                else if(!ui_signed_add(remaining.x,advance,&remaining.x))return SC_UI_E_RANGE;
                remaining.width=remaining.width>advance?remaining.width-advance:0;
                }
        }
        else if(n->layout.mode==SC_UI_GRID_LAYOUT){
            ScU32 columns=n->layout.columns?n->layout.columns:1,rows=grid_rows;
            ScI32 offset;
            if(child->layout.column>=columns || child->layout.column_span>columns-child->layout.column)return SC_UI_E_RANGE;
            if(columns>(ScU32)INT_MAX || rows>(ScU32)INT_MAX)return SC_UI_E_RANGE;
            offset=(ScI32)ui_fraction((ScU32)inner.width,child->layout.column,columns);
            if(!ui_signed_add(inner.x,offset,&r.x) || !ui_signed_add(r.x,ml,&r.x))return SC_UI_E_RANGE;
            offset=(ScI32)ui_fraction((ScU32)inner.height,child->layout.row,rows);
            if(!ui_signed_add(inner.y,offset,&r.y) || !ui_signed_add(r.y,mt,&r.y))return SC_UI_E_RANGE;
            r.width=(ScI32)(ui_fraction((ScU32)inner.width,child->layout.column+child->layout.column_span,columns)-ui_fraction((ScU32)inner.width,child->layout.column,columns));
            r.height=(ScI32)(ui_fraction((ScU32)inner.height,child->layout.row+child->layout.row_span,rows)-ui_fraction((ScU32)inner.height,child->layout.row,rows));
            r.width=r.width>ml+mr?r.width-ml-mr:0;
            r.width=r.width>gap?r.width-gap:0;
            r.height=r.height>mt+mb?r.height-mt-mb:0;
            r.height=r.height>gap?r.height-gap:0;
        }
        else if(n->layout.mode==SC_UI_FLOW){
            ScI32 advance,cross;
            ScU32 used=(ScU32)cursor_x-(ScU32)inner.x,total;
            if(!ui_signed_add(r.width,ml+mr,&advance) || !ui_signed_add(r.height,mt+mb,&cross))return SC_UI_E_RANGE;
            if(cursor_x>inner.x && (!ui_add(used,(ScU32)advance,&total) || total>(ScU32)inner.width)){cursor_x=inner.x;
                if(!ui_signed_add(line_height,gap,&line_height) || !ui_signed_add(cursor_y,line_height,&cursor_y))return SC_UI_E_RANGE;
                line_height=0;
                }
            if(!ui_signed_add(cursor_x,ml,&r.x) || !ui_signed_add(cursor_y,mt,&r.y) || !ui_signed_add(advance,gap,&advance) || !ui_signed_add(cursor_x,advance,&cursor_x))return SC_UI_E_RANGE;
            if(cross>line_height)line_height=cross;
        }
        else if(n->layout.mode==SC_UI_OVERLAY){
            if(child->layout.width==SC_UI_AUTO || child->layout.align_h==SC_UI_STRETCH)r.width=inner.width-ml-mr;
            if(child->layout.height==SC_UI_AUTO || child->layout.align_v==SC_UI_STRETCH)r.height=inner.height-mt-mb;
        }
        else if(!ui_signed_add(r.x,ui_scale(c,child->layout.left),&r.x) || !ui_signed_add(r.y,ui_scale(c,child->layout.top),&r.y))return SC_UI_E_RANGE;
        if(r.width<0)r.width=0;
        if(r.height<0)r.height=0;
        r.width=ui_clamp_size(c,r.width,child->layout.min_width,child->layout.max_width);
        r.height=ui_clamp_size(c,r.height,child->layout.min_height,child->layout.max_height);
        if(n->layout.mode!=SC_UI_ABSOLUTE && n->layout.mode!=SC_UI_DOCK_LAYOUT && n->layout.mode!=SC_UI_GRID_LAYOUT){
            if(child->layout.align_h==SC_UI_CENTER){if(!ui_signed_add(inner.x,(inner.width-r.width)/2,&r.x))return SC_UI_E_RANGE;
                }
            else if(child->layout.align_h==SC_UI_END_ALIGN){if(!ui_signed_add(inner.x,inner.width-r.width,&r.x) || !ui_signed_add(r.x,-mr,&r.x))return SC_UI_E_RANGE;
                }
            if(child->layout.align_v==SC_UI_CENTER){if(!ui_signed_add(inner.y,(inner.height-r.height)/2,&r.y))return SC_UI_E_RANGE;
                }
            else if(child->layout.align_v==SC_UI_END_ALIGN){if(!ui_signed_add(inner.y,inner.height-r.height,&r.y) || !ui_signed_add(r.y,-mb,&r.y))return SC_UI_E_RANGE;
                }
        }
        if(!(child->private_flags&UI_NODE_VIEWPORT_CHILD)){
            if(n->scroll_x.hi || n->scroll_x.lo>(ScU32)INT_MAX || !ui_signed_add(r.x,-(ScI32)n->scroll_x.lo,&r.x))r.x=-INT_MAX;
            if(n->scroll_y.hi || n->scroll_y.lo>(ScU32)INT_MAX || !ui_signed_add(r.y,-(ScI32)n->scroll_y.lo,&r.y))r.y=-INT_MAX;
            }
        {ScI32 edge;
            if(!ui_signed_add(r.x,r.width,&edge) || !ui_signed_add(r.y,r.height,&edge))return SC_UI_E_RANGE;
            }
placed:
        p.rectangle=r;
        ui_rect_intersection(&r,&parent.child_clip,&p.clip);
        p.child_clip=(child->layout.flags&UI_LAYOUT_CLIP)?p.clip:parent.child_clip;
        s=ScArraySet(plan,ci,&p);
        if(s<0)return s;
    }
    return SC_UI_OK;
}
static ScStatus ui_layout_root(UiNode *root)
{
    ScUiContext *c=root->owner;
    ScArray *plan=0;
    ScHash *map=0;
    UiNode *n;
    ScU32 count=0,i,pass,revision=c->revision;
    ScStatus s;
    if(!root->window || (root->private_flags&UI_NODE_RETIRED))return SC_UI_E_STALE;
    if(c->layout_depth)return SC_UI_E_BUSY;
    c->layout_depth++;
    ui_node_hold(root);
    c->walking++;
    s=ui_array_new(sizeof(UiPlacement),&plan);
    if(s==0)s=ui_hash_new(sizeof(ScU32),&map);
    if(s<0)goto done;
    n=root;
    while(n){UiPlacement p;
        memset(&p,0,sizeof(p));
        p.node=n->id;
        p.revision=n->value_revision;
        s=ScArrayInsert(plan,count,&p,1);
        if(s==0)s=ScHashSet(map,&n->id,sizeof(n->id),&count,SC_HASH_INSERT_ONLY);
        if(s<0)goto done;
        count++;
        if(n->first_child){n=n->first_child;
            continue;
            }
        while(n!=root && !n->next)n=n->parent;
        if(n==root)break;
        n=n->next;
        }
    /* 第一遍得到Splitter、Dock和父padding后的真实宽度；第二遍
     * 将Label换行高度自底向上传回VBox，再算flex余量和兄弟位置。
     * 两遍共用候选，不发布中间帧，应用arrange只在最终一遍调用。 */
    for(pass=0;pass<2;pass++){
        for(i=count;i;i--){UiPlacement p;
            ScArrayGet(plan,i-1,&p);
            n=ui_node(c,p.node);
            if(!n){s=SC_UI_E_CANCELLED;
                goto done;
                }
            ui_node_hold(n);
            s=ui_measure_node(n,plan,map,&p.preferred,pass!=0);
            ui_node_leave(n);
            if(s!=0 || revision!=c->revision){if(s>=0)s=SC_UI_E_BUSY;
                goto done;
                }
            s=ScArraySet(plan,i-1,&p);
            if(s<0)goto done;
            }
        {UiPlacement p;
            ScArrayGet(plan,0,&p);
            p.rectangle.x=p.rectangle.y=0;
            p.rectangle.width=(ScI32)root->window->information.client_width;
            p.rectangle.height=(ScI32)root->window->information.client_height;
            p.clip=p.rectangle;
            p.child_clip=p.clip;
            ScArraySet(plan,0,&p);
            }
        for(i=0;i<count;i++){UiPlacement p;
            ScArrayGet(plan,i,&p);
            n=ui_node(c,p.node);
            if(!n){s=SC_UI_E_CANCELLED;
                goto done;
                }
            s=ui_arrange_children(n,plan,map);
            if(s<0)goto done;
            if(pass && n->custom && n->custom->description.arrange){ui_node_hold(n);
                n->custom->depth++;
                s=n->custom->description.arrange(c,n->id,&p.rectangle,n->custom->description.context);
                n->custom->depth--;
                ui_node_leave(n);
                }
            if(s!=0 || revision!=c->revision){if(s>=0)s=SC_UI_E_BUSY;
                goto done;
                }}
    }
    if(c->revision==UINT_MAX){s=SC_UI_E_RANGE;
        goto done;
        }
    /* 候选全完成才发布，measure/arrange失败从不留下半棵新布局。 */
    for(i=0;i<count;i++){UiPlacement p;
        ScArrayGet(plan,i,&p);
        n=ui_node(c,p.node);
        if(!n){s=SC_UI_E_CANCELLED;
            goto done;
            }
        n->rectangle=p.rectangle;
        n->clip=p.clip;
        n->child_clip=p.child_clip;
        n->preferred=p.preferred;
        n->layout_revision=c->revision+1;
        n->private_flags&=~UI_NODE_LAYOUT_DIRTY;
        }
    c->revision++;
    for(i=0;i<count;i++){UiPlacement p;
        ScArrayGet(plan,i,&p);
        n=ui_node(c,p.node);
        if(n)ui_controls_layout(n);
        }
    ui_node_damage(root);
    s=ui_event(root,SC_UI_EVENT_LAYOUT,0,0);
    if(s<0)c->notification_error=s;
    s=SC_UI_OK;
done:ScHashRelease(map);
    ScArrayRelease(plan);
    c->walking--;
    c->layout_depth--;
    ui_node_leave(root);
    return s;
}
ScStatus ScUiLayout(ScUiContext *c,ScUiNode id)
{UiNode *root;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    root=ui_root(c,id);
    s=root?ui_layout_root(root):SC_UI_E_STALE;
    s=ui_cause(c,s,UI_OP_LAYOUT,0);
    ui_object_leave(&c->object);
    return s;
    }
ScStatus ScUiBatchBegin(ScUiContext *c)
{if(!c || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_STALE;
    if(c->batch==UINT_MAX)return SC_UI_E_RANGE;
    c->batch++;
    return SC_UI_OK;
    }
ScStatus ScUiBatchEnd(ScUiContext *c)
{if(!c || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_STALE;
    if(!c->batch)return SC_UI_E_INVALID;
    c->batch--;
    return SC_UI_OK;
    }
ScStatus ScUiEnsureVisible(ScUiContext *c,ScUiNode id,const ScUiRect *local)
{
    UiNode *n=ui_node(c,id),*parent;
    ScUiRect r;
    if(!n)return SC_UI_E_STALE;
    r=n->rectangle;
    if(local){r=*local;
        r.x+=n->rectangle.x;
        r.y+=n->rectangle.y;
        }
    for(parent=n->parent;parent;parent=parent->parent)if(parent->kind==SC_UI_SCROLL || parent->layout.mode==SC_UI_SCROLL_LAYOUT){ScWideCounter x=parent->scroll_x,y=parent->scroll_y;
        if(r.x<parent->rectangle.x){ScU32 delta=(ScU32)(parent->rectangle.x-r.x);
            x.lo=x.lo>delta?x.lo-delta:0;
            }
        else if(ui_rect_right(&r)>ui_rect_right(&parent->rectangle))ui_wide_add(&x,(ScU32)(ui_rect_right(&r)-ui_rect_right(&parent->rectangle)));
        if(r.y<parent->rectangle.y){ScU32 delta=(ScU32)(parent->rectangle.y-r.y);
            y.lo=y.lo>delta?y.lo-delta:0;
            }
        else if(ui_rect_bottom(&r)>ui_rect_bottom(&parent->rectangle))ui_wide_add(&y,(ScU32)(ui_rect_bottom(&r)-ui_rect_bottom(&parent->rectangle)));
        return ScUiScrollTo(c,parent->id,&x,&y);
        }
    return SC_UI_OK;
}
