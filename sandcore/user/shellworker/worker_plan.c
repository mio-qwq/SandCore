static void sw_key(const char *path,char key[68])
{
    ScU32 i=0;
    for(;path[i];i++){
        unsigned char c=(unsigned char)path[i];
        key[i]=(char)(c>='a' && c<='z'?c-32:c);
    }
    key[i]=0;
}
static SwItem *sw_find_item(const char *path)
{
    char key[68];
    sw_key(path,key);
    ScHashEntry row;
    sw_record(&row,sizeof(row));
    if(ScHashGet(sw.paths,key,(ScU32)strlen(key),&row))return 0;
    SwItem *item=0;
    if(row.value_bytes==sizeof(item))memcpy(&item,row.value,sizeof(item));
    return item;
}
static ScStatus sw_plan_add(SwRoot *root,const ScLeaseEntry *entry)
{
    if(entry->kind!=1 && entry->kind!=2)return SC32_E_INVALID;
    char source[68],target[68];
    ScStatus status;
    memset(source,0,68);
    memset(target,0,68);
    if(sw.operation==SW_MKDIR){source[0]=0;
        memcpy(target,root->destination,68);
    }
    else{
        status=sw_path(entry->path,source);
        if(status)return status;
        if(!sw_same(source,root->source) && !sw_child(source,root->source))return SC32_E_INVALID;
        if(!root->skip){
            SwItem *old=sw_find_item(source);
            if(old){
                if(sw.retry_item && old!=sw.retry_item && old->phase!=SW_DONE && !old->published){
                    old->covered=0;
                    old->kind=entry->kind;
                    old->source_revision=entry->revision;
                    old->source_mode=entry->rw;
                    old->bytes=entry->kind==1?entry->length:0;
                    old->final_mode=(sw.flags&SHW_PRESERVE_MODE)?entry->rw:sw.final_mode;
                    old->status=0;
                    old->skip_children=0;
                }
                return 0;
            }
        }
        if(sw_same(source,root->source))memcpy(target,root->destination,68);
        else if(sw.retry_item && sw_child(source,sw.retry_item->source) && sw.retry_item->destination[0]){
            const char *tail=source+strlen(sw.retry_item->source);
            while(*tail=='/')tail++;
            status=sw_join(sw.retry_item->destination,tail,target);
            if(status)return status;
        }
        else if(root->destination[0]){
            const char *tail=source+strlen(root->source);
            while(*tail=='/')tail++;
            status=sw_join(root->destination,tail,target);
            if(status)return status;
        }else target[0]=0;
    }
    if(sw.item_counter==0x3fffffffu)return SC32_E_RANGE;
    SwItem *item=(SwItem *)calloc(1,sizeof(*item));
    if(!item)return SC32_E_MEMORY;
    int is_root=!root->root_item;
    if(is_root)item->id=root->client_id;
    else{
        if(sw.next_item_id==0xffffffffu){free(item);
            return SC32_E_RANGE;
        }
        item->id=++sw.next_item_id;
    }
    item->client_id=root->client_id;
    item->root=root;
    item->kind=entry->kind;
    item->source_revision=entry->revision;
    item->source_mode=entry->rw;
    item->bytes=entry->kind==1?entry->length:0;
    item->phase=SW_SOURCE;
    item->final_mode=(sw.flags&SHW_PRESERVE_MODE)?entry->rw:sw.final_mode;
    memcpy(item->source,source,68);
    memcpy(item->destination,target,68);
    const char *depth_path=sw.operation==SW_MKDIR?target:source;
    for(const char *p=depth_path;*p;p++)if(*p=='/')item->depth++;
    char key[68];
    sw_key(sw.operation==SW_MKDIR?target:source,key);
    status=root->skip?0:ScHashSet(sw.paths,key,(ScU32)strlen(key),&item,SC_HASH_INSERT_ONLY);
    if(status){free(item);
        return status;
    }
    status=ScArrayInsert(sw.order,sw.item_counter,&item,1);
    if(status){if(!root->skip)ScHashRemove(sw.paths,key,(ScU32)strlen(key));
        free(item);
        return status;
    }
    if(sw.last_item)sw.last_item->next=item;
    else sw.items=item;
    sw.last_item=item;
    sw.item_counter++;
    root->remaining++;
    if(is_root)root->root_item=item;
    if(item->kind==1)sw_counter_add(&sw.total_bytes,item->bytes);
    return 0;
}
static void sw_entry_from_meta(ScLeaseEntry *row,const char *path,const ScPathInfo *info)
{
    sw_record(row,sizeof(*row));
    row->kind=info->kind;
    row->length=info->bytes;
    row->revision=info->generation;
    row->uid=info->uid;
    row->gid=info->gid;
    row->rw=info->rw;
    memcpy(row->path,path,68);
}
static ScStatus sw_plan_problem(SwRoot *root,ScStatus status)
{
    root->plan_failed=1;
    root->planned=1;
    sw.plan_incomplete=1;
    if(root->root_item)sw_error(root->root_item,status);
    return 0;
}
static ScStatus sw_plan_root(SwRoot *root)
{
    if(root->planned)return 0;
    if(!root->root_item){
        ScLeaseEntry row;
        sw_record(&row,sizeof(row));
        row.kind=root->kind;
        if(root->skip){
            row.kind=root->kind;
            row.revision=root->expected_revision;
            memcpy(row.path,root->source,68);
            ScStatus status=sw_plan_add(root,&row);
            if(status)return status;
            root->planned=1;
            return 0;
        }
        if(sw.operation==SW_MKDIR){row.kind=2;
            row.rw=sw.final_mode;
        }
        else{
            ScPathInfo info;
            sw_record(&info,sizeof(info));
            ScStatus status=ScPathQuery(root->source,&info);
            if(status){ScStatus failed=status;
                row.kind=root->kind;
                row.revision=root->expected_revision;
                memcpy(row.path,root->source,68);
                status=sw_plan_add(root,&row);
                if(status)return status;
                return sw_plan_problem(root,failed);
            }
            sw_entry_from_meta(&row,root->source,&info);
            if(info.kind!=root->kind || (root->expected_revision && root->expected_revision!=info.generation)){
                ScStatus added=sw_plan_add(root,&row);
                if(added)return added;
                return sw_plan_problem(root,SC32_E_STALE);
            }
        }
        ScStatus status=sw_plan_add(root,&row);
        if(status)return status;
        if(root->skip){root->planned=1;
            return 0;
        }
        if(sw.operation==SW_MKDIR || root->kind==1){root->planned=1;
            return 0;
        }
        if(sw.operation==SW_RENAME || ((sw.operation==SW_DELETE || sw.operation==SW_MOVE) && !(sw.flags&SHW_RECURSIVE))){
            status=sw_lease_metadata(&root->scope,root->source,2,row.revision,0);
            if(status)return sw_plan_problem(root,status);
            root->planned=1;
            if(sw.operation==SW_MOVE)sw.plan_incomplete=1;
            return 0;
        }
        if(!(sw.flags&SHW_RECURSIVE))return sw_plan_problem(root,SC32_E_INVALID);
        status=sw_namespace_open(&root->scope,root->source,row.revision,sw.operation==SW_COPY,0);
        if(status && sw.operation!=SW_COPY){
            /* 没有自身R也可能有parentRW，整体rename/空目录remove仍
             * 是旧合法命名操作。META pin不伪装已枚举子树。 */
            status=sw_lease_metadata(&root->scope,root->source,2,row.revision,0);
            if(!status){root->planned=1;
                sw.plan_incomplete=1;
                return 0;
            }
        }
        if(status)return sw_plan_problem(root,status);
        root->member_count=root->scope.info.member_count;
    }
    ScLeaseControl control;
    ScLeasePage page;
    sw_control(&root->scope,&control);
    control.operation_sequence=0;
    sw_record(&page,sizeof(page));
    ScStatus status=ScFileLeaseSnapshot(&control,root->scan_cursor,sw.rows,SW_PLAN_QUANTUM,&page);
    if(status)return sw_plan_problem(root,status);
    if(page.cursor<root->scan_cursor || page.count>SW_PLAN_QUANTUM
       || page.cursor-root->scan_cursor!=page.count)return SC32_E_FAILED;
    for(ScU32 i=0;i<page.count;i++){
        status=sw_plan_add(root,sw.rows+i);
        if(status)return status;
        if(sw.rows[i].kind==2 && !sw_can_read(sw.rows[i].uid,sw.rows[i].gid,sw.rows[i].rw)){
            SwItem *item=sw_find_item(sw.rows[i].path);
            if(item){sw_error(item,SC32_E_ACCESS);
                item->skip_children=1;
                sw.plan_incomplete=1;
            }
        }
    }
    root->scan_cursor=page.cursor;
    if(page.flags&SC32_LEASE_PAGE_FINISHED){root->planned=1;
        return 0;
    }
    if(!page.count)return SC32_E_FAILED;
    return SC32_PENDING;
}
static int sw_path_compare(const char *a,const char *b)
{
    char first[68],second[68];
    sw_key(a,first);
    sw_key(b,second);
    return strcmp(first,second);
}
static int sw_item_compare(const void *a,const void *b)
{
    const SwItem *first=*(SwItem *const *)a,*second=*(SwItem *const *)b;
    if(first->root->id!=second->root->id)return first->root->id<second->root->id?-1:1;
    if(first->depth!=second->depth)return first->depth<second->depth?-1:1;
    return sw_path_compare(first->source[0]?first->source:first->destination,
                           second->source[0]?second->source:second->destination);
}
static int sw_root_compare(const void *a,const void *b)
{
    const SwRoot *first=*(SwRoot *const *)a,*second=*(SwRoot *const *)b;
    return sw_path_compare(first->source[0]?first->source:first->destination,
                           second->source[0]?second->source:second->destination);
}
static ScStatus sw_plan_finish(void)
{
    if(sw.item_counter>0xffffffffu/(ScU32)sizeof(SwItem *))return SC32_E_RANGE;
    SwItem **sorted=(SwItem **)malloc(sw.item_counter*(ScU32)sizeof(SwItem *));
    if(sw.item_counter && !sorted)return SC32_E_MEMORY;
    for(ScU32 i=0;i<sw.item_counter;i++){
        ScStatus status=ScArrayGet(sw.order,i,sorted+i);
        if(status){free(sorted);
            return status;
        }
    }
    free(sw.sorted);
    sw.sorted=sorted;
    /* 只排序本人新vector，不改SCLIB借用View；item本身地址不移动，
     * parent/当前冲突指针不会因qsort或容器扩容悬空。 */
    qsort(sw.sorted,sw.item_counter,sizeof(SwItem *),sw_item_compare);
    sw.total_bytes.lo=sw.total_bytes.hi=0;
    for(ScU32 i=0;i<sw.item_counter;i++){
        SwItem *item=sw.sorted[i];
        item->parent=0;
        item->children_total=item->children_finished=item->children_failed=0;
        if((!item->covered || item->outcome==SW_OUT_MOVED) && !item->root->skip && item->kind==1)sw_counter_add(&sw.total_bytes,item->bytes);
    }
    for(ScU32 i=0;i<sw.item_counter;i++){
        SwItem *item=sw.sorted[i];
        SwRoot *root=item->root;
        if(i==0 || sw.sorted[i-1]->root!=root)root->first=i;
        root->last=i+1;
        if(item->covered && item->outcome==SW_OUT_SKIPPED)continue;
        char parent[68];
        memcpy(parent,item->source,68);
        ScU32 n=(ScU32)strlen(parent);
        while(n && parent[n-1]!='/')n--;
        if(n>1)parent[n-1]=0;
        else parent[n]=0;
        if(n && !sw_same(parent,item->source))item->parent=sw_find_item(parent);
        if(item->parent && item->parent->root!=root)item->parent=0;
        if(item->parent)item->parent->children_total++;
    }
    for(ScU32 i=0;i<sw.item_counter;i++){
        SwItem *item=sw.sorted[i];
        if(item->parent && item->phase==SW_DONE){
            item->parent->children_finished++;
            if(item->outcome!=SW_OUT_COPIED && item->outcome!=SW_OUT_MOVED && item->outcome!=SW_OUT_REMOVED
               && item->outcome!=SW_OUT_RENAMED && item->outcome!=SW_OUT_CREATED)item->parent->children_failed++;
        }
    }
    sw.planning_complete=1;
    sw.running_root=sw.roots;
    if(sw.running_root)sw.active_index=sw.running_root->first;
    return 0;
}
