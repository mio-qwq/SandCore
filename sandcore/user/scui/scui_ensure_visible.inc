static ScStatus ui_scroll_reveal_axis(ScI32 first,ScI32 extent,ScI32 view_first,ScI32 view_extent,ScU32 current,ScWideCounter *out)
{
    ScI32 last,view_last,delta=0;
    if(!ui_signed_add(first,extent,&last) || !ui_signed_add(view_first,view_extent,&view_last))return SC_UI_E_RANGE;
    if(first<view_first || extent>view_extent){
        ScU32 distance=first<view_first?(ScU32)view_first-(ScU32)first:(ScU32)first-(ScU32)view_first;
        if(distance>(ScU32)INT_MAX)return SC_UI_E_RANGE;
        delta=first<view_first?-(ScI32)distance:(ScI32)distance;
    }else if(last>view_last){ScU32 distance=(ScU32)last-(ScU32)view_last;if(distance>(ScU32)INT_MAX)return SC_UI_E_RANGE;delta=(ScI32)distance;}
    out->hi=0;
    if(delta<0){ScU32 distance=0u-(ScU32)delta;out->lo=distance>current?0:current-distance;}
    else {out->lo=current;ui_wide_add(out,(ScU32)delta);}
    return 0;
}
ScStatus ScUiEnsureVisible(ScUiContext *c,ScUiNode id,const ScUiRect *local)
{
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);if(s<0)return s;
    ScArray *path=0;ScU32 count=0;UiNode *n=ui_node(c,id);ScUiRect rectangle;
    if(!n){s=SC_UI_E_STALE;goto done;}
    rectangle=n->rectangle;
    if(local){
        if(local->width<0 || local->height<0){s=SC_UI_E_RANGE;goto done;}
        rectangle=*local;
        if(!ui_signed_add(rectangle.x,n->rectangle.x,&rectangle.x) || !ui_signed_add(rectangle.y,n->rectangle.y,&rectangle.y)){s=SC_UI_E_RANGE;goto done;}
    }
    /* 无滚动祖先不分配；保存ID，不跨ValueChanged回调借祖先指针。 */
    for(UiNode *parent=n->parent;parent;parent=parent->parent)if(ui_scroll_layout_node(parent) || parent->kind==SC_UI_SCROLL){
        if(!path){s=ui_array_new(sizeof(ScU32),&path);if(s<0)goto done;}
        s=ScArrayInsert(path,count++,&parent->id,1);if(s<0)goto done;
    }
    for(ScU32 i=0;i<count;i++){
        ScU32 parent_id;s=ScArrayGet(path,i,&parent_id);if(s<0)goto done;
        UiNode *parent=ui_node(c,parent_id);if(!parent){s=SC_UI_E_STALE;goto done;}
        if(parent->scroll_x.hi || parent->scroll_y.hi || parent->scroll_x.lo>(ScU32)INT_MAX || parent->scroll_y.lo>(ScU32)INT_MAX){s=SC_UI_E_RANGE;goto done;}
        ScUiRect viewport;s=ui_scroll_viewport(parent,&viewport);if(s<0)goto done;
        ScWideCounter x,y;ScU32 before_x=parent->scroll_x.lo,before_y=parent->scroll_y.lo;
        s=ui_scroll_reveal_axis(rectangle.x,rectangle.width,viewport.x,viewport.width,before_x,&x);if(s<0)goto done;
        s=ui_scroll_reveal_axis(rectangle.y,rectangle.height,viewport.y,viewport.height,before_y,&y);if(s<0)goto done;
        ScU32 revision=c->revision,value_revision=parent->value_revision;
        s=ScUiScrollTo(c,parent_id,&x,&y);if(s<0)goto done;
        parent=ui_node(c,parent_id);if(!parent || !ui_node(c,id)){s=SC_UI_E_STALE;goto done;}
        if(parent->scroll_x.hi || parent->scroll_y.hi || parent->scroll_x.lo>(ScU32)INT_MAX || parent->scroll_y.lo>(ScU32)INT_MAX){s=SC_UI_E_RANGE;goto done;}
        ScU32 changed=parent->value_revision!=value_revision;
        if(c->revision!=revision+changed){s=SC_UI_E_BUSY;goto done;}
        /* 内层尚未安排，先投影将显示的目标矩形，再计算外层offset。 */
        ScI32 dx=(ScI32)before_x-(ScI32)parent->scroll_x.lo,dy=(ScI32)before_y-(ScI32)parent->scroll_y.lo;
        if(!ui_signed_add(rectangle.x,dx,&rectangle.x) || !ui_signed_add(rectangle.y,dy,&rectangle.y)){s=SC_UI_E_RANGE;goto done;}
    }
    s=0;
done:
    ScArrayRelease(path);s=ui_cause(c,s,UI_OP_LAYOUT,id);ui_object_leave((UiObject *)c);return s;
}
