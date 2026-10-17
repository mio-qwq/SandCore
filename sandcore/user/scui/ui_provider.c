static void ui_columns_free(ScArray *columns)
{
    ScContainerInfo info;
    ScU32 i;
    if(!columns)return;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(columns,&info);
    for(i=0;i<info.length;i++){UiColumn column;
        ScArrayGet(columns,i,&column);
        ScStringRelease(column.title);
        }
    ScArrayRelease(columns);
}
static void ui_view_free(UiView *v)
{
    if(v->cells){ScIterator it;
        ScHashEntry e;
        memset(&it,0,sizeof(it));
        it.size=sizeof(it);
        it.version=1;
        ScHashIterate(v->cells,&it);
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        while(ScHashNext(&it,&e)==0){UiCell *cell=*(UiCell * const *)e.value;
            ScStringRelease(cell->text);
            free(cell);
            }
        ScHashRelease(v->cells);
        }
    ui_provider_detach(v->binding);
    ScUiSelectionRelease(v->selection);
    ScUiProviderRelease(v->provider);
    ui_columns_free(v->columns);
    ScArrayRelease(v->column_order);
    ScArrayRelease(v->visible);
    ScHashRelease(v->expanded);
    ScStringRelease(v->search);
    free(v);
}
static ScStatus ui_view_new(UiNode *n,UiView **out)
{
    UiView *v=(UiView *)calloc(1,sizeof(*v));
    ScStatus s;
    if(!v)return SC_UI_E_MEMORY;
    v->tree=n->kind==SC_UI_TREE;
    v->resize_column=UINT_MAX;
    s=ui_array_new(sizeof(UiColumn),&v->columns);
    if(s==0)s=ui_array_new(sizeof(ScU32),&v->column_order);
    if(s==0)s=ui_array_new(sizeof(UiTreeSegment),&v->visible);
    if(s==0)s=ui_hash_new(sizeof(ScU32),&v->expanded);
    if(s==0)s=ui_hash_new(sizeof(UiCell *),&v->cells);
    if(s<0){ui_view_free(v);
        return s;
        }
    *out=v;
    return SC_UI_OK;
}
static void ui_view_destroy(UiNode *n)
{UiView *v=n->view;
    if(!v)return;
    n->view=0;
    v->retired=1;
    ui_provider_detach(v->binding);
    v->binding=0;
    if(!v->depth)ui_view_free(v);
    }
static void ui_view_enter(UiNode *n,UiView *v)
{n->owner->object.depth++;
    ui_node_hold(n);
    v->depth++;
    }
static void ui_view_leave(UiNode *n,UiView *v)
{ScUiContext *c=n->owner;
    if(v->depth)v->depth--;
    if(!v->depth && v->retired)ui_view_free(v);
    ui_node_leave(n);
    ui_object_leave(&c->object);
    }
