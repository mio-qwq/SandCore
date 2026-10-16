static ScUiText f2_text(const char *z)
{ScUiText text={sizeof(text),1,z,(ScU32)strlen(z)};return text;}
static const char *f2_leaf(const char *path)
{const char *leaf=strrchr(path,'/');return leaf && leaf[1]?leaf+1:path;}
static ScU32 f2_fold(ScU32 c)
{return c>='a' && c<='z'?c-'a'+'A':c;}
static ScI32 f2_compare_text(const char *a,const char *b)
{
    for(;;a++,b++){ScU32 x=f2_fold((unsigned char)*a),y=f2_fold((unsigned char)*b);
        if(x!=y)return x<y?-1:1;
        if(!x)return 0;}
}
static ScU32 f2_key(const char *path,char out[F2_PATH])
{ScU32 i=0;while(path[i] && i<F2_PATH-1){out[i]=(char)f2_fold((unsigned char)path[i]);i++;}out[i]=0;return i;}
static ScU32 f2_match(const char *text,const char *needle)
{
    if(!*needle)return 1;
    for(;*text;text++){ScU32 i=0;
        while(needle[i] && text[i] && f2_fold((unsigned char)needle[i])==f2_fold((unsigned char)text[i]))i++;
        if(!needle[i])return 1;}
    return 0;
}
static ScStatus f2_reserve(void **items,ScU32 *capacity,ScU32 count,ScU32 bytes)
{
    if(count<=*capacity)return 0;
    ScU32 next=*capacity?*capacity:16;
    while(next<count){if(next>UINT_MAX/2){next=count;break;}next*=2;}
    if(!bytes || next>UINT_MAX/bytes)return SC_UI_E_RANGE;
    void *candidate=realloc(*items,next*bytes);if(!candidate)return SC_UI_E_MEMORY;
    *items=candidate;*capacity=next;return 0;
}
static ScStatus f2_hash(ScU32 bytes,ScDropProc drop,ScHash **out)
{
    ScHashDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.value_bytes=bytes;desc.drop=drop;return ScHashCreate(&desc,out);
}
static ScStatus f2_lookup(ScHash *hash,const void *key,ScU32 bytes,ScU32 *index)
{
    ScHashEntry found;memset(&found,0,sizeof(found));found.size=sizeof(found);found.version=1;
    ScStatus s=ScHashGet(hash,key,bytes,&found);if(s==0)memcpy(index,found.value,4);return s;
}
static ScStatus f2_absolute(const char *path,const char *base,char out[F2_PATH])
{
    /* 输入保持完整到公开Normalize/Resolve，拒绝空格与超长时不截断。
     * 公开绝对路径比SandFS内部63B名字多一个根斜线，最大64B。 */
    ScString *normalized=0;const char *z=0;
    ScStatus s=ScPathNormalize(path,base,SC_PATH_BACKSLASH|SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT,&normalized);
    if(s==0)s=ScStringCStr(normalized,&z);
    if(s==0 && (strlen(z)>64 || strchr(z,' ')))s=SC32_E_RANGE;
    ScU32 length=0;if(s==0)s=ScPathResolve(z,out,F2_PATH,&length);
    ScStringRelease(normalized);return s;
}
static ScStatus f2_join(const char *directory,const char *leaf,char out[F2_PATH])
{
    ScString *joined=0;const char *z=0;
    ScStatus s=ScPathJoin(directory,leaf,SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT,&joined);
    if(s==0)s=ScStringCStr(joined,&z);
    if(s==0)s=f2_absolute(z,"/",out);
    ScStringRelease(joined);return s;
}
static ScStatus f2_query(const char *path,ScPathInfo *info)
{memset(info,0,sizeof(*info));info->size=sizeof(*info);info->version=1;return ScPathQuery(path,info);}
static ScStatus f2_check_item(const ScShellFileItem *item)
{
    ScPathInfo current;ScStatus s=f2_query(item->source,&current);if(s<0)return s;
    if(current.kind!=item->kind || ((item->flags&SC_SHELL_ITEM_EXPECT_SOURCE)
        && current.generation!=item->expected_source_revision))return SC_SHELL_E_STALE;
    return 0;
}
static void f2_snapshot_free(F2Snapshot *snapshot)
{
    if(!snapshot)return;
    free(snapshot->rows);free(snapshot->order);free(snapshot->rank);
    ScHashRelease(snapshot->paths);ScHashRelease(snapshot->ids);free(snapshot);
}
static ScStatus f2_snapshot_create(const char *path,const char *query,F2Snapshot **out)
{
    F2Snapshot *snapshot=calloc(1,sizeof(*snapshot));if(!snapshot)return SC_UI_E_MEMORY;
    ScStatus s=f2_hash(4,0,&snapshot->paths);if(s==0)s=f2_hash(4,0,&snapshot->ids);
    if(s<0){f2_snapshot_free(snapshot);return s;}
    strcpy(snapshot->path,path);if(query){strcpy(snapshot->query,query);snapshot->search=1;}
    *out=snapshot;return 0;
}
static void f2_kind_label(F2Entry *row)
{
    if(row->kind==SC32_PATH_DIRECTORY){strcpy(row->type,"文件夹");return;}
    const char *extension=strrchr(f2_leaf(row->path),'.');
    if(!extension || !extension[1]){strcpy(row->type,"文件");return;}
    if(!f2_compare_text(extension,".SCX")){strcpy(row->type,"应用程序");return;}
    if(!f2_compare_text(extension,".SCL")){strcpy(row->type,"共享库");return;}
    if(!f2_compare_text(extension,".LNK")){strcpy(row->type,"快捷方式");return;}
    /* 类型是分类而非身份；不按固定字节截断用户扩展名，避免把UTF8
     * 的半个汉字交给公共Provider，导致整个可见帧校验失败。 */
    if(!f2_compare_text(extension,".C") || !f2_compare_text(extension,".H")
        || !f2_compare_text(extension,".INC") || !f2_compare_text(extension,".ASM"))strcpy(row->type,"源代码");
    else if(!f2_compare_text(extension,".TXT") || !f2_compare_text(extension,".CFG") || !f2_compare_text(extension,".LOG"))strcpy(row->type,"文本");
    else if(!f2_compare_text(extension,".SCB") || !f2_compare_text(extension,".BMP") || !f2_compare_text(extension,".PNG")
        || !f2_compare_text(extension,".JPG") || !f2_compare_text(extension,".JPEG") || !f2_compare_text(extension,".WEBP"))strcpy(row->type,"图像");
    else strcpy(row->type,"文件");
}
static ScStatus f2_snapshot_add(F2Snapshot *snapshot,const char *path,const ScDirectoryEntry *entry)
{
    ScStatus valid=ScUtf8Validate(path,(ScU32)strlen(path),0);if(valid<0)return valid;
    char key[F2_PATH];ScU32 key_bytes=f2_key(path,key),existing;
    if(f2_lookup(snapshot->paths,key,key_bytes,&existing)==0)return SC_UI_E_EXISTS;
    if(snapshot->count==UINT_MAX)return SC_UI_E_RANGE;
    ScStatus s=f2_reserve((void **)&snapshot->rows,&snapshot->capacity,snapshot->count+1,sizeof(F2Entry));if(s<0)return s;
    F2Entry row;memset(&row,0,sizeof(row));strcpy(row.path,path);
    row.kind=entry->kind;row.bytes=entry->length;row.uid=entry->uid;row.gid=entry->gid;
    row.rw=entry->rw;row.revision=entry->generation;
    /* 刷新只继承同名且同修订的行身份；同名删后重建绝不继承选择。 */
    if(f2.current && f2_lookup(f2.current->paths,key,key_bytes,&existing)==0){
        F2Entry *old=f2.current->rows+existing;
        if(old->kind==row.kind && old->revision==row.revision)row.id=old->id;
    }
    if(!row.id){if(!f2.next_item || f2.next_item==UINT_MAX)return SC_UI_E_RANGE;row.id=f2.next_item++;}
    snprintf(row.owner,sizeof(row.owner),"%d:%d",row.uid,row.gid);
    snprintf(row.access,sizeof(row.access),"%c%c/%c%c/%c%c",
        row.rw&1?'r':'-',row.rw&2?'w':'-',row.rw&4?'r':'-',row.rw&8?'w':'-',row.rw&16?'r':'-',row.rw&32?'w':'-');f2_kind_label(&row);
    ScU32 index=snapshot->count;
    s=ScHashSet(snapshot->paths,key,key_bytes,&index,SC_HASH_INSERT_ONLY);
    if(s==0)s=ScHashSet(snapshot->ids,&row.id,4,&index,SC_HASH_INSERT_ONLY);
    if(s<0)return s;
    snapshot->rows[snapshot->count++]=row;return 0;
}
static void f2_scan_cancel(void)
{
    if(f2.scan.directory)ScDirectoryClose(f2.scan.directory);
    f2_snapshot_free(f2.scan.snapshot);free(f2.scan.queue);memset(&f2.scan,0,sizeof(f2.scan));
}
static ScStatus f2_scan_queue(const char *path)
{
    ScStatus s=f2_reserve((void **)&f2.scan.queue,&f2.scan.capacity,f2.scan.count+1,sizeof(F2Path));
    if(s==0)strcpy(f2.scan.queue[f2.scan.count++].path,path);
    return s;
}
static ScStatus f2_navigate(const char *path,ScU32 mode,ScU32 history_index,const char *query)
{
    if(f2.exit_pending)return SC_UI_E_BUSY;
    char absolute[F2_PATH];ScStatus s=f2_absolute(path,f2.current?f2.current->path:"/",absolute);
    if(s<0)return s;
    ScPathInfo info;s=f2_query(absolute,&info);if(s<0)return s;
    if(info.kind!=SC32_PATH_DIRECTORY)return SC32_E_INVALID;
    if(query && (!*query || strlen(query)>=F2_QUERY))return SC32_E_RANGE;
    F2Snapshot *candidate=0;s=f2_snapshot_create(absolute,query,&candidate);if(s<0)return s;
    candidate->directory_revision=info.generation;
    /* 新请求先取得合法候选再取消旧请求。快速导航失败不会丢上一结果。 */
    ScU32 cancelled_sort=f2.order.active && !f2.order.publish;
    f2_order_cancel();if(cancelled_sort)f2_table_notify(SC_UI_DATA_SORTED);
    f2_scan_cancel();f2_gesture_cancel(1);
    f2.scan.snapshot=candidate;f2.scan.active=1;f2.scan.nav_mode=mode;f2.scan.history_index=history_index;
    s=f2_scan_queue(absolute);if(s<0){f2_scan_cancel();return s;}
    f2_status(query?"正在搜索；Esc 可取消，当前列表保留":"正在读取目录；当前列表保留");
    ScUiNodeSetState(f2.ui,f2.scan_progress,SC_UI_VISIBLE,SC_UI_VISIBLE);
    ScUiProgressSet(f2.ui,f2.scan_progress,0,0,SC_UI_PROGRESS_INDETERMINATE);return 0;
}
static ScStatus f2_scan_step(void)
{
    if(!f2.scan.active)return 0;
    F2Scan *scan=&f2.scan;F2Snapshot *snapshot=scan->snapshot;
    if(!scan->directory){
        if(scan->cursor>=scan->count){
            ScStatus s=f2_order_begin(snapshot,1,f2.sort_field,f2.sort_descending,scan->nav_mode,scan->history_index);
            if(s<0){f2_scan_cancel();return s;}
            scan->snapshot=0;f2_scan_cancel();return 0;
        }
        strcpy(scan->enumerating,scan->queue[scan->cursor++].path);
        ScStatus s=ScDirectoryOpen(scan->enumerating,&scan->directory);
        if(s<0){
            if(snapshot->search && scan->cursor>1 && (s==SC32_E_ACCESS || s==SC32_E_NOT_FOUND)){
                snapshot->skipped++;return 0;}
            f2_scan_cancel();return s;
        }
        scan->visited++;
    }
    ScDirectoryEntry entries[F2_BATCH];ScDirectoryPage page;memset(&page,0,sizeof(page));page.size=sizeof(page);page.version=1;
    ScStatus s=ScDirectoryNext(scan->directory,entries,F2_BATCH,&page);
    for(ScU32 i=0;s==0 && i<page.count;i++){
        if(!memchr(entries[i].name,0,sizeof(entries[i].name)) || !entries[i].name[0]
            || strchr(entries[i].name,'/') || strchr(entries[i].name,'\\')){s=SC_UI_E_INVALID;break;}
        char path[F2_PATH];s=f2_join(scan->enumerating,entries[i].name,path);if(s<0)break;
        if(snapshot->search && entries[i].kind==SC32_PATH_DIRECTORY)s=f2_scan_queue(path);
        if(s==0 && (!snapshot->search || f2_match(entries[i].name,snapshot->query)))s=f2_snapshot_add(snapshot,path,entries+i);
    }
    if(s<0){f2_scan_cancel();return s;}
    if(page.flags&SC32_DIRECTORY_FINISHED){s=ScDirectoryClose(scan->directory);scan->directory=0;if(s<0){f2_scan_cancel();return s;}}
    char text[160];snprintf(text,sizeof(text),snapshot->search?"搜索中：%u 项匹配 · %u 个目录 · %u 个不可读目录":"已读取 %u 项 · %u 个目录 · %u 个不可读目录",
        snapshot->count,scan->visited,snapshot->skipped);f2_status(text);return 0;
}
static void f2_order_cancel(void)
{
    if(f2.order.publish)f2_snapshot_free(f2.order.snapshot);
    free(f2.order.order);free(f2.order.scratch);free(f2.order.rank);memset(&f2.order,0,sizeof(f2.order));
}
static ScStatus f2_order_begin(F2Snapshot *snapshot,ScU32 publish,ScU32 field,ScU32 descending,ScU32 nav_mode,ScU32 history_index)
{
    if(!snapshot)return SC_UI_E_INVALID;
    if(snapshot->count>UINT_MAX/4)return SC_UI_E_RANGE;
    ScU32 bytes=snapshot->count?snapshot->count*4:4;
    ScU32 *order=malloc(bytes),*scratch=malloc(bytes),*rank=malloc(bytes);
    if(!order || !scratch || !rank){free(order);free(scratch);free(rank);return SC_UI_E_MEMORY;}
    f2_order_cancel();F2Order *sort=&f2.order;sort->active=1;sort->snapshot=snapshot;
    sort->publish=publish;sort->field=field;sort->descending=descending;sort->order=order;sort->scratch=scratch;
    sort->rank=rank;sort->width=1;sort->nav_mode=nav_mode;sort->history_index=history_index;
    strcpy(sort->filter,f2.filter_text);return 0;
}
static ScI32 f2_compare_rows(F2Order *sort,ScU32 a,ScU32 b)
{
    const F2Entry *left=sort->snapshot->rows+a,*right=sort->snapshot->rows+b;
    if(left->kind!=right->kind)return left->kind==SC32_PATH_DIRECTORY?-1:1;
    ScI32 result=0;
    if(sort->field==1)result=f2_compare_text(left->type,right->type);
    else if(sort->field==2)result=left->bytes<right->bytes?-1:left->bytes>right->bytes?1:0;
    else if(sort->field==3){result=left->uid<right->uid?-1:left->uid>right->uid?1:0;
        if(!result)result=left->gid<right->gid?-1:left->gid>right->gid?1:0;}
    else if(sort->field==4)result=left->rw<right->rw?-1:left->rw>right->rw?1:0;
    else if(sort->field==5)result=left->revision<right->revision?-1:left->revision>right->revision?1:0;
    else result=f2_compare_text(sort->field==6?left->path:f2_leaf(left->path),sort->field==6?right->path:f2_leaf(right->path));
    if(!result)result=f2_compare_text(left->path,right->path);
    return sort->descending?-result:result;
}
static ScStatus f2_table_notify(ScU32 kind)
{
    if(f2.table_revision==UINT_MAX)return SC_UI_E_RANGE;
    ScUiDataChange change;memset(&change,0,sizeof(change));change.size=sizeof(change);change.version=1;
    change.kind=kind;change.revision=++f2.table_revision;return ScUiProviderNotify(f2.table_provider,&change);
}
static ScStatus f2_publish_order(void)
{
    F2Order *sort=&f2.order;F2Snapshot *snapshot=sort->snapshot;
    /* 历史容量也在提交前保证，OOM不会产生画面已导航而历史未记录。 */
    ScStatus s=0;ScArray *selected=0;
    if(sort->publish && (sort->nav_mode==F2_NAV_NEW || !f2.history_count))
        s=f2_reserve((void **)&f2.history,&f2.history_capacity,f2.history_index+2,sizeof(F2Path));
    if(s==0)s=ScUiSelectionCopy(f2.selection,&selected);
    if(s<0)return s;
    F2Snapshot *old=f2.current;
    free(snapshot->order);free(snapshot->rank);snapshot->order=sort->order;snapshot->rank=sort->rank;
    snapshot->visible=sort->count;sort->order=sort->rank=0;
    if(sort->publish){
        f2.current=snapshot;sort->publish=0;
        if(sort->nav_mode==F2_NAV_HISTORY)f2.history_index=sort->history_index;
        else if(sort->nav_mode==F2_NAV_NEW || !f2.history_count){
            if(f2.history_count && !f2_compare_text(f2.history[f2.history_index].path,snapshot->path)){}
            else {if(f2.history_count)f2.history_index++;
                strcpy(f2.history[f2.history_index].path,snapshot->path);f2.history_count=f2.history_index+1;}
        }
        ScUiText path=f2_text(snapshot->path);ScUiNodeSetText(f2.ui,f2.address,&path);
        char title[120];snprintf(title,sizeof(title),snapshot->search?"Files 2 · 搜索 %s":"Files 2 · %s",snapshot->path);
        ScUiText heading=f2_text(title);ScUiWindowSetTitle(f2.ui,f2.root,&heading);
    }
    f2.sort_field=sort->field;f2.sort_descending=sort->descending;f2.publishing=1;
    s=f2_table_notify(old!=snapshot?SC_UI_DATA_RESET:SC_UI_DATA_SORTED);ScUiSelectionClear(f2.selection);
    ScContainerInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    if(ScArrayQuery(selected,&info)==0)for(ScU32 i=0;i<info.length;i++){
        ScU32 id,index;if(ScArrayGet(selected,i,&id)==0 && f2_lookup(snapshot->ids,&id,4,&index)==0
            && snapshot->rank[index]!=F2_NONE)ScUiSelectionSet(f2.selection,id,SC_UI_ADD);
    }
    ScArrayRelease(selected);f2.publishing=0;
    if(old!=snapshot)f2_snapshot_free(old);
    f2_history_refresh();f2_places_refresh();
    if(f2.pending_file[0]){char key[F2_PATH];ScU32 index;
        if(f2_lookup(snapshot->paths,key,f2_key(f2.pending_file,key),&index)==0){
            ScUiSelectionSet(f2.selection,snapshot->rows[index].id,SC_UI_REPLACE);
            ScUiViewEnsureItem(f2.ui,f2.table,snapshot->rows[index].id);}
        f2.pending_file[0]=0;}
    char message[200];snprintf(message,sizeof(message),snapshot->search?"搜索完成：%u / %u 项 · %u 个目录未能读取":"%u / %u 项 · %u 个目录未能读取",
        snapshot->visible,snapshot->count,snapshot->skipped);f2_status(message);
    ScUiNodeSetState(f2.ui,f2.scan_progress,SC_UI_VISIBLE,0);
    free(sort->scratch);memset(sort,0,sizeof(*sort));return s;
}
static ScStatus f2_order_advance(void)
{
    F2Order *sort=&f2.order;if(!sort->active)return 0;
    F2Snapshot *snapshot=sort->snapshot;ScU32 budget=1024;
    if(sort->phase==0){
        while(sort->position<snapshot->count && budget--){ScU32 i=sort->position++;sort->rank[i]=F2_NONE;
            if(f2_match(snapshot->search?snapshot->rows[i].path:f2_leaf(snapshot->rows[i].path),sort->filter))sort->order[sort->count++]=i;}
        if(sort->position==snapshot->count){sort->phase=1;sort->position=0;}return 0;
    }
    if(sort->phase==1){
        while(budget-- && sort->width<sort->count){
            if(sort->left>=sort->count){ScU32 *swap=sort->order;sort->order=sort->scratch;sort->scratch=swap;
                sort->left=0;sort->merging=0;
                sort->width=sort->width>sort->count/2?sort->count:sort->width*2;continue;}
            if(!sort->merging){sort->a=sort->left;sort->middle=sort->left+sort->width;
                if(sort->middle>sort->count)sort->middle=sort->count;
                sort->last=sort->middle+sort->width;if(sort->last<sort->middle || sort->last>sort->count)sort->last=sort->count;
                sort->b=sort->middle;sort->out=sort->left;sort->merging=1;}
            if(sort->out==sort->last){sort->left=sort->last;sort->merging=0;continue;}
            if(sort->a<sort->middle && (sort->b>=sort->last || f2_compare_rows(sort,sort->order[sort->a],sort->order[sort->b])<=0))
                sort->scratch[sort->out++]=sort->order[sort->a++];
            else sort->scratch[sort->out++]=sort->order[sort->b++];
        }
        if(sort->width>=sort->count){sort->phase=2;sort->position=0;}return 0;
    }
    while(sort->position<sort->count && budget--){ScU32 i=sort->position++;sort->rank[sort->order[i]]=i;}
    if(sort->position==sort->count)return SC_UI_END;
    return 0;
}
static ScStatus f2_order_step(void)
{ScStatus s=f2_order_advance();return s==SC_UI_END?f2_publish_order():s;}
static void f2_icon_drop(void *item,void *context)
{(void)context;ScUiIconRelease(*(ScUiIcon **)item);}
static ScUiIcon *f2_icon(F2Entry *row)
{
    char key[72];const char *ext=strrchr(f2_leaf(row->path),'.');
    const char *type=row->kind==SC32_PATH_DIRECTORY?"directory":ext?ext+1:"";
    snprintf(key,sizeof(key),"%s",type);for(ScU32 i=0;key[i];i++)key[i]=(char)f2_fold((unsigned char)key[i]);
    ScHashEntry found;memset(&found,0,sizeof(found));found.size=sizeof(found);found.version=1;
    if(ScHashGet(f2.icons,key,(ScU32)strlen(key),&found)==0){ScUiIcon *icon;memcpy(&icon,found.value,sizeof(icon));return icon;}
    ScUiIcon *icon=0;ScShellApplication *app=0;const char *spec=row->kind==SC32_PATH_DIRECTORY?"@FILES":"@NOTE",*executable=0;
    ScShellAssociationInfo association;memset(&association,0,sizeof(association));association.size=sizeof(association);association.version=1;
    if(f2.catalog && ScShellCatalogAssociation(f2.catalog,type,"open",&association)==0
        && ScShellCatalogApplication(f2.catalog,association.application_token,&app)==0){
        ScShellApplicationDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
        if(ScShellApplicationDescribe(app,&desc)==0){spec=desc.icon;executable=desc.executable;}
    }else if(ext && !f2_compare_text(ext,".SCX"))spec="@SHELL";
    ScStatus s=ScShellIconLoad(f2.ui,spec,executable,&icon);ScShellApplicationRelease(app);
    if(s<0)return 0;
    if(ScHashSet(f2.icons,key,(ScU32)strlen(key),&icon,0)<0){ScUiIconRelease(icon);return 0;}return icon;
}
static ScStatus f2_table_data(void *context,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    (void)context;F2Snapshot *snapshot=f2.current;reply->revision=f2.table_revision;
    if(request->operation==SC_UI_DATA_COUNT || request->operation==SC_UI_DATA_CHILD_COUNT){reply->total=snapshot?snapshot->visible:0;return 0;}
    if(request->operation==SC_UI_DATA_SORT){
        if(!snapshot || f2.scan.active || (f2.order.active && f2.order.publish))return SC_UI_E_BUSY;
        ScStatus s=f2_order_begin(snapshot,0,request->field,request->flags&1,0,0);return s<0?s:SC_UI_PENDING;
    }
    if(!snapshot)return SC_UI_END;
    if(request->operation==SC_UI_DATA_FIND_PREFIX){
        if(reply->value.type!=SC_VALUE_UTF8 || reply->value.bytes>=F2_QUERY)return SC_UI_E_INVALID;
        char needle[F2_QUERY];memcpy(needle,reply->value.data,reply->value.bytes);needle[reply->value.bytes]=0;
        /* 键盘前缀在内存索引查找；不因一个按键重新读目录。 */
        for(ScU32 i=0;i<snapshot->visible;i++){ScU32 rank=(request->index+i)%snapshot->visible;
            F2Entry *row=snapshot->rows+snapshot->order[rank];const char *label=f2_leaf(row->path);ScU32 j=0;
            while(needle[j] && label[j] && f2_fold((unsigned char)needle[j])==f2_fold((unsigned char)label[j]))j++;
            if(!needle[j]){reply->item=row->id;reply->total=rank;reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;return 0;}
            if((i&2047u)==2047u)ScProcessYield();}
        return SC_UI_E_NOT_FOUND;
    }
    ScU32 index;
    if(request->operation==SC_UI_DATA_CHILD_AT){if(request->index>=snapshot->visible)return SC_UI_END;index=snapshot->order[request->index];}
    else if(f2_lookup(snapshot->ids,&request->item,4,&index)<0)return SC_UI_E_STALE;
    F2Entry *row=snapshot->rows+index;reply->item=row->id;reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(request->operation==SC_UI_DATA_INDEX_OF){if(snapshot->rank[index]==F2_NONE)return SC_UI_E_NOT_FOUND;reply->total=snapshot->rank[index];return 0;}
    if(request->operation==SC_UI_DATA_ITEM){reply->icon=f2.visible?f2_icon(row):0;return 0;}
    if(request->operation==SC_UI_DATA_CELL){
        if(request->field==2 || request->field==5){reply->value.type=SC_VALUE_U32;reply->value.lo=request->field==2?row->bytes:row->revision;}
        else {const char *text=request->field==1?row->type:request->field==3?row->owner:request->field==4?row->access:request->field==6?row->path:f2_leaf(row->path);
            reply->value.type=SC_VALUE_UTF8;reply->value.data=text;reply->value.bytes=(ScU32)strlen(text);}return 0;
    }
    return 0;
}

