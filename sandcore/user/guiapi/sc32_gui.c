#include "../sc32/SC32PRIVATE.H"
#include "../sc32/SC32GUIWIRE.H"

/* 公共GUI只管理三环过程、context和owner关系。内核窗口/画布地址
 * 从不进入此表；4KiB页内槽是分配粒度，不是窗口数量上限。 */
typedef struct sc32_gui_record sc32_gui_record;
typedef struct sc32_gui_page sc32_gui_page;
struct sc32_gui_page {
    sc32_gui_page *next,*previous,*available_next,*available_previous;
    sc32_gui_record *free;
    ScU32 live,capacity;
};
struct sc32_gui_record {
    sc32_gui_record *hash_next,*all_next,*all_previous,*free_next;
    sc32_gui_record *retired_next,*retired_previous;
    sc32_gui_page *page;
    sc32_gui_record *parent,*first_child,*next_child,*previous_child;
    ScU32 window,generation,state,depth,create_seen,destroy_notified;
    ScWindowProc procedure;
    void *context;
};
typedef char gui_desc_64[(sizeof(ScWindowDesc)==64)?1:-1];
typedef char gui_message_64[(sizeof(ScMessage)==64)?1:-1];
typedef char gui_window_96[(sizeof(ScWindowInfo)==96)?1:-1];
typedef char gui_input_96[(sizeof(ScInputSnapshot)==96)?1:-1];
typedef char gui_paint_64[(sizeof(ScPaintInfo)==64)?1:-1];
typedef char gui_submit_64[(sizeof(ScSubmitDesc)==64)?1:-1];
typedef char gui_rect_24[(sizeof(ScSubmitRect)==24)?1:-1];
typedef char gui_record_fits[(sizeof(sc32_gui_record)+sizeof(sc32_gui_page)<=4096)?1:-1];

#define GUI_RESERVED 0u
#define GUI_CREATING 1u
#define GUI_ACTIVE 2u
#define GUI_DESTROYING 3u
#define GUI_RETIRED 4u
#define GUI_OPERATION_BASE 0x800u
static sc32_gui_page *gui_pages,*gui_available;
static sc32_gui_record *gui_all,*gui_retired;
static sc32_gui_record **gui_buckets;
static ScU32 gui_bucket_count,gui_records,gui_walk_depth,gui_stopping;
typedef struct {sc32_gui_record **table;ScU32 count;} sc32_gui_index_undo;

