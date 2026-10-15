/* 模型候选完整后才替换活动图标；坏LNK只忽略该条，读取故障保原快照。 */
static char *desk_copy(const char *s)
{
    ScU32 n=(ScU32)strlen(s);char *p=malloc(n+1);
    if(p)memcpy(p,s,n+1);
    return p;
}
static void desk_display_name(const char *source,ScU32 bytes,char *out,ScU32 capacity)
{
    ScU32 position=0,used=0;if(!capacity)return;
    /* 旧窗口/旧卷可含不完整字节；替代只用于显示，不改路径、exe或
     * window四元组。单个历史标题不能让整个桌面的UTF8绘制失败。 */
    while(position<bytes){ScU32 start=position,scalar=0;
        ScStatus s=ScUtf8Decode(source,bytes,&position,&scalar);
        if(s<0){position=start+1;if(used+1>=capacity)break;out[used++]='?';}
        else {ScU32 count=position-start;if(count>=capacity-used)break;
            memcpy(out+used,source+start,count);used+=count;}
    }
    out[used]=0;
}
static ScUiText desk_text(const char *s)
{
    ScUiText text={sizeof(ScUiText),1,s,(ScU32)strlen(s)};return text;
}
static ScU32 desk_path_key(const char *path,char key[68])
{
    ScU32 n=0;while(path[n] && n<67){unsigned char c=(unsigned char)path[n];
        key[n]=(char)(c>='a' && c<='z'?c-'a'+'A':c);n++;}key[n]=0;return n;
}
static int desk_reserve(void **p,ScU32 *capacity,ScU32 needed,ScU32 bytes)
{
    if(needed<=*capacity)return 1;
    if(!bytes || needed>0x3fffffffu/bytes)return 0;
    ScU32 n=*capacity?*capacity:16;
    while(n<needed){if(n>0x1fffffffu/bytes){n=needed;break;}n*=2;}
    void *next=realloc(*p,n*bytes);if(!next)return 0;
    *p=next;*capacity=n;return 1;
}
static void desk_items_free(DeskItem *items,ScU32 count)
{
    for(ScU32 i=0;i<count;i++){
        free(items[i].identity);free(items[i].label);free(items[i].path);
        ScShellShortcutRelease(items[i].shortcut);ScUiIconRelease(items[i].icon);
    }
    free(items);
}
static ScI32 desk_scale(ScI32 value)
{
    return (value*(ScI32)desk.metrics.scale_percent+50)/100;
}
static ScUiRect desk_item_rect(ScU32 index)
{
    DeskItem *item=desk.items+index;
    ScUiRect r={desk_scale(item->x)-desk.scroll_x,desk_scale(item->y)-desk.scroll_y,
        desk_scale((ScI32)desk.grid_w),desk_scale((ScI32)desk.grid_h)};
    if(desk.gesture==DESK_DRAG && desk.moved && item->selected){
        r.x+=desk.pointer_x-desk.down_x;r.y+=desk.pointer_y-desk.down_y;
    }
    return r;
}
static int desk_intersects(const ScUiRect *a,const ScUiRect *b)
{
    return a->x<b->x+b->width && b->x<a->x+a->width
        && a->y<b->y+b->height && b->y<a->y+a->height;
}
static void desk_invalidate_item(ScU32 i)
{
    if(i<desk.count){ScUiRect r=desk_item_rect(i);ScUiInvalidate(desk.ui,desk.canvas,&r);}
}
static ScU32 desk_cell_hash(ScI32 x,ScI32 y)
{
    return ((ScU32)x*0x9e3779b1u)^((ScU32)y*0x85ebca6bu);
}
static ScI32 desk_floor(ScI32 value,ScI32 unit)
{
    ScI32 q=value/unit;return value<0 && value%unit?q-1:q;
}
static DeskCell *desk_cell(ScI32 x,ScI32 y)
{
    ScU32 at=desk_cell_hash(x,y)&(desk.cell_capacity-1);
    for(ScU32 n=0;n<desk.cell_capacity;n++){
        DeskCell *cell=desk.cells+at;
        if(!cell->used || (cell->x==x && cell->y==y))return cell;
        at=(at+1)&(desk.cell_capacity-1);
    }
    return 0;
}
static ScStatus desk_index(void)
{
    /* 图标包围盒的宽高恒为一个cell；查询额外覆盖左/上邻格，拖动候选
     * 不写索引。鼠标静止不做全模型扫描；commit/缩放/重载才重建。 */
    ScU32 capacity=16;
    if(desk.count>0x10000000u)return SC_UI_E_RANGE;
    while(capacity<desk.count*2u)capacity*=2;
    DeskCell *cells=calloc(capacity,sizeof(*cells));if(!cells)return SC_UI_E_MEMORY;
    ScHash *names=0;ScHashDesc hd;memset(&hd,0,sizeof(hd));hd.size=sizeof(hd);hd.version=1;hd.value_bytes=sizeof(ScU32);
    ScStatus s=ScHashCreate(&hd,&names);
    for(ScU32 i=0;s==0 && i<desk.count;i++)if(!desk.items[i].removed){char key[68];ScU32 bytes=desk_path_key(desk.items[i].path,key);
        s=ScHashSet(names,key,bytes,&i,SC_HASH_INSERT_ONLY);}
    if(s<0){free(cells);ScHashRelease(names);return s;}
    ScHashRelease(desk.item_index);desk.item_index=names;
    free(desk.cells);desk.cells=cells;desk.cell_capacity=capacity;
    desk.extent_w=desk.extent_h=0;
    for(ScU32 i=0;i<desk.count;i++){
        DeskItem *item=desk.items+i;
        if(item->removed)continue;
        ScI32 right=desk_scale(item->x+(ScI32)desk.grid_w),bottom=desk_scale(item->y+(ScI32)desk.grid_h);
        if(right>desk.extent_w)desk.extent_w=right;
        if(bottom>desk.extent_h)desk.extent_h=bottom;
        DeskCell *cell=desk_cell(desk_floor(item->x,(ScI32)desk.grid_w),
            desk_floor(item->y,(ScI32)desk.grid_h));
        if(!cell)return SC_UI_E_FAILED;
        if(!cell->used){cell->used=1;cell->x=desk_floor(item->x,(ScI32)desk.grid_w);
            cell->y=desk_floor(item->y,(ScI32)desk.grid_h);cell->first=DESK_NONE;}
        item->next_cell=cell->first;cell->first=i;
    }
    return 0;
}
static ScU32 desk_hit(ScI32 x,ScI32 y)
{
    if(!desk.cell_capacity)return DESK_NONE;
    ScI32 lx=(x+desk.scroll_x)*100/(ScI32)desk.metrics.scale_percent;
    ScI32 ly=(y+desk.scroll_y)*100/(ScI32)desk.metrics.scale_percent;
    ScI32 cx=desk_floor(lx,(ScI32)desk.grid_w),cy=desk_floor(ly,(ScI32)desk.grid_h);
    ScU32 hit=DESK_NONE;
    for(ScI32 yy=cy-1;yy<=cy;yy++)for(ScI32 xx=cx-1;xx<=cx;xx++){
        DeskCell *cell=desk_cell(xx,yy);if(!cell || !cell->used)continue;
        for(ScU32 i=cell->first;i!=DESK_NONE;i=desk.items[i].next_cell){
            if(desk.items[i].removed)continue;
            ScUiRect r=desk_item_rect(i);
            if(x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height
                && (hit==DESK_NONE || i>hit))hit=i;
        }
    }
    return hit;
}
static ScStatus desk_layout_restore(void)
{
    ScShellLayout *layout=0;ScStatus s=ScShellCatalogLayout(desk.catalog,&layout);
    if(s<0)return s;
    ScShellLayoutInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    s=ScShellLayoutQuery(layout,&info);
    if(s==0){desk.layout_mode=info.mode;if(info.grid_width)desk.grid_w=info.grid_width;
        if(info.grid_height)desk.grid_h=info.grid_height;}
    /* 配置布局身份走hash，避免大目录逐条乘布局数；借用字符串只活到Release。 */
    ScHash *index=0;ScHashDesc hd;memset(&hd,0,sizeof(hd));hd.size=sizeof(hd);hd.version=1;
    hd.value_bytes=sizeof(ScU32);
    if(s==0)s=ScHashCreate(&hd,&index);
    for(ScU32 i=0;s==0 && i<desk.count;i++){char key[68];ScU32 bytes=desk_path_key(desk.items[i].identity,key);
        s=ScHashSet(index,key,bytes,&i,0);}
    for(ScU32 i=0;s==0 && i<info.count;i++){
        ScShellLayoutItem value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
        s=ScShellLayoutGet(layout,i,&value);if(s<0)break;
        if(value.identity[0]!='/' || strlen(value.identity)>64)continue;
        ScHashEntry found;memset(&found,0,sizeof(found));found.size=sizeof(found);found.version=1;
        char key[68];ScU32 bytes=desk_path_key(value.identity,key);
        ScStatus lookup=ScHashGet(index,key,bytes,&found);
        if(lookup==0){ScU32 at;memcpy(&at,found.value,sizeof(at));
            if(value.x<0 || value.y<0 || value.x>1048576 || value.y>1048576){s=SC_UI_E_RANGE;break;}
            desk.items[at].x=value.x;desk.items[at].y=value.y;}
        else if(lookup!=SC_UTIL_E_NOT_FOUND)s=lookup;
    }
    ScHashRelease(index);ScShellLayoutRelease(layout);
    if(s==0){ScShellCatalogInfo catalog;memset(&catalog,0,sizeof(catalog));catalog.size=sizeof(catalog);catalog.version=1;
        s=ScShellCatalogQuery(desk.catalog,&catalog);if(s==0)desk.layout_epoch=catalog.epoch;}
    return s;
}
static void desk_layout_changed(void)
{
    desk.layout_serial++;desk.layout_unsaved=desk.save_pending=desk.clock_dirty=1;
}
static ScStatus desk_layout_save(void)
{
    if(desk.store)return SC_SHELL_PENDING;
    ScShellLayoutItem *items=0;
    if(desk.count){items=calloc(desk.count,sizeof(*items));if(!items)return SC_UI_E_MEMORY;}
    ScU32 live=0;
    for(ScU32 i=0;i<desk.count;i++)if(!desk.items[i].removed){
        items[live].size=sizeof(*items);items[live].version=1;items[live].item=i+1;
        items[live].identity=desk.items[i].identity;items[live].x=desk.items[i].x;items[live].y=desk.items[i].y;
        items[live].width=(ScI32)desk.grid_w;items[live].height=(ScI32)desk.grid_h;items[live].order=i;live++;
    }
    ScShellLayout *layout=0;ScShellCatalogInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScShellLayoutCreate(items,live,desk.layout_mode,desk.grid_w,desk.grid_h,&layout);
    free(items);if(s==0)s=ScShellCatalogQuery(desk.catalog,&info);
    if(s==0 && info.epoch!=desk.save_epoch)s=SC_SHELL_E_STALE;
    if(s==0)s=ScShellCatalogApplyLayout(desk.catalog,layout,info.epoch);
    ScShellLayoutRelease(layout);
    if(s==0){s=ScShellCatalogQuery(desk.catalog,&info);if(s==0)desk.layout_epoch=info.epoch;}
    if(s==0)s=ScShellCatalogStoreRevision(desk.catalog,&desk.store_revision);
    if(s==0)s=ScShellStoreBegin(desk.catalog,desk.store_path,desk.store_revision,&desk.store);
    if(s==0){desk.save_pending=0;desk.store_serial=desk.layout_serial;}return s==0?SC_SHELL_PENDING:s;
}
static ScStatus desk_layout_apply(ScU32 mode)
{
    if(mode>SC_SHELL_LAYOUT_AUTO)return SC_UI_E_INVALID;
    ScU32 scale=desk.metrics.scale_percent;ScU32 width=desk.host.work_width*100/scale;
    ScU32 columns=width>48?(width-48)/desk.grid_w:1;if(!columns)columns=1;
    ScU32 previous_mode=desk.layout_mode;desk.layout_mode=mode;desk.save_epoch=desk.layout_epoch;
    for(ScU32 i=0;i<desk.count;i++){
        DeskItem *item=desk.items+i;
        item->old_x=item->x;item->old_y=item->y;
        if(mode==SC_SHELL_LAYOUT_AUTO){item->x=24+(ScI32)(i%columns*desk.grid_w);
            item->y=24+(ScI32)(i/columns*desk.grid_h);}
        else if(mode==SC_SHELL_LAYOUT_GRID){
            item->x=((item->x+desk.grid_w/2)/(ScI32)desk.grid_w)*(ScI32)desk.grid_w;
            item->y=((item->y+desk.grid_h/2)/(ScI32)desk.grid_h)*(ScI32)desk.grid_h;
        }
    }
    ScStatus s=desk_index();
    if(s<0){for(ScU32 i=0;i<desk.count;i++){desk.items[i].x=desk.items[i].old_x;desk.items[i].y=desk.items[i].old_y;}
        desk.layout_mode=previous_mode;}
    else desk_layout_changed();
    ScUiInvalidate(desk.ui,desk.canvas,0);return s;
}
static ScStatus desk_refresh(void)
{
    ScStatus s=0;
    if(!desk.refresh_directory){
        s=ScDirectoryOpen(desk.desktop_path,&desk.refresh_directory);if(s<0)return s;
        memset(&desk.refresh_page,0,sizeof(desk.refresh_page));
        desk.refresh_page.size=sizeof(desk.refresh_page);desk.refresh_page.version=1;
    }
    DeskItem *items=desk.refresh_items;ScU32 count=desk.refresh_count,capacity=desk.refresh_capacity;
    ScDirectoryEntry entries[8];
    s=ScDirectoryNext(desk.refresh_directory,entries,8,&desk.refresh_page);
    if(s==0){
        for(ScU32 i=0;i<desk.refresh_page.count;i++){
            ScDirectoryEntry *entry=entries+i;
            char path[68];int length=snprintf(path,sizeof(path),"%s/%s",desk.desktop_path,entry->name);
            if(length<0 || (ScU32)length>=sizeof(path)){s=SC_UI_E_RANGE;break;}
            if(!desk_reserve((void **)&items,&capacity,count+1,sizeof(*items))){s=SC_UI_E_MEMORY;break;}
            DeskItem value;memset(&value,0,sizeof(value));value.next_cell=value.next_icon=DESK_NONE;
            value.identity=desk_copy(path);value.path=desk_copy(path);value.label=desk_copy(entry->name);value.kind=entry->kind;
            value.observed_revision=entry->generation;
            if(!value.identity || !value.path || !value.label){free(value.identity);free(value.path);free(value.label);s=SC_UI_E_MEMORY;break;}
            desk_display_name(entry->name,(ScU32)strlen(entry->name),value.label,(ScU32)strlen(entry->name)+1);
            const char *extension=strrchr(entry->name,'.');
            if(extension && strlen(extension)==4 && (extension[1]=='L' || extension[1]=='l')
                && (extension[2]=='N' || extension[2]=='n') && (extension[3]=='K' || extension[3]=='k')){
                ScStatus link=ScShellShortcutRead(path,&value.shortcut);
                if(link==0){ScShellShortcutDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
                    link=ScShellShortcutDescribe(value.shortcut,&desc);
                    if(link==0){char *label=desk_copy(desc.label);if(!label)link=SC_UI_E_MEMORY;
                        else{free(value.label);value.label=label;}}
                }
                if(link<0){ScShellShortcutRelease(value.shortcut);value.shortcut=0;}
            }
            ScU32 logical=desk.host.work_width*100/desk.metrics.scale_percent;
            ScU32 columns=logical>48?(logical-48)/desk.grid_w:1;if(!columns)columns=1;
            value.x=24+(ScI32)(count%columns*desk.grid_w);value.y=24+(ScI32)(count/columns*desk.grid_h);
            items[count++]=value;
        }
    }
    if(s==0 && !(desk.refresh_page.flags&SC32_DIRECTORY_FINISHED)){
        desk.refresh_items=items;desk.refresh_count=count;desk.refresh_capacity=capacity;return SC_UI_PENDING;
    }
    ScStatus closed=ScDirectoryClose(desk.refresh_directory);desk.refresh_directory=0;
    desk.refresh_items=0;desk.refresh_count=desk.refresh_capacity=0;if(s==0)s=closed;
    if(s<0){desk_items_free(items,count);return s;}
    DeskItem *old=desk.items;ScU32 old_count=desk.count,old_capacity=desk.capacity;
    ScU32 new_focus=DESK_NONE,new_anchor=DESK_NONE;ScHash *old_index=0;
    ScHashDesc hd;memset(&hd,0,sizeof(hd));hd.size=sizeof(hd);hd.version=1;hd.value_bytes=sizeof(ScU32);
    s=ScHashCreate(&hd,&old_index);
    for(ScU32 i=0;s==0 && i<old_count;i++){char key[68];ScU32 bytes=desk_path_key(old[i].identity,key);
        s=ScHashSet(old_index,key,bytes,&i,0);}
    for(ScU32 i=0;s==0 && i<count;i++){
        ScHashEntry found;memset(&found,0,sizeof(found));found.size=sizeof(found);found.version=1;
        char key[68];ScU32 bytes=desk_path_key(items[i].identity,key);
        ScStatus lookup=ScHashGet(old_index,key,bytes,&found);
        if(lookup==0){ScU32 at;memcpy(&at,found.value,sizeof(at));
            if(items[i].observed_revision==old[at].observed_revision && items[i].kind==old[at].kind){
                items[i].selected=old[at].selected;if(at==desk.focus)new_focus=i;if(at==desk.anchor)new_anchor=i;}}
        else if(lookup!=SC_UTIL_E_NOT_FOUND)s=lookup;
    }
    ScHashRelease(old_index);if(s<0){desk_items_free(items,count);return s;}
    DeskCell *old_cells=desk.cells;ScU32 old_cell_capacity=desk.cell_capacity;
    ScHash *old_names=desk.item_index;
    ScU32 mode=desk.layout_mode,gw=desk.grid_w,gh=desk.grid_h;
    desk.items=items;desk.count=count;desk.capacity=capacity;desk.cells=0;desk.cell_capacity=0;desk.item_index=0;
    s=desk_layout_restore();if(s==SC_SHELL_E_NOT_FOUND)s=0;
    if(s==0)s=desk_index();
    if(s<0){desk_items_free(desk.items,desk.count);free(desk.cells);ScHashRelease(desk.item_index);desk.item_index=old_names;
        desk.items=old;desk.count=old_count;desk.capacity=old_capacity;
        desk.cells=old_cells;desk.cell_capacity=old_cell_capacity;
        desk.layout_mode=mode;desk.grid_w=gw;desk.grid_h=gh;return s;}
    desk_items_free(old,old_count);free(old_cells);ScHashRelease(old_names);
    desk.loaded_icons=DESK_NONE;desk.icons_pending=1;
    desk.focus=new_focus!=DESK_NONE?new_focus:count?0:DESK_NONE;
    desk.anchor=new_anchor!=DESK_NONE?new_anchor:desk.focus;
    desk.refresh_pending=0;ScUiInvalidate(desk.ui,desk.canvas,0);return 0;
}
static void desk_icon_unlink(ScU32 index)
{
    ScU32 *slot=&desk.loaded_icons;
    while(*slot!=DESK_NONE){DeskItem *item=desk.items+*slot;
        if(*slot==index){*slot=item->next_icon;item->next_icon=DESK_NONE;return;}slot=&item->next_icon;}
}
static ScStatus desk_icons_step(void)
{
    /* SCSHELL已拥有有预算的资源cache。本桌面只强持当前视口的Icon：
     * cache不能收回仍被每个隐藏item强持的图片，所以离屏引用必须另撤。
     * 图标IO分4项小批、在Paint之外；隐藏会话不生成/读取候选图片。 */
    if(!desk.visible || !desk.cell_capacity)return 0;
    ScUiRect viewport={0,0,(ScI32)desk.host.work_width,(ScI32)desk.host.work_height};
    ScU32 *slot=&desk.loaded_icons;
    while(*slot!=DESK_NONE){ScU32 i=*slot;DeskItem *item=desk.items+i;ScUiRect r=desk_item_rect(i);
        if(item->removed || !desk_intersects(&r,&viewport)){*slot=item->next_icon;item->next_icon=DESK_NONE;
            ScUiIconRelease(item->icon);item->icon=0;}
        else slot=&item->next_icon;
    }
    ScI32 scale=(ScI32)desk.metrics.scale_percent;
    ScI32 x0=desk_floor(desk.scroll_x*100/scale,(ScI32)desk.grid_w)-1;
    ScI32 x1=desk_floor(((ScI32)desk.host.work_width+desk.scroll_x)*100/scale,(ScI32)desk.grid_w);
    ScI32 y0=desk_floor(desk.scroll_y*100/scale,(ScI32)desk.grid_h)-1;
    ScI32 y1=desk_floor(((ScI32)desk.host.work_height+desk.scroll_y)*100/scale,(ScI32)desk.grid_h);
    ScU32 attempted=0;desk.icons_pending=0;
    for(ScI32 y=y0;y<=y1;y++)for(ScI32 x=x0;x<=x1;x++){
        DeskCell *cell=desk_cell(x,y);if(!cell || !cell->used)continue;
        for(ScU32 i=cell->first;i!=DESK_NONE;i=desk.items[i].next_cell){DeskItem *item=desk.items+i;
            if(item->removed || item->icon || item->icon_failed || !item->shortcut)continue;
            ScUiRect r=desk_item_rect(i);if(!desk_intersects(&r,&viewport))continue;
            if(attempted>=4){desk.icons_pending=1;return 0;}attempted++;
            ScShellShortcutDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
            ScStatus s=ScShellShortcutDescribe(item->shortcut,&desc);
            if(s==0)s=ScShellIconLoad(desk.ui,desc.icon,desc.target,&item->icon);
            if(s<0){item->icon_failed=1;desk.icon_errors++;
                fprintf(stderr,"SCSHELL icon unavailable=%d source=%s\n",s,item->path);}
            else {item->next_icon=desk.loaded_icons;desk.loaded_icons=i;desk_invalidate_item(i);}
        }
    }
    return 0;
}