static ScStatus f2_tree_notify(void)
{
    if(f2.tree_revision==UINT_MAX)return SC_UI_E_RANGE;
    ScUiDataChange change;memset(&change,0,sizeof(change));change.size=sizeof(change);change.version=1;
    change.kind=SC_UI_DATA_RESET;change.revision=++f2.tree_revision;return ScUiProviderNotify(f2.tree_provider,&change);
}
static ScStatus f2_tree_add(const char *path,ScU32 parent,ScU32 sibling,ScU32 revision,ScU32 *out)
{
    char key[F2_PATH];ScU32 index,bytes=f2_key(path,key);
    if(f2_lookup(f2.tree_index,key,bytes,&index)==0){F2TreeNode *node=f2.nodes+index;
        if(node->revision!=revision){node->loaded=0;node->revision=revision;node->error=0;}
        node->parent=parent;node->sibling=sibling;*out=node->id;return 0;}
    if(f2.node_count>=0x7ffffffeu)return SC_UI_E_RANGE;
    ScStatus s=f2_reserve((void **)&f2.nodes,&f2.node_capacity,f2.node_count+1,sizeof(F2TreeNode));if(s<0)return s;
    index=f2.node_count;s=ScHashSet(f2.tree_index,key,bytes,&index,SC_HASH_INSERT_ONLY);if(s<0)return s;
    F2TreeNode *node=f2.nodes+f2.node_count++;memset(node,0,sizeof(*node));
    node->id=index+1;node->parent=parent;node->sibling=sibling;node->revision=revision;strcpy(node->path,path);*out=node->id;return 0;
}
static ScStatus f2_tree_enqueue(ScU32 id)
{
    if(!id || id>f2.node_count)return SC_UI_E_STALE;
    F2TreeNode *node=f2.nodes+id-1;if(node->queued || node->loaded)return 0;
    ScStatus s=f2_reserve((void **)&f2.tree_queue,&f2.tree_capacity,f2.tree_count+1,4);if(s<0)return s;
    f2.tree_queue[f2.tree_count++]=id;node->queued=1;return 0;
}
static void f2_tree_scan_clear(void)
{if(f2.tree_scan.directory)ScDirectoryClose(f2.tree_scan.directory);free(f2.tree_scan.children);memset(&f2.tree_scan,0,sizeof(f2.tree_scan));}
static ScStatus f2_tree_step(void)
{
    if(!f2.visible)return 0;
    F2TreeScan *scan=&f2.tree_scan;ScStatus s=0;
    if(!scan->node){
        if(f2.tree_cursor>=f2.tree_count){f2.tree_count=f2.tree_cursor=0;return 0;}
        scan->node=f2.tree_queue[f2.tree_cursor++];F2TreeNode *node=f2.nodes+scan->node-1;
        if(node->loaded){node->queued=0;f2_tree_scan_clear();return 0;}
        s=ScDirectoryOpen(node->path,&scan->directory);
    }
    ScDirectoryEntry entries[F2_BATCH];ScDirectoryPage page;memset(&page,0,sizeof(page));page.size=sizeof(page);page.version=1;
    if(s==0)s=ScDirectoryNext(scan->directory,entries,F2_BATCH,&page);
    for(ScU32 i=0;s==0 && i<page.count;i++)if(entries[i].kind==SC32_PATH_DIRECTORY){
        s=f2_reserve((void **)&scan->children,&scan->capacity,scan->count+1,sizeof(F2TreeChild));if(s<0)break;
        F2TreeChild *child=scan->children+scan->count;
        if(!memchr(entries[i].name,0,sizeof(entries[i].name)) || strchr(entries[i].name,'/')){s=SC_UI_E_INVALID;break;}
        s=f2_join(f2.nodes[scan->node-1].path,entries[i].name,child->path);
        if(s==0){child->revision=entries[i].generation;scan->count++;}
    }
    if(s==0 && !(page.flags&SC32_DIRECTORY_FINISHED))return 0;
    ScU32 id=scan->node,*children=0;
    if(s==0 && scan->count){if(scan->count>UINT_MAX/4)s=SC_UI_E_RANGE;else {children=malloc(scan->count*4);if(!children)s=SC_UI_E_MEMORY;}}
    for(ScU32 i=0;s==0 && i<scan->count;i++)s=f2_tree_add(scan->children[i].path,id,i,scan->children[i].revision,children+i);
    F2TreeNode *node=f2.nodes+id-1;node->queued=0;
    if(s==0){free(node->children);node->children=children;node->count=scan->count;node->loaded=1;node->error=0;}
    else {free(children);node->error=(ScU32)(-s);node->loaded=2;}
    f2_tree_scan_clear();f2_tree_notify();return s;
}
static ScStatus f2_tree_data(void *context,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    (void)context;reply->revision=f2.tree_revision;
    if(request->operation==SC_UI_DATA_COUNT){reply->total=f2.node_count?1:0;return 0;}
    ScU32 id=request->item,parent=request->parent;
    if(request->operation==SC_UI_DATA_CHILD_COUNT){
        if(!parent || parent&0x80000000u){reply->total=0;return 0;}
        if(parent>f2.node_count)return SC_UI_E_STALE;
        F2TreeNode *node=f2.nodes+parent-1;reply->total=node->loaded==1?node->count:1;return 0;
    }
    if(request->operation==SC_UI_DATA_CHILD_AT){
        if(!parent){if(request->index)return SC_UI_END;id=1;}
        else {if(parent&0x80000000u || parent>f2.node_count)return SC_UI_END;
            F2TreeNode *node=f2.nodes+parent-1;
            if(node->loaded!=1){if(request->index)return SC_UI_END;id=0x80000000u|parent;
                if(!node->loaded){ScStatus s=f2_tree_enqueue(parent);if(s<0)return s;}}
            else {if(request->index>=node->count)return SC_UI_END;id=node->children[request->index];}}
    }
    ScU32 placeholder=id&0x80000000u;id&=0x7fffffffu;if(!id || id>f2.node_count)return SC_UI_E_STALE;
    F2TreeNode *node=f2.nodes+id-1;reply->item=placeholder?0x80000000u|id:id;
    reply->parent=placeholder?id:node->parent;reply->state=SC_UI_VISIBLE|(placeholder?SC_UI_BUSY:SC_UI_ENABLED);
    if(request->operation==SC_UI_DATA_INDEX_OF){reply->total=placeholder?0:node->sibling;return 0;}
    if(request->operation==SC_UI_DATA_CELL){const char *label=placeholder?(node->error?"无法读取；F5 重试":"展开后读取…"):id==1?"SandCore /":f2_leaf(node->path);
        reply->value.type=SC_VALUE_UTF8;reply->value.data=label;reply->value.bytes=(ScU32)strlen(label);}
    if(request->operation==SC_UI_DATA_ITEM && !placeholder && f2.visible){F2Entry row;memset(&row,0,sizeof(row));
        row.kind=SC32_PATH_DIRECTORY;strcpy(row.path,node->path);reply->icon=f2_icon(&row);}
    return 0;
}
static ScStatus f2_tree_invalidate(void)
{
    /* 文件事务可能改变目录epoch；只清已缓存节点，下一次展开再取批。
     * 老子节点的存储仍由树拥有，稳定ID不因一次刷新而复用给另一名字。 */
    f2_tree_scan_clear();f2.tree_count=f2.tree_cursor=0;
    for(ScU32 i=0;i<f2.node_count;i++){f2.nodes[i].loaded=0;f2.nodes[i].queued=0;f2.nodes[i].error=0;}
    return f2_tree_notify();
}