static ScStatus gui_error(ScStatus code,ScU32 selector)
{return sc32_invalid(code,GUI_OPERATION_BASE+selector-SC32_GUI_CREATE);}
static ScStatus gui_result(ScI32 status,ScU32 selector)
{
    if(status==SC32_OK || status==SC32_PENDING || status==SC32_TIMEOUT)return status;
    return sc32_native(status,GUI_OPERATION_BASE+selector-SC32_GUI_CREATE);
}
static ScI32 gui_call(ScU32 selector,const void *request,void *out)
{return sc32_call(SC32_WIRE_GATE,(ScI32)selector,(ScI32)request,(ScI32)out,0,0,0);}
static void *gui_allocate(ScU32 bytes)
{
    ScU32 value=(ScU32)sc32_call(0x100,(ScI32)bytes,0,0,0,0,0);
    return value>=0x40000000u && value<0x44000000u?(void *)value:0;
}
static void gui_release(void *memory)
{if(memory)sc32_call(0x101,(ScI32)memory,0,0,0,0,0);}
static ScU32 gui_hash(ScU32 window,ScU32 count)
{return ((window*2654435761u)^(window>>16))&(count-1);}
static ScStatus gui_reserve_index(sc32_gui_index_undo *undo)
{
    if(gui_bucket_count && gui_records<gui_bucket_count)return 0;
    ScU32 capacity=gui_bucket_count?gui_bucket_count*2:1024;
    if(capacity<gui_bucket_count || capacity>0xFFFFFFFFu/sizeof(*gui_buckets))return SC32_E_MEMORY;
    sc32_gui_record **fresh=gui_allocate(capacity*4);if(!fresh)return SC32_E_MEMORY;
    /* USERALLOC已经清零。只在少见的窗口创建扩容重建索引；键鼠分发
     * 按稳定ID查桶，不能每个高频消息线性扫全部窗口过程。 */
    for(sc32_gui_record *p=gui_all;p;p=p->all_next){
        if(!p->window)continue;
        ScU32 bucket=gui_hash(p->window,capacity);
        p->hash_next=fresh[bucket];fresh[bucket]=p;
    }
    undo->table=gui_buckets;undo->count=gui_bucket_count;
    gui_buckets=fresh;gui_bucket_count=capacity;return 0;
}
static void gui_index_finish(sc32_gui_index_undo *undo,int committed)
{
    if(!undo->table)return;
    /* 旧索引保留到CREATE事务结束。拒绝/内存失败且剩余实例仍容得下
     * 旧桶时，重建旧索引并归还候选页；嵌套CREATE真正留下更多独立
     * 活窗时才保留新容量。回退不再分配页，不把OOM后的扩容当泄漏缓存。 */
    if(!committed && gui_records && gui_records<=undo->count){
        sc32_gui_record **current=gui_buckets;
        sc32_zero(undo->table,undo->count*4);
        for(sc32_gui_record *p=gui_all;p;p=p->all_next){
            if(!p->window)continue;
            ScU32 bucket=gui_hash(p->window,undo->count);
            p->hash_next=undo->table[bucket];undo->table[bucket]=p;
        }
        gui_buckets=undo->table;gui_bucket_count=undo->count;
        gui_release(current);
    }else gui_release(undo->table);
    undo->table=0;
}
static void gui_available_remove(sc32_gui_page *page)
{
    if(page->available_previous)page->available_previous->available_next=page->available_next;
    else gui_available=page->available_next;
    if(page->available_next)page->available_next->available_previous=page->available_previous;
    page->available_previous=page->available_next=0;
}
static void gui_available_add(sc32_gui_page *page)
{
    page->available_previous=0;page->available_next=gui_available;
    if(gui_available)gui_available->available_previous=page;
    gui_available=page;
}
static sc32_gui_record *gui_new_record(sc32_gui_index_undo *undo)
{
    undo->table=0;undo->count=0;
    if(gui_reserve_index(undo))return 0;
    sc32_gui_page *page=gui_available;
    if(!page){
        page=gui_allocate(4096);if(!page){
            if(!gui_records){gui_release(gui_buckets);gui_buckets=0;gui_bucket_count=0;}
            gui_index_finish(undo,0);return 0;
        }
        page->capacity=(4096-sizeof(sc32_gui_page))/sizeof(sc32_gui_record);
        page->next=gui_pages;
        if(gui_pages)gui_pages->previous=page;
        gui_pages=page;
        for(ScU32 i=0;i<page->capacity;i++){
            sc32_gui_record *p=(sc32_gui_record *)((unsigned char *)page+sizeof(sc32_gui_page)+i*sizeof(sc32_gui_record));
            p->free_next=page->free;page->free=p;
        }
        gui_available_add(page);
    }
    sc32_gui_record *record=page->free;page->free=record->free_next;
    if(!page->free)gui_available_remove(page);
    page->live++;gui_records++;sc32_zero(record,sizeof(*record));record->page=page;
    record->all_next=gui_all;
    if(gui_all)gui_all->all_previous=record;
    gui_all=record;return record;
}
static sc32_gui_record *gui_find(ScWindow window)
{
    if(!window || !gui_bucket_count)return 0;
    for(sc32_gui_record *p=gui_buckets[gui_hash(window,gui_bucket_count)];p;p=p->hash_next){
        if(p->window==window)return p;
    }
    return 0;
}
static sc32_gui_record *gui_live(ScWindow window)
{sc32_gui_record *p=gui_find(window);return p && p->state<GUI_DESTROYING?p:0;}
static void gui_index(sc32_gui_record *record)
{
    ScU32 bucket=gui_hash(record->window,gui_bucket_count);
    record->hash_next=gui_buckets[bucket];gui_buckets[bucket]=record;
}
static void gui_unlink_owner(sc32_gui_record *record)
{
    if(record->previous_child)record->previous_child->next_child=record->next_child;
    else if(record->parent)record->parent->first_child=record->next_child;
    if(record->next_child)record->next_child->previous_child=record->previous_child;
    record->parent=record->next_child=record->previous_child=0;
}
static void gui_link_owner(sc32_gui_record *record,sc32_gui_record *parent)
{
    gui_unlink_owner(record);record->parent=parent;
    if(parent){
        record->next_child=parent->first_child;
        if(record->next_child)record->next_child->previous_child=record;
        parent->first_child=record;
    }
}
static void gui_unlink_retired(sc32_gui_record *record)
{
    if(record->retired_previous)record->retired_previous->retired_next=record->retired_next;
    else gui_retired=record->retired_next;
    if(record->retired_next)record->retired_next->retired_previous=record->retired_previous;
    record->retired_previous=record->retired_next=0;
}
static void gui_free_record(sc32_gui_record *record)
{
    if(record->window && gui_bucket_count){
        sc32_gui_record **link=gui_buckets+gui_hash(record->window,gui_bucket_count);
        while(*link && *link!=record)link=&(*link)->hash_next;
        if(*link)*link=record->hash_next;
    }
    if(record->all_previous)record->all_previous->all_next=record->all_next;else gui_all=record->all_next;
    if(record->all_next)record->all_next->all_previous=record->all_previous;
    if(record->state==GUI_RETIRED)gui_unlink_retired(record);
    gui_unlink_owner(record);
    while(record->first_child)gui_unlink_owner(record->first_child);
    sc32_gui_page *page=record->page;int was_full=page->free==0;
    sc32_zero(record,sizeof(*record));record->free_next=page->free;page->free=record;
    page->live--;gui_records--;
    if(!page->live){
        if(!was_full)gui_available_remove(page);
        if(page->previous)page->previous->next=page->next;else gui_pages=page->next;
        if(page->next)page->next->previous=page->previous;
        gui_release(page);
    }else if(was_full)gui_available_add(page);
    if(!gui_records){gui_release(gui_buckets);gui_buckets=0;gui_bucket_count=0;}
}
static void gui_collect(void)
{
    if(gui_walk_depth)return;
    sc32_gui_record *p=gui_retired;
    while(p){
        sc32_gui_record *next=p->retired_next;
        if(!p->depth)gui_free_record(p);
        p=next;
    }
}
static void gui_hold(sc32_gui_record *record){record->depth++;}
static void gui_leave(sc32_gui_record *record)
{
    record->depth--;
    if(record->state==GUI_RETIRED && !record->depth && !gui_walk_depth)gui_free_record(record);
}
static ScStatus gui_invoke(sc32_gui_record *record,const ScMessage *message)
{
    ScWindowProc procedure=record->procedure?record->procedure:ScDefaultWindowProc;
    ScStatus checked=sc32_probe((const void *)procedure,1,SC32_WIRE_READ);if(checked<0)return checked;
    /* 调用只发生在当前Ring3栈。持有者必须先hold，回调可重入Destroy
     * 或创建/关闭其它窗口；retired不再接输入，但不能释放仍执行的过程记录。 */
    return procedure(record->window,message,record->context);
}
static void gui_retire_one(sc32_gui_record *record,int notify,const ScMessage *fact)
{
    if(record->state==GUI_RETIRED)return;
    gui_hold(record);
    record->state=GUI_RETIRED;gui_unlink_owner(record);
    while(record->first_child)gui_unlink_owner(record->first_child);
    record->retired_next=gui_retired;
    if(gui_retired)gui_retired->retired_previous=record;
    gui_retired=record;
    if(notify && record->create_seen && !record->destroy_notified && !gui_stopping){
        record->destroy_notified=1;ScMessage local;
        if(fact)local=*fact;else{sc32_zero(&local,sizeof(local));local.size=64;local.version=1;
            local.id=SC32_MSG_DESTROY;local.window=record->window;local.generation=record->generation;local.flags=SC32_MSG_APPLICATION;}
        gui_hold(record);(void)gui_invoke(record,&local);gui_leave(record);
    }
    gui_leave(record);
}
static void gui_retire_tree(sc32_gui_record *root,int notify)
{
    /* 内核Destroy已经按owner子树关闭。三环沿同一owner图叶先回调，
     * 不递归占用128KiB应用栈，也不会先释放父model再回调孩子context。
     * walk期间retired保留节点，重入销其它窗只标退役，不破坏当前游标。 */
    gui_walk_depth++;gui_hold(root);
    /* 先给整个已关闭子树打收束标记。否则孩子DESTROY重入销父时，
     * 内核STALE会解除尚未通知的兄弟，父model可能先于孩子释放。
     * 标记期间没有回调；owner图遍历为O(子树大小)，不扫其它根窗。 */
    sc32_gui_record *p=root;
    for(;;){
        p->state=GUI_DESTROYING;
        if(p->first_child){p=p->first_child;continue;}
        while(p!=root && !p->next_child)p=p->parent;
        if(p==root)break;
        p=p->next_child;
    }
    sc32_gui_record *leaf=root;
    while(leaf->first_child)leaf=leaf->first_child;
    for(;;){
        sc32_gui_record *parent=leaf->parent;
        int last=leaf==root;
        gui_retire_one(leaf,notify,0);
        if(last)break;
        /* 只沿刚关闭的孩子回到父亲，再进它的剩余孩子。每条owner
         * 边至多上/下各走一次；深链不会每关闭一叶都从根重扫成O(n²)。 */
        leaf=parent;
        while(leaf->first_child)leaf=leaf->first_child;
    }
    gui_leave(root);gui_walk_depth--;gui_collect();
}
static void gui_reference(sc32_gui_record *record,Sc32WindowRef *out)
{
    /* 私有记录写入本次调用栈，不要求工具链额外实现结构体返回ABI。 */
    out->size=16;out->version=1;out->window=record->window;out->generation=record->generation;
}
static ScStatus gui_require(sc32_gui_record **record,ScWindow window,ScU32 selector)
{
    if(gui_stopping)return gui_error(SC32_E_BUSY,selector);
    *record=gui_live(window);return *record?SC32_OK:gui_error(SC32_E_STALE,selector);
}