ScStatus ScUiViewBind(ScUiContext *c,ScUiNode id,ScUiProvider *provider,ScUiSelection *selection)
{
    UiNode *n;
    UiView *candidate=0,*old;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    n=ui_node(c,id);
    if(!n || (n->kind!=SC_UI_TREE && n->kind!=SC_UI_LIST && n->kind!=SC_UI_TABLE && n->kind!=SC_UI_COMBOBOX)){s=SC_UI_E_INVALID;
        goto done;
        }
    if(!provider || provider->object.closing || (selection && selection->provider!=provider)){s=SC_UI_E_INVALID;
        goto done;
        }
    s=ui_view_new(n,&candidate);
    if(s<0)goto done;
    s=ScUiProviderRetain(provider);
    if(s<0)goto done;
    candidate->provider=provider;
    if(selection){s=ScUiSelectionRetain(selection);
        if(s<0)goto done;
        candidate->selection=selection;
        }
    else{s=ScUiSelectionCreate(provider,SC_UI_SELECTION_MULTI|SC_UI_SELECTION_ALLOW_EMPTY,&candidate->selection);
        if(s<0)goto done;
        }
    if(n->view && n->view->columns){ScContainerInfo info;
        ScU32 i,visible=0;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(n->view->columns,&info);
        s=ScArrayReserve(candidate->columns,info.length);
        if(s==0)s=ScArrayReserve(candidate->column_order,info.length);
        if(s<0)goto done;
        for(i=0;i<info.length;i++){UiColumn column;
            ScArrayGet(n->view->columns,i,&column);
            s=ScStringRetain(column.title);
            if(s<0)goto done;
            s=ScArrayInsert(candidate->columns,i,&column,1);
            if(s<0){ScStringRelease(column.title);
                goto done;
                }
            if(!(column.description.flags&SC_UI_COLUMN_HIDDEN)){s=ScArrayInsert(candidate->column_order,visible++,&i,1);
                if(s<0)goto done;
                }}
        candidate->columns_width=n->view->columns_width;
        }
    candidate->binding=(UiBinding *)calloc(1,sizeof(*candidate->binding));
    if(!candidate->binding){s=SC_UI_E_MEMORY;
        goto done;
        }
    candidate->binding->provider=provider;
    candidate->binding->context=c;
    candidate->binding->node=id;
    candidate->binding->next=provider->bindings;
    if(provider->bindings)provider->bindings->previous=candidate->binding;
    provider->bindings=candidate->binding;
    old=n->view;
    n->view=candidate;
    candidate=0;
    if(old){old->retired=1;
        ui_provider_detach(old->binding);
        old->binding=0;
        if(!old->depth)ui_view_free(old);
        }
    ui_node_layout_dirty(n);
    s=SC_UI_OK;
done:if(candidate)ui_view_free(candidate);
    s=ui_cause(c,s,UI_OP_PROVIDER,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiViewUnbind(ScUiContext *c,ScUiNode id)
{UiNode *n=ui_node(c,id);
    if(!n)return SC_UI_E_STALE;
    ui_view_destroy(n);
    ui_node_layout_dirty(n);
    return SC_UI_OK;
    }
ScStatus ScUiViewRefresh(ScUiContext *c,ScUiNode id,ScU32 revision)
{UiNode *n=ui_node(c,id);
    if(!n || !n->view)return SC_UI_E_STALE;
    if(revision && revision!=n->view->provider->revision)return SC_UI_E_STALE;
    n->view->revision=0;
    n->view->sort_pending=0;
    n->state&=~SC_UI_BUSY;
    ui_tree_effective(n);
    ui_node_damage(n);
    return SC_UI_OK;
    }
static ScStatus ui_provider_index(ScUiProvider *provider,ScU32 item,ScU32 parent,ScU32 *index,ScU32 *actual_parent)
{
    ScUiDataRequest q;
    ScUiDataReply r;
    ScStatus s;
    memset(&q,0,sizeof(q));
    q.size=sizeof(q);
    q.version=1;
    q.operation=SC_UI_DATA_INDEX_OF;
    q.item=item;
    q.parent=parent;
    memset(&r,0,sizeof(r));
    r.size=sizeof(r);
    r.version=1;
    s=ui_provider_call(provider,&q,&r);
    if(s==0){*index=r.total;
        *actual_parent=r.parent;
        }
    return s;
}
static ScStatus ui_tree_recount(ScArray *segments,ScU32 *total)
{
    ScContainerInfo info;
    ScU32 i,sum=0;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(segments,&info);
    for(i=0;i<info.length;i++){UiTreeSegment segment;
        ScArrayGet(segments,i,&segment);
        segment.start=sum;
        if(!ui_add(sum,segment.count,&sum))return SC_UI_E_RANGE;
        ScArraySet(segments,i,&segment);
        }
    *total=sum;
    return SC_UI_OK;
}
static ScStatus ui_tree_locate(UiView *v,ScArray *segments,ScU32 item,ScU32 *segment_index,ScU32 *offset)
{
    ScContainerInfo info;
    ScU32 i,index,parent;
    ScStatus s=ui_provider_index(v->provider,item,0,&index,&parent);
    if(s!=0)return s;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(segments,&info);
    for(i=0;i<info.length;i++){UiTreeSegment segment;
        ScArrayGet(segments,i,&segment);
        if(parent==segment.parent && index>=segment.first && index-segment.first<segment.count){*segment_index=i;
            *offset=index-segment.first;
            return SC_UI_OK;
            }
    }
    return SC_UI_E_NOT_FOUND;
}
static ScStatus ui_tree_expand_segments(UiView *v,ScArray *segments,ScU32 item)
{
    UiTreeSegment source,parts[3];
    ScU32 index,offset,count,revision,used=0;
    ScStatus s;
    s=ui_tree_locate(v,segments,item,&index,&offset);
    if(s<0)return s;
    s=ui_provider_count(v->provider,item,&count,&revision);
    if(s!=0)return s;
    if(!count)return SC_UI_END;
    ScArrayGet(segments,index,&source);
    parts[used]=source;
    parts[used++].count=offset+1;
    memset(&parts[used],0,sizeof(parts[used]));
    parts[used].parent=item;
    parts[used].count=count;
    parts[used].depth=source.depth+1;
    used++;
    if(source.count>offset+1){parts[used]=source;
        parts[used].first+=offset+1;
        parts[used].count-=offset+1;
        used++;
        }
    /* 两个追加槽先留足，之后erase/insert没有新的空间失败点。 */
    {ScContainerInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(segments,&info);
        s=ScArrayReserve(segments,info.length+2);
        if(s<0)return s;
        }
    s=ScArrayErase(segments,index,1);
    if(s==0)s=ScArrayInsert(segments,index,parts,used);
    return s;
}
static ScStatus ui_tree_candidate(UiView *v,ScHash *expanded,ScU32 count,ScU32 revision,ScArray **out,ScU32 *total)
{
    ScArray *candidate=0;
    ScStatus s=ui_array_new(sizeof(UiTreeSegment),&candidate);
    if(s<0)return s;
    if(count){UiTreeSegment segment;
        memset(&segment,0,sizeof(segment));
        segment.count=count;
        s=ScArrayInsert(candidate,0,&segment,1);
        if(s<0)goto done;
        }
    {ScContainerInfo information;
        ScU32 pass;
        memset(&information,0,sizeof(information));
        information.size=sizeof(information);
        information.version=1;
        ScHashQuery(expanded,&information);
        for(pass=0;pass<information.length;pass++){ScIterator it;
            ScHashEntry entry;
            ScU32 progress=0;
            memset(&it,0,sizeof(it));
            it.size=sizeof(it);
            it.version=1;
            ScHashIterate(expanded,&it);
            memset(&entry,0,sizeof(entry));
            entry.size=sizeof(entry);
            entry.version=1;
            while(ScHashNext(&it,&entry)==0){ScU32 id=*(const ScU32 *)entry.key,index,offset;
                ScContainerInfo info;
                ScU32 i,already=0;
                memset(&info,0,sizeof(info));
                info.size=sizeof(info);
                info.version=1;
                ScArrayQuery(candidate,&info);
                for(i=0;i<info.length;i++){UiTreeSegment segment;
                    ScArrayGet(candidate,i,&segment);
                    if(segment.parent==id){already=1;
                        break;
                        }}
                if(already || ui_tree_locate(v,candidate,id,&index,&offset)!=0)continue;
                s=ui_tree_expand_segments(v,candidate,id);
                if(s!=0 && s!=SC_UI_E_NOT_FOUND && s!=SC_UI_END)goto done;
                if(s==0)progress++;
                if(v->provider->revision!=revision || v->retired){s=SC_UI_E_STALE;
                    goto done;
                    }
            }
            if(!progress)break;
        }}
    s=ui_tree_recount(candidate,total);
    if(s<0)goto done;
    if(v->provider->revision!=revision || v->retired){s=SC_UI_E_STALE;
        goto done;
        }
    *out=candidate;
    candidate=0;
    s=SC_UI_OK;
done:ScArrayRelease(candidate);
    return s;
}
static void ui_view_scroll_limit(UiNode *,ScWideCounter *);
static ScStatus ui_view_prepare(UiNode *n)
{
    UiView *v=n->view;
    ScArray *candidate=0;
    ScU32 count,revision,total;
    ScStatus s;
    if(!v || !v->provider)return SC_UI_E_NOT_FOUND;
    ui_view_enter(n,v);
    s=ui_provider_count(v->provider,0,&count,&revision);
    if(s!=0)goto done;
    if(v->retired || (n->private_flags&UI_NODE_RETIRED)){s=SC_UI_E_CANCELLED;
        goto done;
        }
    if(v->revision==revision){s=SC_UI_OK;
        goto done;
        }
    if(!v->tree){v->total=count;
        v->revision=revision;
        v->sort_pending=0;
        n->state&=~SC_UI_BUSY;
        ui_tree_effective(n);
        s=SC_UI_OK;
        goto done;
        }
    s=ui_tree_candidate(v,v->expanded,count,revision,&candidate,&total);
    if(s!=0)goto done;
    ScArrayRelease(v->visible);
    v->visible=candidate;
    candidate=0;
    v->total=total;
    v->revision=revision;
    v->sort_pending=0;
    n->state&=~SC_UI_BUSY;
    ui_tree_effective(n);
    s=SC_UI_OK;
done:if(s==0 && !v->retired){ScWideCounter maximum;ui_view_scroll_limit(n,&maximum);}
    ScArrayRelease(candidate);
    s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
static ScStatus ui_view_row(UiNode *n,UiView *v,ScU32 row,UiVisibleItem *out)
{
    ScStatus s;
    ScU32 revision,parent=0,index=row,depth=0;
    if(row>=v->total)return SC_UI_END;
    if(v->tree){ScContainerInfo info;
        ScU32 lo=0,hi;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(v->visible,&info);
        hi=info.length;
        while(lo<hi){ScU32 mid=lo+(hi-lo)/2;
            UiTreeSegment segment;
            ScArrayGet(v->visible,mid,&segment);
            if(row>=segment.start+segment.count)lo=mid+1;
            else hi=mid;
            }
        if(lo>=info.length)return SC_UI_E_STALE;
        {UiTreeSegment segment;
            ScArrayGet(v->visible,lo,&segment);
            parent=segment.parent;
            index=segment.first+row-segment.start;
            depth=segment.depth;
            }}
    memset(out,0,sizeof(*out));
    s=ui_provider_item(v->provider,parent,index,&out->id,&revision,&out->state);
    if(s==0 && revision!=v->revision)return SC_UI_E_STALE;
    if(s==0){out->parent=parent;
        out->index=index;
        out->depth=depth;
        }
    (void)n;
    return s;
}
static ScStatus ui_view_item_position(UiNode *n,UiView *v,ScU32 item,ScU32 *position)
{
    ScU32 index,parent;
    ScStatus s;
    if(!v->tree){s=ui_provider_index(v->provider,item,0,&index,&parent);
        if(s==0 && !parent)*position=index;
        return s;
        }
    {ScU32 segment,offset;
        UiTreeSegment range;
        s=ui_tree_locate(v,v->visible,item,&segment,&offset);
        if(s==0){ScArrayGet(v->visible,segment,&range);
            *position=range.start+offset;
            }}
    (void)n;
    return s;
}
ScStatus ScUiTableSetColumns(ScUiContext *c,ScUiNode id,const ScUiColumnDesc *columns,ScU32 count)
{
    UiNode *n=ui_node(c,id);
    UiView *v;
    ScArray *candidate=0,*order=0;
    ScHash *ids=0;
    ScStatus s;
    ScU32 i,visible=0,mark=1;
    ScWideCounter width={0,0};
    if(!n || (count && !columns) || count>UINT_MAX/sizeof(*columns))return SC_UI_E_INVALID;
    if(!n->view){s=ui_view_new(n,&n->view);
        if(s<0)return s;
        }
    v=n->view;
    ui_view_enter(n,v);
    s=ui_array_new(sizeof(UiColumn),&candidate);
    if(s==0)s=ui_array_new(sizeof(ScU32),&order);
    if(s==0)s=ui_hash_new(sizeof(ScU32),&ids);
    if(s<0)goto done;
    s=ScArrayReserve(candidate,count);
    if(s==0)s=ScArrayReserve(order,count);
    if(s<0)goto done;
    for(i=0;i<count;i++){UiColumn column;
        ScU32 j;
        s=ui_input(&columns[i],sizeof(*columns));
        if(s<0)goto done;
        for(j=0;j<2;j++)if(columns[i].reserved[j]){s=SC_UI_E_INVALID;
            goto done;
            }
        if(!columns[i].column || columns[i].flags&~15u || columns[i].format>SC_UI_FORMAT_COUNTER || columns[i].sort_kind>SC_UI_SORT_COUNTER
            || columns[i].width<0 || columns[i].min_width<0 || columns[i].max_width<-1 || (columns[i].max_width>=0 && columns[i].min_width>columns[i].max_width)){s=SC_UI_E_INVALID;
            goto done;
            }
        memset(&column,0,sizeof(column));
        column.description=columns[i];
        s=ui_text_copy(&columns[i].title,&column.title);
        if(s<0)goto done;
        column.width=ui_clamp_size(c,ui_scale(c,columns[i].width),columns[i].min_width,columns[i].max_width);
        column.position=width;
        if(!(column.description.flags&SC_UI_COLUMN_HIDDEN)){ui_wide_add(&width,(ScU32)column.width);
            s=ScArrayInsert(order,visible++,&i,1);
            if(s<0){ScStringRelease(column.title);
                goto done;
                }}
        s=ScHashSet(ids,&columns[i].column,4,&mark,SC_HASH_INSERT_ONLY);
        if(s==0)s=ScArrayInsert(candidate,i,&column,1);
        if(s<0){ScStringRelease(column.title);
            goto done;
            }}
    if(v->retired){s=SC_UI_E_CANCELLED;
        goto done;
        }
    ui_columns_free(v->columns);
    ScArrayRelease(v->column_order);
    v->columns=candidate;
    candidate=0;
    v->column_order=order;
    order=0;
    v->columns_width=width;
    n->content_width=width;
    ui_node_layout_dirty(n);
    s=SC_UI_OK;
done:ui_columns_free(candidate);
    ScArrayRelease(order);
    ScHashRelease(ids);
    s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
static ScStatus ui_column_find(UiView *v,ScU32 id,UiColumn *out,ScU32 *index)
{
    ScContainerInfo info;
    ScU32 i;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(v->columns,&info);
    for(i=0;i<info.length;i++){ScArrayGet(v->columns,i,out);
        if(out->description.column==id){*index=i;
            return SC_UI_OK;
            }}
    return SC_UI_E_NOT_FOUND;
}
static void ui_column_at(UiView *v,ScU32 ordinal,UiColumn *out)
{ScU32 index;
    ScArrayGet(v->column_order,ordinal,&index);
    ScArrayGet(v->columns,index,out);
    }
static ScI32 ui_column_x(UiNode *n,const UiColumn *column)
{
    ScWideCounter distance;
    const ScWideCounter *a=&column->position,*b=&n->scroll_x;
    int negative=ui_wide_compare(a,b)<0;
    if(negative){a=&n->scroll_x;
        b=&column->position;
        }
    distance.lo=a->lo-b->lo;
    distance.hi=a->hi-b->hi-(a->lo<b->lo);
    if(distance.hi || distance.lo>(ScU32)INT_MAX)return negative?-INT_MAX:INT_MAX;
    return negative?-(ScI32)distance.lo:(ScI32)distance.lo;
}
static void ui_column_range(UiNode *n,UiView *v,ScU32 *first,ScU32 *past)
{
    ScContainerInfo info;
    ScU32 lo=0,hi;
    ScWideCounter edge=n->scroll_x;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(v->column_order,&info);
    hi=info.length;
    /* 隐藏列不占display索引；水平滚动同样只工作于可见列。 */
    while(lo<hi){ScU32 mid=lo+(hi-lo)/2;
        UiColumn column;
        ScWideCounter right;
        ui_column_at(v,mid,&column);
        right=column.position;
        ui_wide_add(&right,(ScU32)column.width);
        if(ui_wide_compare(&right,&n->scroll_x)<=0)lo=mid+1;
        else hi=mid;
        }
    *first=lo;
    ui_wide_add(&edge,n->rectangle.width>0?(ScU32)n->rectangle.width:0);
    hi=info.length;
    while(lo<hi){ScU32 mid=lo+(hi-lo)/2;
        UiColumn column;
        ui_column_at(v,mid,&column);
        if(ui_wide_compare(&column.position,&edge)<0)lo=mid+1;
        else hi=mid;
        }
    *past=lo;
}
ScStatus ScUiTableSetColumnWidth(ScUiContext *c,ScUiNode id,ScU32 column,ScI32 width)
{
    UiNode *n=ui_node(c,id);
    UiColumn d;
    ScU32 index;
    ScStatus s;
    if(!n || !n->view)return SC_UI_E_STALE;
    if(width<0)return SC_UI_E_RANGE;
    s=ui_column_find(n->view,column,&d,&index);
    if(s<0)return s;
    d.description.width=width;
    d.width=ui_clamp_size(c,ui_scale(c,width),d.description.min_width,d.description.max_width);
    {ScContainerInfo info;
        ScWideCounter position={0,0};
        ScU32 i;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScArrayQuery(n->view->columns,&info);
        if(s<0)return ui_cause(c,s,UI_OP_PROVIDER,id);
        if(info.length>=UINT_MAX-info.generation)return SC_UI_E_RANGE;
        /* 宽度改变只改已分配记录；不会在鼠标resize热路径分配一份表。 */
        for(i=0;i<info.length;i++){UiColumn item;
            ScArrayGet(n->view->columns,i,&item);
            if(i==index)item=d;
            item.position=position;
            if(!(item.description.flags&SC_UI_COLUMN_HIDDEN))ui_wide_add(&position,(ScU32)item.width);
            s=ScArraySet(n->view->columns,i,&item);
            if(s<0)return ui_cause(c,s,UI_OP_PROVIDER,id);
            }
        n->view->columns_width=position;
        n->content_width=position;
        }
    ui_node_damage(n);
    return SC_UI_OK;
}
ScStatus ScUiTableSort(ScUiContext *c,ScUiNode id,ScU32 column,ScU32 descending)
{
    UiNode *n=ui_node(c,id);
    UiView *v;
    UiColumn d;
    ScU32 index;
    ScUiDataRequest q;
    ScUiDataReply r;
    ScStatus s;
    if(!n || !n->view || !n->view->provider)return SC_UI_E_STALE;
    if(descending>1)return SC_UI_E_INVALID;
    v=n->view;
    ui_view_enter(n,v);
    s=ui_column_find(v,column,&d,&index);
    if(s<0)goto done;
    if(!(d.description.flags&SC_UI_COLUMN_SORTABLE)){s=SC_UI_E_INVALID;
        goto done;
        }
    memset(&q,0,sizeof(q));
    q.size=sizeof(q);
    q.version=1;
    q.operation=SC_UI_DATA_SORT;
    q.column=column;
    q.field=d.description.field;
    q.flags=descending|(d.description.sort_kind<<8);
    memset(&r,0,sizeof(r));
    r.size=sizeof(r);
    r.version=1;
    s=ui_provider_call(v->provider,&q,&r);
    if(s==0){v->sort_column=column;
        v->descending=descending;
        v->revision=0;
        v->sort_pending=0;
        n->state&=~SC_UI_BUSY;
        ui_tree_effective(n);
        ui_node_damage(n);
        }
    else if(s==SC_UI_PENDING){v->sort_pending=1;
        n->state|=SC_UI_BUSY;
        ui_tree_effective(n);
        ui_node_damage(n);
        }
done:s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
ScStatus ScUiTreeExpand(ScUiContext *c,ScUiNode id,ScU32 item,ScU32 expanded)
{
    UiNode *n=ui_node(c,id);
    UiView *v;
    ScHash *candidate=0;
    ScArray *segments=0;
    ScStatus s;
    ScU32 value=1,count,revision,total;
    if(!n || !n->view || n->kind!=SC_UI_TREE)return SC_UI_E_INVALID;
    if(expanded>1 || !item)return SC_UI_E_INVALID;
    v=n->view;
    ui_view_enter(n,v);
    s=ui_view_prepare(n);
    if(s!=0)goto done;
    s=ui_hash_new(sizeof(ScU32),&candidate);
    if(s<0)goto done;
    {ScU32 position,parent;
        s=ui_provider_index(v->provider,item,0,&position,&parent);
        if(s!=0)goto done;
        }
    {ScIterator it;
        ScHashEntry entry;
        memset(&it,0,sizeof(it));
        it.size=sizeof(it);
        it.version=1;
        ScHashIterate(v->expanded,&it);
        memset(&entry,0,sizeof(entry));
        entry.size=sizeof(entry);
        entry.version=1;
        while(ScHashNext(&it,&entry)==0){s=ScHashSet(candidate,entry.key,entry.key_bytes,entry.value,0);
            if(s<0)goto done;
            }}
    if(expanded)s=ScHashSet(candidate,&item,4,&value,0);
    else{ScHashEntry e;
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        s=ScHashGet(candidate,&item,4,&e);
        if(s==0)s=ScHashRemove(candidate,&item,4);
        else s=SC_UI_OK;
        }
    if(s<0)goto done;
    s=ui_provider_count(v->provider,0,&count,&revision);
    if(s!=0)goto done;
    s=ui_tree_candidate(v,candidate,count,revision,&segments,&total);
    if(s!=0)goto done;
    /* 展开状态和段索引一起提交，OOM不会留一个假展开的旧显示。 */
    ScHashRelease(v->expanded);
    ScArrayRelease(v->visible);
    v->expanded=candidate;
    candidate=0;
    v->visible=segments;
    segments=0;
    v->total=total;
    v->revision=revision;
    ui_node_damage(n);
    s=SC_UI_OK;
done:ScArrayRelease(segments);
    ScHashRelease(candidate);
    s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
static ScU32 ui_view_row_height(UiNode *n)
{ScUiMetrics m;
    memset(&m,0,sizeof(m));
    m.size=sizeof(m);
    m.version=1;
    ScUiMetricsQuery(n->owner,n->root,&m);
    return m.row_height?m.row_height:1;
    }
static ScU32 ui_pair_divide_row(const ScWideCounter *value,ScU32 divisor,ScU32 *remainder)
{
    ScU32 q=0,r=0;
    ScI32 bit;
    int overflow=0;
    for(bit=63;bit>=0;bit--){ScU32 carry=r>>31;
        r=(r<<1)|(bit>=32?((value->hi>>(bit-32))&1u):((value->lo>>bit)&1u));
        if(carry || r>=divisor){r-=divisor;
            if(bit>=32)overflow=1;
            else q|=1u<<bit;
            }}
    *remainder=r;
    return overflow?UINT_MAX:q;
}
static void ui_pair_row(ScU32 row,ScU32 height,ScWideCounter *out)
{
    ScU32 a=row&65535u,b=row>>16,lo=a*height,hi=b*height;
    out->lo=lo+(hi<<16);
    out->hi=(hi>>16)+(out->lo<lo);
}
static void ui_view_scroll_limit(UiNode *n,ScWideCounter *maximum)
{
    ScU32 height=ui_view_row_height(n),viewport=n->rectangle.height>0?(ScU32)n->rectangle.height:0;
    ui_pair_row(n->view->total,height,&n->content_height);
    /* 表头固定占一行。上限是内容减可见区域，目录缩短或窗口变大也立即
     * 归位；不能只处理滚轮向上减法，任由向下偏移无限增长成空白页。 */
    if(n->kind==SC_UI_TABLE)ui_wide_add(&n->content_height,height);
    *maximum=n->content_height;
    if(!maximum->hi && maximum->lo<=viewport)maximum->lo=0;
    else{ScU32 old=maximum->lo;maximum->lo-=viewport;if(old<viewport)maximum->hi--;}
    if(ui_wide_compare(&n->scroll_y,maximum)>0)n->scroll_y=*maximum;
}
static void ui_view_changed(UiNode *n,const ScUiDataChange *change)
{
    UiView *v=n->view;
    ScU32 row,height,offset,remainder;
    ScStatus s;
    ScUiRect area;
    if(!v)return;
    if(change->kind!=SC_UI_DATA_UPDATE || !v->revision || v->sort_pending){v->revision=0;
        ui_node_layout_dirty(n);
        return;
        }
    /* 字段变化不重建十万项的展开索引；revision只失效实际可见cell。 */
    v->revision=change->revision;
    if(!change->item){ui_node_damage(n);
        return;
        }
    ui_view_enter(n,v);
    s=ui_view_item_position(n,v,change->item,&row);
    height=ui_view_row_height(n);
    offset=ui_pair_divide_row(&n->scroll_y,height,&remainder);
    if(v->retired || (n->private_flags&UI_NODE_RETIRED)){ui_view_leave(n,v);
        return;
        }
    if(s==0 && row>=offset && row-offset<=(ScU32)(n->rectangle.height>0?n->rectangle.height:0)/height){ScI32 delta=(ScI32)((row-offset)*height)-(ScI32)remainder;
        if(n->kind==SC_UI_TABLE)delta+=(ScI32)height;
        area=n->rectangle;
        if(ui_signed_add(area.y,delta,&area.y)){area.height=(ScI32)height;
            ui_rect_intersection(&area,&n->clip,&area);
            ui_damage(ui_root(n->owner,n->id),&area);
            }
        else ui_node_damage(n);
    }
    else if(s!=0 && s!=SC_UI_E_NOT_FOUND)ui_node_damage(n);
    ui_view_leave(n,v);
}
static ScStatus ui_tree_ensure(UiNode *n,UiView *v,ScU32 item,ScU32 *row)
{
    ScHash *candidate=0,*seen=0;
    ScArray *segments=0;
    ScU32 current=item,index,parent,count,revision,total,mark=1;
    ScStatus s;
    s=ui_hash_new(sizeof(ScU32),&candidate);
    if(s==0)s=ui_hash_new(sizeof(ScU32),&seen);
    if(s<0)goto done;
    {ScIterator it;
        ScHashEntry e;
        memset(&it,0,sizeof(it));
        it.size=sizeof(it);
        it.version=1;
        ScHashIterate(v->expanded,&it);
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        while(ScHashNext(&it,&e)==0){s=ScHashSet(candidate,e.key,e.key_bytes,e.value,0);
            if(s<0)goto done;
            }}
    revision=v->provider->revision;
    while(current){s=ScHashSet(seen,&current,4,&mark,SC_HASH_INSERT_ONLY);
        if(s<0){if(s==SC_UTIL_E_EXISTS)s=SC_UI_E_INVALID;
            goto done;
            }
        s=ui_provider_index(v->provider,current,0,&index,&parent);
        if(s!=0)goto done;
        if(parent){s=ScHashSet(candidate,&parent,4,&mark,0);
            if(s<0)goto done;
            }
        current=parent;
        if(v->retired || v->provider->revision!=revision){s=SC_UI_E_STALE;
            goto done;
            }}
    s=ui_provider_count(v->provider,0,&count,&revision);
    if(s!=0)goto done;
    s=ui_tree_candidate(v,candidate,count,revision,&segments,&total);
    if(s!=0)goto done;
    {ScU32 segment,offset;
        UiTreeSegment range;
        s=ui_tree_locate(v,segments,item,&segment,&offset);
        if(s!=0)goto done;
        ScArrayGet(segments,segment,&range);
        *row=range.start+offset;
        }
    ScHashRelease(v->expanded);
    ScArrayRelease(v->visible);
    v->expanded=candidate;
    candidate=0;
    v->visible=segments;
    segments=0;
    v->total=total;
    v->revision=revision;
    ui_node_damage(n);
    s=SC_UI_OK;
done:ScArrayRelease(segments);
    ScHashRelease(candidate);
    ScHashRelease(seen);
    return s;
}
ScStatus ScUiViewEnsureItem(ScUiContext *c,ScUiNode id,ScU32 item)
{
    UiNode *n=ui_node(c,id);
    UiView *v;
    ScU32 row,height,offset,visible;
    ScStatus s;
    if(!n || !n->view)return SC_UI_E_STALE;
    v=n->view;
    ui_view_enter(n,v);
    s=ui_view_prepare(n);
    if(s!=0)goto done;
    s=ui_view_item_position(n,v,item,&row);
    if(s==SC_UI_E_NOT_FOUND && v->tree)s=ui_tree_ensure(n,v,item,&row);
    if(s!=0)goto done;
    height=ui_view_row_height(n);
    offset=ui_pair_divide_row(&n->scroll_y,height,&visible);
    visible=(ScU32)(n->rectangle.height>0?n->rectangle.height:0)/height;
    if(n->kind==SC_UI_TABLE && visible)visible--;
    if(row<offset)ui_pair_row(row,height,&n->scroll_y);
    else if(!visible || row-offset>=visible)ui_pair_row(visible?row-visible+1:row,height,&n->scroll_y);
    {ScWideCounter maximum;ui_view_scroll_limit(n,&maximum);}
    ui_node_damage(n);
    s=SC_UI_OK;
done:s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
ScStatus ScUiViewHitTest(ScUiContext *c,ScUiNode id,ScI32 x,ScI32 y,ScU32 *item,ScU32 *column)
{
    UiNode *n=ui_node(c,id);
    UiView *v;
    UiVisibleItem row;
    ScU32 height,offset,remainder,index;
    ScStatus s;
    if(!n || !n->view || !item || !column)return SC_UI_E_INVALID;
    if(x<0 || y<0 || x>=n->rectangle.width || y>=n->rectangle.height)return SC_UI_E_NOT_FOUND;
    v=n->view;
    ui_view_enter(n,v);
    s=ui_view_prepare(n);
    if(s!=0)goto done;
    height=ui_view_row_height(n);
    if(n->kind==SC_UI_TABLE){if((ScU32)y<height){s=SC_UI_E_NOT_FOUND;
            goto done;
            }
        y-=(ScI32)height;
        }
    offset=ui_pair_divide_row(&n->scroll_y,height,&remainder);
    index=offset+((ScU32)y+remainder)/height;
    s=ui_view_row(n,v,index,&row);
    if(s!=0)goto done;
    *item=row.id;
    *column=0;
    {ScU32 i,first,past;
        ui_column_range(n,v,&first,&past);
        for(i=first;i<past;i++){UiColumn col;
            ScI32 left,right;
            ui_column_at(v,i,&col);
            left=ui_column_x(n,&col);
            right=left>INT_MAX-col.width?INT_MAX:left+col.width;
            if(x>=left && x<right){*column=col.description.column;
                break;
                }}}
done:s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
static ScStatus ui_value_text(const ScValue *value,ScString **out)
{
    if(value->type==SC_VALUE_UTF8)return ScStringCreate(value->data,value->bytes,out);
    if(value->type==SC_VALUE_BYTES)return ScFormatHex(value->data,value->bytes,0,out);
    {char buffer[64];
        int length;
        if(value->type==SC_VALUE_I32)length=snprintf(buffer,sizeof(buffer),"%d",(ScI32)value->lo);
        else if(value->type==SC_VALUE_COUNTER)length=snprintf(buffer,sizeof(buffer),"%08X:%08X",value->hi,value->lo);
        else if(value->type==SC_VALUE_BOOL)length=snprintf(buffer,sizeof(buffer),"%s",value->lo?"Yes":"No");
        else if(value->type==SC_VALUE_NULL)length=0;
        else length=snprintf(buffer,sizeof(buffer),"%u",value->lo);
        if(length<0)return SC_UI_E_FAILED;
        return ScStringCreate(buffer,(ScU32)length,out);
        }
}
static ScStatus ui_view_cell_text(UiView *v,ScU32 item,ScU32 field,ScU32 format,ScString **out)
{
    UiCellKey key;
    ScHashEntry e;
    UiCell *cell;
    ScValue value;
    ScStatus s;
    key.item=item;
    key.field=field;
    key.revision=v->revision;
    key.format=format;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    if(ScHashGet(v->cells,&key,sizeof(key),&e)==0){cell=*(UiCell * const *)e.value;
        cell->stamp=v->cell_stamp;
        *out=cell->text;
        return SC_UI_OK;
        }
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    s=ui_provider_cell(v->provider,item,field,&value);
    if(s!=0)return s;
    cell=(UiCell *)calloc(1,sizeof(*cell));
    if(!cell)return SC_UI_E_MEMORY;
    if(format==SC_UI_FORMAT_HEX || format==SC_UI_FORMAT_I32 || format==SC_UI_FORMAT_U32){char number[16];
        int count;
        if(format==SC_UI_FORMAT_HEX)count=snprintf(number,sizeof(number),"%08X",value.lo);
        else if(format==SC_UI_FORMAT_I32)count=snprintf(number,sizeof(number),"%d",(ScI32)value.lo);
        else count=snprintf(number,sizeof(number),"%u",value.lo);
        s=count<0?SC_UI_E_FAILED:ScStringCreate(number,(ScU32)count,&cell->text);
        }
    else if(format==SC_UI_FORMAT_BYTES && value.type==SC_VALUE_U32)s=ScFormatBytes(value.lo,&cell->text);
    else s=ui_value_text(&value,&cell->text);
    if(s<0){free(cell);
        return s;
        }
    cell->stamp=v->cell_stamp;
    s=ScHashSet(v->cells,&key,sizeof(key),&cell,SC_HASH_INSERT_ONLY);
    if(s<0){ScStringRelease(cell->text);
        free(cell);
        return s;
        }
    v->cell_count++;
    *out=cell->text;
    return SC_UI_OK;
}
static ScStatus ui_view_cells_prune(const ScHashEntry *e,void *context)
{
    UiView *v=(UiView *)context;
    UiCell *cell=*(UiCell * const *)e->value;
    if(((const UiCellKey *)e->key)->revision!=v->revision || (cell->stamp!=v->cell_stamp && v->cell_count>v->cell_limit)){
        ScStatus s=ScHashRemove(v->cells,e->key,e->key_bytes);
        if(s<0)return s;
        ScStringRelease(cell->text);
        free(cell);
        if(v->cell_count)v->cell_count--;
        }
    return SC_UI_OK;
}
static ScStatus ui_view_paint(UiNode *n,ScUiPainter *paint)
{
    UiView *v=n->view;
    ScUiRect whole,rowrect;
    ScStatus s;
    ScU32 height,offset,remainder,row_count,r,col_count,first,past;
    ScContainerInfo info;
    ScUiIcon *row_icon=0;
    whole.x=0;
    whole.y=0;
    whole.width=n->rectangle.width;
    whole.height=n->rectangle.height;
    ui_fill(paint,&whole,SC32_COLOR_PAPER);
    ui_border(paint,&whole,1);
    if(!v || !v->provider)return SC_UI_OK;
    ui_view_enter(n,v);
    s=ui_view_prepare(n);
    if(s==SC_UI_PENDING){ScUiText loading;
        loading.size=sizeof(loading);
        loading.version=1;
        loading.data="Loading...";
        loading.bytes=10;
        paint->text_role=SC32_COLOR_MUTED;
        s=ScUiPainterText(paint,&whole,&loading,SC_UI_FONT_BODY,0);
        goto done;
        }
    if(s!=0)goto done;
    v->cell_stamp++;
    if(!v->cell_stamp)v->cell_stamp=1;
    height=ui_view_row_height(n);
    offset=ui_pair_divide_row(&n->scroll_y,height,&remainder);
    row_count=(ScU32)(n->rectangle.height>0?n->rectangle.height:0)/height+2;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(v->columns,&info);
    col_count=info.length;
    ui_column_range(n,v,&first,&past);
    if(!ui_multiply(row_count,col_count?past-first:1,&v->cell_limit) || v->cell_limit>UINT_MAX/2){s=SC_UI_E_RANGE;
        goto done;
        }
    v->cell_limit*=2;
    {ScContainerInfo cache;
        memset(&cache,0,sizeof(cache));
        cache.size=sizeof(cache);
        cache.version=1;
        ScHashQuery(v->cells,&cache);
        v->cell_count=cache.length;
        }
    if(n->kind==SC_UI_TABLE){ScU32 i;
        for(i=first;i<past;i++){UiColumn col;
            ScUiText title;
            ScUiRect rect;
            ui_column_at(v,i,&col);
            rect.x=ui_column_x(n,&col);
            rect.y=0;
            rect.width=col.width;
            rect.height=(ScI32)height;
            ui_fill(paint,&rect,SC32_COLOR_FACE);
            ui_border(paint,&rect,0);
            ui_text_view(col.title,&title);
            rect.x+=ui_scale(n->owner,4);
            rect.width-=ui_scale(n->owner,8);
            if(v->sort_column==col.description.column){ScI32 ax=ui_rect_right(&rect)-ui_scale(n->owner,10),ay=(ScI32)height/2,d;
                rect.width-=ui_scale(n->owner,16);
                for(d=0;d<ui_scale(n->owner,4);d++)ScUiPainterLine(paint,ax-d,ay+(v->descending?d:-d),ax+d,ay+(v->descending?d:-d),SC32_COLOR_TEXT);
                }
            paint->text_role=SC32_COLOR_TEXT;
            s=ScUiPainterText(paint,&rect,&title,SC_UI_FONT_BODY,UI_TEXT_ELLIPSIS);
            if(s<0)goto done;
            }}
    for(r=0;r<row_count && r<v->total && offset<v->total-r;r++){UiVisibleItem item;
        ScU32 col;
        ScI32 local_y=(ScI32)(r*height)-(ScI32)remainder+(n->kind==SC_UI_TABLE?(ScI32)height:0);
        if(n->rectangle.y+local_y+(ScI32)height<=paint->clip.y || n->rectangle.y+local_y>=paint->clip.y+paint->clip.height)continue;
        s=ui_view_row(n,v,offset+r,&item);
        if(s!=0)goto done;
        rowrect.x=1;
        rowrect.y=(ScI32)(r*height)-(ScI32)remainder+(n->kind==SC_UI_TABLE?(ScI32)height:0);
        rowrect.width=whole.width-2;
        rowrect.height=(ScI32)height;
        if(ui_selection_has(v->selection,item.id))ui_fill(paint,&rowrect,SC32_COLOR_SELECT);
        paint->text_role=(item.state&n->effective&SC_UI_ENABLED)?ui_selection_has(v->selection,item.id)?SC32_COLOR_SELECT_TEXT:SC32_COLOR_TEXT:SC32_COLOR_MUTED;
        {ScUiDataRequest q;
            ScUiDataReply reply;
            memset(&q,0,sizeof(q));
            q.size=sizeof(q);
            q.version=1;
            q.operation=SC_UI_DATA_ITEM;
            q.item=item.id;
            q.revision=v->revision;
            memset(&reply,0,sizeof(reply));
            reply.size=sizeof(reply);
            reply.version=1;
            s=ui_provider_call(v->provider,&q,&reply);
            if(s!=0)goto done;
            if(reply.icon){s=ScUiIconRetain(reply.icon);
                if(s<0)goto done;
                row_icon=reply.icon;
                }}
        for(col=col_count?first:0;col<(col_count?past:1);col++){UiColumn column;
            ScString *text=0;
            ScUiText label;
            ScUiRect cell=rowrect;
            ScU32 field=0,flags=UI_TEXT_ELLIPSIS;
            memset(&column,0,sizeof(column));
            if(col_count){ui_column_at(v,col,&column);
                field=column.description.field;
                cell.x=ui_column_x(n,&column);
                cell.width=column.width;
                if(column.description.flags&SC_UI_COLUMN_RIGHT)flags|=UI_TEXT_RIGHT;
                }
            if(v->tree){ScI32 indent=(ScI32)item.depth*ui_scale(n->owner,16);
                cell.x+=indent+ui_scale(n->owner,16);
                cell.width-=indent+ui_scale(n->owner,16);
                if(!col){ScI32 ax=cell.x-ui_scale(n->owner,12),ay=cell.y+(ScI32)height/2;
                    ScU32 children,revision;
                    ScStatus query=ui_provider_count(v->provider,item.id,&children,&revision);
                    if(query==0 && children){ScHashEntry e;
                        ScUiRect box;
                        box.x=ax-ui_scale(n->owner,3);
                        box.y=ay-ui_scale(n->owner,3);
                        box.width=box.height=ui_scale(n->owner,7);
                        ui_border(paint,&box,0);
                        ScUiPainterLine(paint,box.x+1,ay,box.x+box.width-2,ay,paint->text_role);
                        memset(&e,0,sizeof(e));
                        e.size=sizeof(e);
                        e.version=1;
                        if(ScHashGet(v->expanded,&item.id,4,&e)<0)ScUiPainterLine(paint,ax,box.y+1,ax,box.y+box.height-2,paint->text_role);
                        }}}
            if(!col && row_icon){ScUiRect icon=cell;
                icon.x+=ui_scale(n->owner,3);
                icon.y+=(icon.height-ui_scale(n->owner,16))/2;
                icon.width=icon.height=ui_scale(n->owner,16);
                s=ScUiPainterIcon(paint,&icon,row_icon,item.state&n->effective);
                if(s<0)goto done;
                cell.x+=ui_scale(n->owner,22);
                cell.width-=ui_scale(n->owner,22);
                }
            cell.x+=ui_scale(n->owner,4);
            cell.width-=ui_scale(n->owner,8);
            s=ui_view_cell_text(v,item.id,field,column.description.format,&text);
            if(s!=0)goto done;
            ui_text_view(text,&label);
            s=ScUiPainterText(paint,&cell,&label,SC_UI_FONT_BODY,flags);
            if(s<0)goto done;
            if(v->retired || (n->private_flags&UI_NODE_RETIRED)){s=SC_UI_E_CANCELLED;
                goto done;
                }
        }
        ScUiIconRelease(row_icon);
        row_icon=0;
    }
    ui_pair_row(v->total,height,&n->content_height);
    if(n->kind==SC_UI_TABLE)ui_wide_add(&n->content_height,height);
    n->content_width=v->columns_width;
    if(!col_count){n->content_width.lo=(ScU32)(whole.width>0?whole.width:0);
        n->content_width.hi=0;
        }
    ScHashVisit(v->cells,ui_view_cells_prune,v);
    s=SC_UI_OK;
done:ScUiIconRelease(row_icon);
    if(s>0)s=SC_UI_E_BUSY;
    s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
static ScStatus ui_view_select(UiNode *n,UiView *v,ScU32 item,ScU32 modifiers)
{
    ScStatus s;
    if((modifiers&SC32_MOD_SHIFT) && v->selection->anchor && v->tree){ScU32 first,last,count,i,*ids=0,bytes;
        s=ui_view_item_position(n,v,v->selection->anchor,&first);
        if(s<0)return s;
        s=ui_view_item_position(n,v,item,&last);
        if(s<0)return s;
        if(first>last){ScU32 swap=first;
            first=last;
            last=swap;
            }
        count=last-first+1;
        if(!ui_multiply(count,4,&bytes))return SC_UI_E_RANGE;
        ids=(ScU32 *)malloc(bytes);
        if(!ids)return SC_UI_E_MEMORY;
        for(i=0;i<count;i++){UiVisibleItem row;
            s=ui_view_row(n,v,first+i,&row);
            if(s!=0){free(ids);
                return s;
                }
            ids[i]=row.id;
            }
        s=ui_selection_ids(v->selection,ids,count,(modifiers&SC32_MOD_CONTROL)?SC_UI_ADD:SC_UI_REPLACE);
        free(ids);
    }
    else if((modifiers&SC32_MOD_SHIFT) && v->selection->anchor)s=ScUiSelectionRange(v->selection,v->selection->anchor,item,(modifiers&SC32_MOD_CONTROL)?SC_UI_ADD:SC_UI_REPLACE);
    else s=ScUiSelectionSet(v->selection,item,(modifiers&SC32_MOD_CONTROL)?SC_UI_TOGGLE:SC_UI_REPLACE);
    if(s==0){ui_node_damage(n);
        ScUiViewEnsureItem(n->owner,n->id,item);
        }
    return s;
}
static int ui_view_all_current(UiNode *n,UiView *v,ScU32 revision,ScU32 selection_revision,ScU32 total)
{
    return !v->retired && !(n->private_flags&UI_NODE_RETIRED) && n->view==v
        && !n->owner->object.closing && !v->selection->object.closing
        && (n->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))==(SC_UI_VISIBLE|SC_UI_ENABLED)
        && v->revision==revision && v->provider->revision==revision
        && v->selection->revision==selection_revision && v->total==total;
}
static ScStatus ui_view_select_all(UiNode *n,UiView *v)
{
    ScU32 revision=v->revision,selection_revision=v->selection->revision,total=v->total;
    ScStatus s;
    if(!ui_view_all_current(n,v,revision,selection_revision,total))return SC_UI_E_STALE;
    if(!total)return ScUiSelectionClear(v->selection);
    if(!v->tree){UiVisibleItem first,last;
        s=ui_view_row(n,v,0,&first);
        if(s==0 && !ui_view_all_current(n,v,revision,selection_revision,total))s=SC_UI_E_STALE;
        if(s==0)s=ui_view_row(n,v,total-1,&last);
        if(s==0 && !ui_view_all_current(n,v,revision,selection_revision,total))s=SC_UI_E_STALE;
        /* root0不是可选ID；完整平表范围已有一次提交的universe/例外路径。 */
        if(s==0)s=ScUiSelectionRange(v->selection,first.id,last.id,SC_UI_REPLACE);
        return s;
    }
    ScArray *segments=0;
    ScU32 *ids=0,bytes=0,used=0;
    ScView source,current;
    UiView rows=*v;
    memset(&source,0,sizeof(source));
    source.size=sizeof(source);
    source.version=1;
    s=ScArrayView(v->visible,&source);
    if(s==0)s=ui_array_new(sizeof(UiTreeSegment),&segments);
    if(s==0)s=ScArrayInsert(segments,0,source.data,source.bytes/sizeof(UiTreeSegment));
    if(s==0 && !ui_multiply(total,sizeof(*ids),&bytes))s=SC_UI_E_RANGE;
    if(s==0){ids=(ScU32 *)malloc(bytes);
        if(!ids)s=SC_UI_E_MEMORY;
    }
    rows.visible=segments;
    /* 只复制展开分支的段索引与ID候选，不为每个数据项建立控件。
     * provider回调可重入改变Tree，枚举始终读拥有的旧段，提交前再核当前段。 */
    for(ScU32 i=0;s==0 && i<total;i++){UiVisibleItem item;
        s=ui_view_row(n,&rows,i,&item);
        if(s==0 && !ui_view_all_current(n,v,revision,selection_revision,total))s=SC_UI_E_STALE;
        if(s==0 && (item.state&SC_UI_ENABLED))ids[used++]=item.id;
        if((i&4095u)==4095u)ScProcessYield();
    }
    memset(&current,0,sizeof(current));
    current.size=sizeof(current);
    current.version=1;
    if(s==0)s=ScArrayView(v->visible,&current);
    if(s==0)s=ScArrayView(segments,&source);
    if(s==0 && (current.bytes!=source.bytes || (source.bytes && memcmp(current.data,source.data,source.bytes))))s=SC_UI_E_STALE;
    if(s==0)s=ui_selection_ids(v->selection,ids,used,SC_UI_REPLACE);
    free(ids);
    ScArrayRelease(segments);
    return s;
}
static ScStatus ui_view_event(UiNode *n,const ScUiEvent *event)
{
    UiView *v=n->view;
    ScStatus s=0;
    ScU32 item,column;
    if(!v || !v->provider)return SC_UI_OK;
    if(event->type==SC_UI_EVENT_CAPTURE_LOST || event->type==SC_UI_EVENT_INPUT_SYNC || event->type==SC_UI_EVENT_FOCUS_OUT){v->resize_column=UINT_MAX;
        return SC_UI_OK;
        }
    if(event->type!=SC_UI_EVENT_POINTER_DOWN && event->type!=SC_UI_EVENT_POINTER_MOVE && event->type!=SC_UI_EVENT_POINTER_UP && event->type!=SC_UI_EVENT_KEY_DOWN && event->type!=SC_UI_EVENT_WHEEL && event->type!=SC_UI_EVENT_TEXT)return SC_UI_OK;
    ui_view_enter(n,v);
    s=ui_view_prepare(n);
    if(s!=0)goto done;
    if(event->type==SC_UI_EVENT_POINTER_DOWN){ScU32 height=ui_view_row_height(n);
        if(event->message && !(event->message->data[3]&1u))goto done;
        if(v->editor){s=ScUiTableEndEdit(n->owner,n->id,1);
            if(s!=0)goto handled;
            if(v->retired){s=SC_UI_E_CANCELLED;
                goto done;
                }}
        if(n->kind==SC_UI_TABLE && event->y>=0 && (ScU32)event->y<height){ScU32 i,first,past;
            ui_column_range(n,v,&first,&past);
            for(i=first;i<past;i++){UiColumn col;
                ScI32 x;
                ui_column_at(v,i,&col);
                x=ui_column_x(n,&col);
                if(event->x>=x+col.width-ui_scale(n->owner,3) && event->x<=x+col.width+ui_scale(n->owner,3)){s=ScUiCapture(n->owner,n->id,1);
                    if(s==0){v->resize_column=col.description.column;
                        v->resize_origin=event->x;
                        v->resize_width=col.width;
                        }
                    goto handled;
                    }
                if(event->x>=x && event->x<x+col.width){if(col.description.flags&SC_UI_COLUMN_SORTABLE)s=ScUiTableSort(n->owner,n->id,col.description.column,v->sort_column==col.description.column?!v->descending:0);
                    goto handled;
                    }}
            goto handled;
            }
        s=ScUiViewHitTest(n->owner,n->id,event->x,event->y,&item,&column);
        if(s!=0)goto done;
        if(v->tree){ScU32 row;
            UiVisibleItem current;
            s=ui_view_item_position(n,v,item,&row);
            if(s==0)s=ui_view_row(n,v,row,&current);
            if(s==0 && event->x<(ScI32)(current.depth+1)*ui_scale(n->owner,16)){ScHashEntry e;
                memset(&e,0,sizeof(e));
                e.size=sizeof(e);
                e.version=1;
                s=ScUiTreeExpand(n->owner,n->id,item,ScHashGet(v->expanded,&item,4,&e)<0);
                goto handled;
                }}
        v->hot_item=item;
        v->hot_column=column;
        s=ui_view_select(n,v,item,event->modifiers);
        goto handled;
    }
    if(event->type==SC_UI_EVENT_POINTER_MOVE && v->resize_column!=UINT_MAX){ScI32 pixels=v->resize_width+event->x-v->resize_origin,scale=ui_scale(n->owner,100);
        if(pixels<0)pixels=0;
        s=ScUiTableSetColumnWidth(n->owner,n->id,v->resize_column,pixels*100/scale);
        goto handled;
        }
    if(event->type==SC_UI_EVENT_POINTER_UP && v->resize_column!=UINT_MAX && (!event->message || (event->message->data[3]&1u))){v->resize_column=UINT_MAX;
        ScUiCapture(n->owner,n->id,0);
        goto handled;
        }
    if(event->type==SC_UI_EVENT_WHEEL && event->message){ScI32 delta=(ScI32)event->message->data[2];
        ScU32 magnitude=delta<0?0u-(ScU32)delta:(ScU32)delta,amount=ui_scroll_wheel_amount(delta,ui_view_row_height(n));
        if(v->editor){s=ScUiTableEndEdit(n->owner,n->id,1);
            if(s!=0)goto handled;
            if(v->retired){s=SC_UI_E_CANCELLED;
                goto done;
                }}
        ScWideCounter maximum,before=n->scroll_y;ui_view_scroll_limit(n,&maximum);
        if(delta>0){if(!n->scroll_y.hi && n->scroll_y.lo<amount)n->scroll_y.lo=0;
            else{ScU32 old=n->scroll_y.lo;
                n->scroll_y.lo-=amount;
                if(old<amount)n->scroll_y.hi--;
                }}
        else if(magnitude){ScU32 high=n->scroll_y.hi;ui_wide_add(&n->scroll_y,amount);
            if(n->scroll_y.hi<high || ui_wide_compare(&n->scroll_y,&maximum)>0)n->scroll_y=maximum;
        }
        if(ui_wide_compare(&before,&n->scroll_y))ui_node_damage(n);
        goto handled;
        }
    if(event->type==SC_UI_EVENT_KEY_DOWN){ScU32 row=0,step=0;
        UiVisibleItem current;
        if(event->key=='a' && (event->modifiers&SC32_MOD_CONTROL)){s=ui_view_select_all(n,v);
            goto handled;
            }
        if(v->selection->current)ui_view_item_position(n,v,v->selection->current,&row);
        if(event->key==SC32_KEY_UP){if(row)row--;
            step=1;
            }
        else if(event->key==SC32_KEY_DOWN){if(row+1<v->total)row++;
            step=1;
            }
        else if(event->key==SC32_KEY_HOME){row=0;
            step=1;
            }
        else if(event->key==SC32_KEY_END){row=v->total?v->total-1:0;
            step=1;
            }
        else if(event->key==SC32_KEY_PAGEUP || event->key==SC32_KEY_PAGEDOWN){ScU32 page=(ScU32)n->rectangle.height/ui_view_row_height(n);
            if(!page)page=1;
            row=event->key==SC32_KEY_PAGEUP?row>page?row-page:0:row+page<v->total?row+page:v->total?v->total-1:0;
            step=1;
            }
        else if(v->tree && (event->key==SC32_KEY_LEFT || event->key==SC32_KEY_RIGHT) && v->selection->current){s=ScUiTreeExpand(n->owner,n->id,v->selection->current,event->key==SC32_KEY_RIGHT);
            goto handled;
            }
        else if(event->key==SC32_KEY_F1+1 && v->selection->current){ScU32 editable=v->hot_column;
            if(!editable){ScContainerInfo info;
                memset(&info,0,sizeof(info));
                info.size=sizeof(info);
                info.version=1;
                ScArrayQuery(v->columns,&info);
                for(ScU32 i=0;i<info.length;i++){UiColumn col;
                    ScArrayGet(v->columns,i,&col);
                    if((col.description.flags&(SC_UI_COLUMN_EDITABLE|SC_UI_COLUMN_HIDDEN))==SC_UI_COLUMN_EDITABLE){editable=col.description.column;
                        break;
                        }}}
            if(editable)s=ScUiTableBeginEdit(n->owner,n->id,v->selection->current,editable);
            goto handled;
            }
        if(step && v->total){s=ui_view_row(n,v,row,&current);
            if(s==0)s=ui_view_select(n,v,current.id,event->modifiers);
            goto handled;
            }
    }
    if(event->type==SC_UI_EVENT_TEXT && !(event->modifiers&(SC32_MOD_CONTROL|SC32_MOD_ALT))){unsigned char bytes[4];
        ScU32 written,start=0,parent=0;
        ScString *candidate=0;
        ScUiDataRequest q;
        ScUiDataReply reply;
        ScUiText prefix;
        s=ScUtf8Encode(event->scalar,bytes,sizeof(bytes),&written);
        if(s<0)goto done;
        if(v->search && event->tick-v->search_tick<=n->owner->hz){ScUiText old;
            ui_text_view(v->search,&old);
            s=ScStringCreate(old.data,old.bytes,&candidate);
            }
        else s=ScStringCreate(0,0,&candidate);
        if(s==0)s=ScStringAppend(candidate,bytes,written);
        if(s<0){ScStringRelease(candidate);
            goto done;
            }
        if(v->selection->current && ui_provider_index(v->provider,v->selection->current,0,&start,&parent)==0)start++;
        memset(&q,0,sizeof(q));
        q.size=sizeof(q);
        q.version=1;
        q.operation=SC_UI_DATA_FIND_PREFIX;
        q.parent=parent;
        q.index=start;
        q.flags=1;
        memset(&reply,0,sizeof(reply));
        reply.size=sizeof(reply);
        reply.version=1;
        reply.value.size=sizeof(reply.value);
        reply.value.version=1;
        reply.value.type=SC_VALUE_UTF8;
        ui_text_view(candidate,&prefix);
        reply.value.data=prefix.data;
        reply.value.bytes=prefix.bytes;
        s=ui_provider_call(v->provider,&q,&reply);
        if(s==0){ScStringRelease(v->search);
            v->search=candidate;
            candidate=0;
            v->search_tick=event->tick;
            s=ui_view_select(n,v,reply.item,0);
            }
        ScStringRelease(candidate);
        goto handled;
    }
    goto done;
handled:if(s>=0)s=SC_UI_HANDLED;
done:s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
ScStatus ScUiTableBeginEdit(ScUiContext *c,ScUiNode id,ScU32 item,ScU32 column)
{
    UiNode *n=ui_node(c,id);
    UiView *v;
    UiColumn col;
    ScU32 index,row;
    ScValue value;
    ScString *text=0;
    ScUiText content;
    ScUiWidgetDesc d;
    ScUiLayoutDesc layout;
    ScUiNode editor=0;
    ScStatus s;
    if(!n || !n->view || !n->view->provider)return SC_UI_E_STALE;
    v=n->view;
    ui_view_enter(n,v);
    if(v->editor){s=SC_UI_E_BUSY;
        goto done;
        }
    s=ui_column_find(v,column,&col,&index);
    if(s<0)goto done;
    if(!(col.description.flags&SC_UI_COLUMN_EDITABLE) || (col.description.flags&SC_UI_COLUMN_HIDDEN)){s=SC_UI_E_INVALID;
        goto done;
        }
    s=ui_view_prepare(n);
    if(s==0)s=ui_view_item_position(n,v,item,&row);
    if(s!=0)goto done;
    s=ScUiViewEnsureItem(c,id,item);
    if(s!=0)goto done;
    if(v->retired){s=SC_UI_E_CANCELLED;
        goto done;
        }
    {ScWideCounter edge=n->scroll_x,right=col.position;
        ScU32 viewport=n->rectangle.width>0?(ScU32)n->rectangle.width:0;
        ui_wide_add(&edge,viewport);
        ui_wide_add(&right,(ScU32)col.width);
        if(ui_wide_compare(&col.position,&n->scroll_x)<0)n->scroll_x=col.position;
        else if(ui_wide_compare(&right,&edge)>0){ScU32 amount=(ScU32)col.width<viewport?(ScU32)col.width:viewport,old=right.lo;
            right.lo-=amount;
            right.hi-=old<amount;
            n->scroll_x=right;
            }}
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    s=ui_provider_cell(v->provider,item,col.description.field,&value);
    if(s==0 && col.description.format==SC_UI_FORMAT_HEX){char hex[16];
        int length=snprintf(hex,sizeof(hex),"%08X",value.lo);
        s=length<0?SC_UI_E_FAILED:ScStringCreate(hex,(ScU32)length,&text);
        }
    else if(s==0)s=ui_value_text(&value,&text);
    if(s<0)goto done;
    ui_text_view(text,&content);
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.kind=SC_UI_TEXTBOX;
    d.parent=id;
    d.text=content;
    s=ScUiWidgetCreate(c,&d,&editor);
    if(s<0)goto done;
    memset(&layout,0,sizeof(layout));
    layout.size=sizeof(layout);
    layout.version=1;
    layout.mode=SC_UI_ABSOLUTE;
    layout.width=col.description.width;
    layout.height=(ScI32)ui_view_row_height(n)*100/ui_scale(c,100);
    layout.min_width=0;
    layout.min_height=0;
    layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.column_span=layout.row_span=1;
    layout.flags=UI_LAYOUT_CLIP;
    {UiNode *overlay=ui_node(c,editor);
        ScU32 remainder,offset=ui_pair_divide_row(&n->scroll_y,ui_view_row_height(n),&remainder);
        ScI32 y=(ScI32)((row-offset)*ui_view_row_height(n))-(ScI32)remainder;
        if(!overlay){s=SC_UI_E_CANCELLED;
            goto done;
            }
        if(n->kind==SC_UI_TABLE)y+=(ScI32)ui_view_row_height(n);
        overlay->private_flags|=UI_NODE_VIEWPORT_CHILD;
        overlay->viewport_rectangle.x=ui_column_x(n,&col);
        overlay->viewport_rectangle.y=y;
        overlay->viewport_rectangle.width=col.width;
        overlay->viewport_rectangle.height=(ScI32)ui_view_row_height(n);
        }
    s=ScUiNodeSetLayout(c,editor,&layout);
    if(s<0 || v->retired || (n->private_flags&UI_NODE_RETIRED)){ScUiNodeDestroy(c,editor);
        if(s>=0)s=SC_UI_E_CANCELLED;
        goto done;
        }
    v->editor=editor;
    v->editor_item=item;
    v->editor_column=column;
    v->editor_type=value.type;
    ui_node_layout_dirty(n);
    ScUiLayout(c,n->root);
    ScUiFocus(c,editor);
    s=SC_UI_OK;
done:ScStringRelease(text);
    s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
ScStatus ScUiTableEndEdit(ScUiContext *c,ScUiNode id,ScU32 commit)
{
    UiNode *n=ui_node(c,id);
    UiView *v;
    ScString *text=0;
    ScBuffer *binary=0;
    ScUiDataRequest q;
    ScUiDataReply r;
    UiColumn col;
    ScU32 index;
    ScStatus s=0;
    if(!n || !n->view || !n->view->editor)return SC_UI_E_STALE;
    if(commit>1)return SC_UI_E_INVALID;
    v=n->view;
    ui_view_enter(n,v);
    if(commit){ScUiText content;
        const char *z;
        char *end=0;
        s=ui_column_find(v,v->editor_column,&col,&index);
        if(s<0)goto done;
        s=ScUiNodeCopyText(c,v->editor,&text);
        if(s<0)goto done;
        ui_text_view(text,&content);
        memset(&q,0,sizeof(q));
        q.size=sizeof(q);
        q.version=1;
        q.operation=SC_UI_DATA_COMMIT_EDIT;
        q.item=v->editor_item;
        q.column=v->editor_column;
        q.field=col.description.field;
        memset(&r,0,sizeof(r));
        r.size=sizeof(r);
        r.version=1;
        r.value.size=sizeof(r.value);
        r.value.version=1;
        r.value.type=SC_VALUE_UTF8;
        r.value.data=content.data;
        r.value.bytes=content.bytes;
        if(col.description.format==SC_UI_FORMAT_I32 || col.description.format==SC_UI_FORMAT_U32 || col.description.format==SC_UI_FORMAT_HEX || (col.description.format==SC_UI_FORMAT_BYTES && v->editor_type==SC_VALUE_U32)){ScStringCStr(text,&z);
            errno=0;
            if(col.description.format==SC_UI_FORMAT_I32){long number=strtol(z,&end,10);
                r.value.type=SC_VALUE_I32;
                r.value.lo=(ScU32)number;
                }
            else{if(*z=='-'){s=SC_UI_E_RANGE;
                    goto done;
                    }
                r.value.type=SC_VALUE_U32;
                r.value.lo=(ScU32)strtoul(z,&end,col.description.format==SC_UI_FORMAT_HEX?16:10);
                }
            if(errno==ERANGE || !end || end==z || *end){s=SC_UI_E_RANGE;
                goto done;
                }
            r.value.data=0;
            r.value.bytes=0;
            }
        else if(col.description.format==SC_UI_FORMAT_COUNTER){ScU32 high,low;
            ScStringCStr(text,&z);
            errno=0;
            if(*z=='-' || *z=='+'){s=SC_UI_E_INVALID;
                goto done;
                }
            high=(ScU32)strtoul(z,&end,16);
            if(errno==ERANGE || end==z || *end!=':'){s=SC_UI_E_RANGE;
                goto done;
                }
            z=end+1;
            if(*z=='-' || *z=='+'){s=SC_UI_E_INVALID;
                goto done;
                }
            low=(ScU32)strtoul(z,&end,16);
            if(errno==ERANGE || end==z || *end){s=SC_UI_E_RANGE;
                goto done;
                }
            r.value.type=SC_VALUE_COUNTER;
            r.value.lo=low;
            r.value.hi=high;
            r.value.data=0;
            r.value.bytes=0;
            }
        else if(col.description.format==SC_UI_FORMAT_BYTES){ScU32 i;
            ScView bytes;
            s=ScBufferCreate(content.bytes/2,&binary);
            if(s<0)goto done;
            if(content.bytes&1u){s=SC_UI_E_INVALID;
                goto done;
                }
            for(i=0;i<content.bytes;i+=2){ScU32 a=(unsigned char)content.data[i],b=(unsigned char)content.data[i+1];
                unsigned char value;
                a=a>='0' && a<='9'?a-'0':a>='a' && a<='f'?a-'a'+10:a>='A' && a<='F'?a-'A'+10:16;
                b=b>='0' && b<='9'?b-'0':b>='a' && b<='f'?b-'a'+10:b>='A' && b<='F'?b-'A'+10:16;
                if(a>15 || b>15){s=SC_UI_E_INVALID;
                    goto done;
                    }
                value=(unsigned char)(a*16+b);
                s=ScBufferAppend(binary,&value,1);
                if(s<0)goto done;
                }
            memset(&bytes,0,sizeof(bytes));
            bytes.size=sizeof(bytes);
            bytes.version=1;
            ScBufferView(binary,&bytes);
            r.value.type=SC_VALUE_BYTES;
            r.value.data=bytes.data;
            r.value.bytes=bytes.bytes;
            }
        s=ui_provider_call(v->provider,&q,&r);
        if(s!=0)goto done;
    }
    if(!v->retired){ScUiNode editor=v->editor;
        v->editor=0;
        v->revision=0;
        ScUiNodeDestroy(c,editor);
        ui_node_damage(n);
        }
    s=SC_UI_OK;
done:ScBufferRelease(binary);
    ScStringRelease(text);
    s=ui_cause(n->owner,s,UI_OP_PROVIDER,n->id);
    ui_view_leave(n,v);
    return s;
}