static ScStatus gui_create_common(const ScWindowDesc *desc,ScWindow *out,const Sc32PopupCreate *popup)
{
    ScU32 selector=popup?SC32_GUI_POPUP_CREATE:SC32_GUI_CREATE;
    if(gui_stopping)return gui_error(SC32_E_BUSY,selector);
    ScStatus checked=sc32_out(out,4);if(checked<0)return gui_error(checked,selector);
    checked=sc32_record_in(desc,sizeof(ScWindowDesc));if(checked<0)return gui_error(checked,selector);
    ScWindowDesc value=*desc;
    for(ScU32 i=0;i<4;i++){
        if(value.reserved[i])return gui_error(SC32_E_INVALID,selector);
    }
    if(value.style&~127u)return gui_error(SC32_E_INVALID,selector);
    checked=sc32_string(value.title,127,0);if(checked<0)return gui_error(checked,selector);
    if(value.procedure){
        checked=sc32_probe((const void *)value.procedure,1,SC32_WIRE_READ);
        if(checked<0)return gui_error(checked,selector);
    }
    sc32_gui_record *parent=value.owner_window?gui_live(value.owner_window):0;
    if(value.owner_window && (!parent || parent->generation!=value.owner_generation))return gui_error(SC32_E_STALE,selector);
    if(!value.owner_window && value.owner_generation)return gui_error(SC32_E_INVALID,selector);
    sc32_gui_index_undo undo;
    sc32_gui_record *record=gui_new_record(&undo);if(!record)return gui_error(SC32_E_MEMORY,selector);
    gui_hold(record);
    Sc32WindowCreate request;sc32_zero(&request,sizeof(request));request.size=64;request.version=1;request.title=(ScU32)value.title;
    request.x=value.x;request.y=value.y;request.client_width=value.client_width;request.client_height=value.client_height;
    request.style=value.style;request.owner_window=value.owner_window;request.owner_generation=value.owner_generation;
    Sc32WindowRef ref={16,1,0,0};ScI32 raw=gui_call(selector,popup?(const void *)popup:(const void *)&request,&ref);
    if(raw){gui_leave(record);gui_free_record(record);gui_index_finish(&undo,0);return gui_result(raw,selector);}
    if(!ref.window || !ref.generation){gui_leave(record);gui_free_record(record);gui_index_finish(&undo,0);return gui_error(SC32_E_FAILED,selector);}
    if(popup && value.procedure){
        /* 新popup先取得隐藏kernel候选和R3元数据，失败几何/OOM不会
         * 凭空固定模块。正式pin成功才进入用户CREATE过程。已真正
         * 执行的callback按公开固定到进程退出合同保留代码寿命。 */
        checked=ScCallbackPin((const void *)value.procedure);
        if(checked){(void)gui_call(SC32_GUI_DESTROY,&ref,0);gui_leave(record);gui_free_record(record);gui_index_finish(&undo,0);return checked;}
    }
    record->window=ref.window;record->generation=ref.generation;record->state=GUI_CREATING;
    record->procedure=value.procedure;record->context=value.context;record->create_seen=1;
    gui_index(record);gui_link_owner(record,parent);
    ScMessage create;sc32_zero(&create,sizeof(create));create.size=64;create.version=1;create.id=SC32_MSG_CREATE;
    create.window=ref.window;create.generation=ref.generation;create.flags=SC32_MSG_APPLICATION;
    create.tick=(ScU32)sc32_call(2,0,0,0,0,0,0);
    create.data[0]=value.client_width;create.data[1]=value.client_height;create.data[2]=value.style;
    create.data[3]=value.owner_window;create.data[4]=value.owner_generation;
    if(popup)create.data[5]=popup->kind;
    ScStatus callback=gui_invoke(record,&create);
    ScStatus failure=callback;
    int native_failure=0;ScU32 failure_selector=selector;
    if(record->state==GUI_RETIRED)failure=SC32_E_CANCELLED;
    else if(!failure){checked=sc32_out(out,4);if(checked<0)failure=checked;}
    if(!failure){raw=gui_call(SC32_GUI_ACTIVATE,&ref,0);if(raw){failure=raw;native_failure=1;failure_selector=SC32_GUI_ACTIVATE;}}
    if(failure){
        if(record->state!=GUI_RETIRED){
            raw=gui_call(SC32_GUI_DESTROY,&ref,0);
            if(!raw)gui_retire_tree(record,1);
            else if(raw==SC32_E_STALE)gui_retire_one(record,1,0);
            else{failure=raw;native_failure=1;failure_selector=SC32_GUI_DESTROY;}
        }
        gui_leave(record);gui_index_finish(&undo,0);
        return native_failure?gui_result(failure,failure_selector):gui_error(failure,selector);
    }
    record->state=GUI_ACTIVE;*out=ref.window;gui_leave(record);gui_index_finish(&undo,1);return 0;
}
ScStatus ScWindowCreate(const ScWindowDesc *desc,ScWindow *out)
{return gui_create_common(desc,out,0);}
ScStatus ScWindowDestroy(ScWindow window)
{
    if(gui_stopping)return gui_error(SC32_E_BUSY,SC32_GUI_DESTROY);
    sc32_gui_record *record=gui_find(window);
    if(!record)return gui_error(SC32_E_STALE,SC32_GUI_DESTROY);
    if(record->state>=GUI_DESTROYING)return 0;
    gui_hold(record);Sc32WindowRef ref;gui_reference(record,&ref);ScI32 raw=gui_call(SC32_GUI_DESTROY,&ref,0);
    if(!raw)gui_retire_tree(record,1);
    else if(raw==SC32_E_STALE)gui_retire_one(record,1,0);
    gui_leave(record);return gui_result(raw,SC32_GUI_DESTROY);
}
ScStatus ScWindowQuery(ScWindow window,ScWindowInfo *out)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_QUERY);if(checked)return checked;
    Sc32WindowRef ref;gui_reference(record,&ref);return gui_result(gui_call(SC32_GUI_QUERY,&ref,out),SC32_GUI_QUERY);
}
ScStatus ScWindowPlace(ScWindow window,ScI32 x,ScI32 y,ScU32 width,ScU32 height,ScWindowInfo *out)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_PLACE);if(checked)return checked;
    Sc32WindowPlace request;sc32_zero(&request,sizeof(request));request.size=sizeof(request);request.version=1;
    request.window=window;request.generation=record->generation;request.x=x;request.y=y;request.width=width;request.height=height;
    return gui_result(gui_call(SC32_GUI_PLACE,&request,out),SC32_GUI_PLACE);
}
ScStatus ScWindowControl(ScWindow window,ScU32 action,ScWindowInfo *out)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_CONTROL);if(checked)return checked;
    Sc32WindowAction request={24,1,window,record->generation,action,0};
    return gui_result(gui_call(SC32_GUI_CONTROL,&request,out),SC32_GUI_CONTROL);
}
ScStatus ScWindowSetTitle(ScWindow window,const char *title)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_TITLE);if(checked)return checked;
    checked=sc32_string(title,127,0);if(checked<0)return gui_error(checked,SC32_GUI_TITLE);
    Sc32WindowTitle request={24,1,window,record->generation,(ScU32)title,0};
    return gui_result(gui_call(SC32_GUI_TITLE,&request,0),SC32_GUI_TITLE);
}
ScStatus ScWindowSetOwner(ScWindow window,ScWindow owner,ScU32 style,ScWindowInfo *out)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_OWNER);if(checked)return checked;
    sc32_gui_record *parent=owner?gui_live(owner):0;
    if(owner && !parent)return gui_error(SC32_E_STALE,SC32_GUI_OWNER);
    for(sc32_gui_record *p=parent;p;p=p->parent){
        if(p==record)return gui_error(SC32_E_INVALID,SC32_GUI_OWNER);
    }
    Sc32WindowOwner request={32,1,window,record->generation,owner,parent?parent->generation:0,style,0};
    ScI32 raw=gui_call(SC32_GUI_OWNER,&request,out);
    if(!raw)gui_link_owner(record,parent);
    return gui_result(raw,SC32_GUI_OWNER);
}
ScStatus ScWindowRejectClose(ScWindow window)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_CLOSE_CANCEL);if(checked)return checked;
    Sc32WindowRef ref;gui_reference(record,&ref);return gui_result(gui_call(SC32_GUI_CLOSE_CANCEL,&ref,0),SC32_GUI_CLOSE_CANCEL);
}
ScStatus ScWindowInvalidate(ScWindow window,ScI32 x,ScI32 y,ScU32 width,ScU32 height)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_INVALIDATE);if(checked)return checked;
    Sc32Invalidate request={36,1,window,record->generation,x,y,width,height,0};
    return gui_result(gui_call(SC32_GUI_INVALIDATE,&request,0),SC32_GUI_INVALIDATE);
}
ScStatus ScPaintBegin(ScWindow window,ScPaintInfo *out)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_PAINT_BEGIN);if(checked)return checked;
    Sc32WindowRef ref;gui_reference(record,&ref);return gui_result(gui_call(SC32_GUI_PAINT_BEGIN,&ref,out),SC32_GUI_PAINT_BEGIN);
}
ScStatus ScPaintEnd(const ScPaintInfo *paint)
{
    ScStatus checked=sc32_record_in(paint,sizeof(ScPaintInfo));if(checked<0)return gui_error(checked,SC32_GUI_PAINT_END);
    sc32_gui_record *record;checked=gui_require(&record,paint->window,SC32_GUI_PAINT_END);if(checked)return checked;
    if(paint->generation!=record->generation)return gui_error(SC32_E_STALE,SC32_GUI_PAINT_END);
    if(paint->reserved[0] || paint->reserved[1])return gui_error(SC32_E_INVALID,SC32_GUI_PAINT_END);
    Sc32PaintEnd request={24,1,paint->window,paint->generation,paint->geometry_epoch,paint->paint_epoch};
    return gui_result(gui_call(SC32_GUI_PAINT_END,&request,0),SC32_GUI_PAINT_END);
}
ScStatus ScWindowSubmit(const ScSubmitDesc *submit)
{
    ScStatus checked=sc32_record_in(submit,sizeof(ScSubmitDesc));if(checked<0)return gui_error(checked,SC32_GUI_SUBMIT);
    ScSubmitDesc request=*submit;sc32_gui_record *record;checked=gui_require(&record,request.window,SC32_GUI_SUBMIT);if(checked)return checked;
    if(request.generation!=record->generation)return gui_error(SC32_E_STALE,SC32_GUI_SUBMIT);
    for(ScU32 i=0;i<4;i++){
        if(request.reserved[i])return gui_error(SC32_E_INVALID,SC32_GUI_SUBMIT);
    }
    if(!request.rectangle_count || request.rectangle_count>SC32_GUI_RECT_LIMIT)return gui_error(SC32_E_RANGE,SC32_GUI_SUBMIT);
    checked=sc32_probe(request.rectangles,request.rectangle_count*sizeof(ScSubmitRect),SC32_WIRE_READ);if(checked<0)return gui_error(checked,SC32_GUI_SUBMIT);
    checked=sc32_probe(request.pixels,request.bytes,SC32_WIRE_READ);if(checked<0)return gui_error(checked,SC32_GUI_SUBMIT);
    return gui_result(gui_call(SC32_GUI_SUBMIT,&request,0),SC32_GUI_SUBMIT);
}
ScStatus ScInputQuery(ScWindow window,ScInputSnapshot *out)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_INPUT);if(checked)return checked;
    Sc32WindowRef ref;gui_reference(record,&ref);return gui_result(gui_call(SC32_GUI_INPUT,&ref,out),SC32_GUI_INPUT);
}
ScStatus ScPointerCapture(ScWindow window,ScU32 enabled)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_CAPTURE);if(checked)return checked;
    Sc32WindowAction request={24,1,window,record->generation,enabled,0};
    return gui_result(gui_call(SC32_GUI_CAPTURE,&request,0),SC32_GUI_CAPTURE);
}
ScStatus ScMessagePeek(ScMessage *out)
{
    if(gui_stopping)return gui_error(SC32_E_BUSY,SC32_GUI_PEEK);
    return gui_result(gui_call(SC32_GUI_PEEK,0,out),SC32_GUI_PEEK);
}
ScStatus ScMessageGet(ScMessage *out,ScU32 timeout)
{
    if(gui_stopping)return gui_error(SC32_E_BUSY,SC32_GUI_GET);
    ScStatus checked=sc32_record_out(out,sizeof(ScMessage));if(checked<0)return gui_error(checked,SC32_GUI_GET);
    if(timeout>0x7FFFFFFFu && timeout!=SC32_INFINITE)return gui_error(SC32_E_RANGE,SC32_GUI_GET);
    ScU32 start=(ScU32)sc32_call(2,0,0,0,0,0,0);
    for(;;){
        /* GET直接预验并发布最终用户out，成功后内核才消费；不能先把
         * 消息领进临时区再发现caller out无效而丢掉不可重放的输入。 */
        ScI32 raw=gui_call(SC32_GUI_GET,0,out);if(raw!=SC32_PENDING)return gui_result(raw,SC32_GUI_GET);
        if(!timeout)return SC32_PENDING;
        ScU32 remaining=SC32_INFINITE;
        if(timeout!=SC32_INFINITE){
            ScU32 elapsed=(ScU32)sc32_call(2,0,0,0,0,0,0)-start;
            if(elapsed>=timeout)return SC32_TIMEOUT;
            remaining=timeout-elapsed;
        }
        Sc32MessageWait wait={16,1,remaining,0};raw=gui_call(SC32_GUI_WAIT,&wait,0);
        if(raw==SC32_TIMEOUT)return SC32_TIMEOUT;
        if(raw!=SC32_OK && raw!=SC32_PENDING)return gui_result(raw,SC32_GUI_WAIT);
        /* WAIT把空队列检查与睡眠合成一次内核操作。被取消/过滤的迟到
         * 事件可能只给PENDING，重查后重新睡，不用YIELD持续poll。 */
    }
}
ScStatus ScMessagePost(const ScMessage *message)
{
    if(gui_stopping)return gui_error(SC32_E_BUSY,SC32_GUI_POST);
    ScStatus checked=sc32_record_in(message,sizeof(ScMessage));if(checked<0)return gui_error(checked,SC32_GUI_POST);
    ScMessage request=*message;
    if((request.id!=SC32_MSG_COMMAND && request.id<SC32_MSG_USER) || (request.flags&~SC32_MSG_APPLICATION))return gui_error(SC32_E_INVALID,SC32_GUI_POST);
    if(request.window){sc32_gui_record *record=gui_live(request.window);
        if(!record || record->generation!=request.generation)return gui_error(SC32_E_STALE,SC32_GUI_POST);}
    else if(request.generation || request.id==SC32_MSG_COMMAND)return gui_error(SC32_E_INVALID,SC32_GUI_POST);
    /* 这些字段只由kernel生成。APPLICATION作为公开来源说明可被应用
     * 原样再投递，但不能以它或旧epoch伪造本次入队的硬件身份/顺序。 */
    request.sequence_lo=request.sequence_hi=request.tick=request.epoch=request.flags=0;
    return gui_result(gui_call(SC32_GUI_POST,&request,0),SC32_GUI_POST);
}
static ScStatus gui_broadcast(const ScMessage *message)
{
    ScStatus first=0;gui_walk_depth++;
    /* walk期间仅延后回收现有节点，新增窗只插链头。重入关闭尚未遍历
     * 的窗不会使next悬空；本次广播也不会追逐回调新建的无限窗。 */
    for(sc32_gui_record *p=gui_all;p;p=p->all_next){
        if(p->state!=GUI_ACTIVE)continue;
        ScMessage local=*message;local.window=p->window;local.generation=p->generation;
        gui_hold(p);ScStatus status=gui_invoke(p,&local);
        if(status && !first)first=status;
        gui_leave(p);
    }
    gui_walk_depth--;gui_collect();return first;
}
ScStatus ScMessageDispatch(const ScMessage *message)
{
    if(gui_stopping)return gui_error(SC32_E_BUSY,SC32_GUI_GET);
    ScStatus checked=sc32_record_in(message,sizeof(ScMessage));if(checked<0)return gui_error(checked,SC32_GUI_GET);
    ScMessage local=*message;
    if(!local.window){
        if(local.id==SC32_MSG_QUEUELOSS)return gui_broadcast(&local);
        return gui_error(SC32_E_NOT_SUPPORTED,SC32_GUI_GET);
    }
    sc32_gui_record *record=gui_find(local.window);
    if(!record || record->generation!=local.generation){
        /* 显式Destroy已经同步回调，内核queued副本可能稍后才出队。
         * 无对应过程的DESTROY只忽略，不再次执行已释放的context。 */
        return local.id==SC32_MSG_DESTROY?SC32_OK:gui_error(SC32_E_STALE,SC32_GUI_GET);
    }
    if(record->state>=GUI_DESTROYING)return local.id==SC32_MSG_DESTROY?SC32_OK:gui_error(SC32_E_STALE,SC32_GUI_GET);
    if(local.id==SC32_MSG_CREATE)return gui_error(SC32_E_INVALID,SC32_GUI_GET);
    if(record->state==GUI_CREATING)return gui_error(SC32_E_BUSY,SC32_GUI_GET);
    gui_hold(record);
    Sc32WindowRef ref;gui_reference(record,&ref);ScWindowInfo info;sc32_zero(&info,sizeof(info));info.size=96;info.version=1;
    ScI32 raw=gui_call(SC32_GUI_QUERY,&ref,&info);
    if(local.id==SC32_MSG_DESTROY){
        if(raw!=SC32_E_STALE){gui_leave(record);return raw?gui_result(raw,SC32_GUI_QUERY):gui_error(SC32_E_INVALID,SC32_GUI_GET);}
        gui_walk_depth++;gui_retire_one(record,1,&local);gui_walk_depth--;gui_leave(record);gui_collect();return 0;
    }
    if(raw){gui_leave(record);return gui_result(raw,SC32_GUI_QUERY);}
    ScStatus status=gui_invoke(record,&local);gui_leave(record);return status;
}
ScStatus ScDefaultWindowProc(ScWindow window,const ScMessage *message,void *context)
{
    (void)context;ScStatus checked=sc32_record_in(message,sizeof(ScMessage));if(checked<0)return gui_error(checked,SC32_GUI_GET);
    if(message->window!=window)return gui_error(SC32_E_INVALID,SC32_GUI_GET);
    sc32_gui_record *record=gui_find(window);
    if(!record || record->generation!=message->generation)
        return message->id==SC32_MSG_DESTROY?SC32_OK:gui_error(SC32_E_STALE,SC32_GUI_GET);
    if(message->id==SC32_MSG_CLOSE)return ScWindowDestroy(window);
    /* 没有提交任何像素，就不能替应用确认PAINT；默认过程只处理关闭。
     * 生命周期/输入通知返回0，控件树与命令分发由后续Ring3 SCUI接入。 */
    return 0;
}
ScStatus ScMessageQueueQuery(ScQueueInfo *out)
{
    if(gui_stopping)return gui_error(SC32_E_BUSY,SC32_GUI_QUEUE);
    return gui_result(gui_call(SC32_GUI_QUEUE,0,out),SC32_GUI_QUEUE);
}
static void sc32_gui_finalize(void)
{
    gui_stopping=1;gui_walk_depth++;
    /* SCUI/应用库的fini可能已释放model，此时绝不进入用户DESTROY。
     * 仍对每个真实句柄尝试撤内核窗；父先关闭后孩子STALE也只静默退役。
     * 异常EXIT没有用户fini，既有内核owner链是独立兜底。 */
    for(sc32_gui_record *p=gui_all;p;p=p->all_next){
        if(p->state>=GUI_RETIRED)continue;
        if(p->window){
            Sc32WindowRef ref;gui_reference(p,&ref);
            (void)gui_call(SC32_GUI_DESTROY,&ref,0);
        }
        gui_retire_one(p,0,0);
    }
    gui_walk_depth--;gui_collect();
}
