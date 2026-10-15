static ScStatus sw_retry_begin(SwItem *item)
{
    if(sw.retry_item || item->kind!=2 || item->published || item->children_finished
       || (item==item->root->root_item && item->kind!=item->root->kind))return SC32_E_STALE;
    if(sw.operation==SW_COPY && !(sw.flags&SHW_RECURSIVE))return SC32_E_INVALID;
    sw.retry_item=item;
    sw.retry_root=item==item->root->root_item;
    sw.retry_phase=0;
    sw.retry_cursor=sw.retry_snapshot_borrowed=0;
    sw.retry_error=0;
    sw.state=SHW_PLANNING;
    return 0;
}
static void sw_retry_problem(ScStatus status)
{
    sw.retry_error=status;
    sw.retry_phase=4;
}
static ScStatus sw_retry_step(void)
{
    SwItem *item=sw.retry_item;
    SwRoot *root=item->root;
    ScStatus status;
    if(sw.cancelled){sw.retry_error=SC32_E_CANCELLED;
        sw.retry_phase=4;
    }
    if(sw.retry_phase==0){
        status=sw_close_active();
        if(status==SC32_PENDING)return status;
        if(status){sw_retry_problem(status);
            return SC32_PENDING;
        }
        ScPathInfo info;
        sw_record(&info,sizeof(info));
        status=ScPathQuery(item->source,&info);
        if(status || info.kind!=2 || info.generation!=item->source_revision){sw_retry_problem(status?status:SC32_E_STALE);
            return SC32_PENDING;
        }
        if(!sw_can_read(info.uid,info.gid,info.rw)){sw_retry_problem(SC32_E_ACCESS);
            return SC32_PENDING;
        }
        item->source_mode=info.rw;
        if(sw.flags&SHW_PRESERVE_MODE)item->final_mode=info.rw;
        if(sw.retry_root && root->scope.valid && (root->scope.info.flags&SC32_LEASE_FLAG_SNAPSHOT)
           && (root->scope.info.state==SC32_LEASE_READY || root->scope.info.state==SC32_LEASE_COMMITTED)){
            sw.retry_snapshot_borrowed=1;
            sw.retry_phase=2;
        }else sw.retry_phase=1;
        /* 只退休尚未开始的旧观察，已提交项目与其ledger不回退。显式
         * Retry是新的规划轮次，不是后台poll偷偷扩大原快照成员。 */
        for(SwItem *old=sw.items;old;old=old->next)
            if(old!=item && old->root==root && old->phase!=SW_DONE && sw_child(old->source,item->source))old->covered=1;
        return SC32_PENDING;
    }
    if(sw.retry_phase==1){
        if(sw.retry_root){status=sw_lease_release(&root->scope);
            if(status==SC32_PENDING)return status;
            if(status){sw_retry_problem(status);
                return SC32_PENDING;
            }}
        SwLease *lease=sw.retry_root?&root->scope:&sw.retry_lease;
        status=sw_namespace_open(lease,item->source,item->source_revision,sw.operation==SW_COPY,
            sw.retry_root?0:sw_source_parent(item));
        if(status){sw_retry_problem(status);
            return SC32_PENDING;
        }
        sw.retry_phase=2;
        return SC32_PENDING;
    }
    if(sw.retry_phase==2){
        SwLease *lease=sw.retry_root?&root->scope:&sw.retry_lease;
        ScLeaseControl control;
        ScLeasePage page;
        sw_control(lease,&control);
        control.operation_sequence=0;
        sw_record(&page,sizeof(page));
        status=ScFileLeaseSnapshot(&control,sw.retry_cursor,sw.rows,SW_PLAN_QUANTUM,&page);
        if(status){sw_retry_problem(status);
            return SC32_PENDING;
        }
        if(page.count>SW_PLAN_QUANTUM || page.cursor<sw.retry_cursor || page.cursor-sw.retry_cursor!=page.count){sw_retry_problem(SC32_E_FAILED);
            return SC32_PENDING;
        }
        for(ScU32 i=0;i<page.count;i++){
            ScLeaseEntry *row=sw.rows+i;
            if(!sw_same(row->path,item->source) && !sw_child(row->path,item->source)){sw_retry_problem(SC32_E_INVALID);
                return SC32_PENDING;
            }
            status=sw_plan_add(root,row);
            if(status){sw_retry_problem(status);
                return SC32_PENDING;
            }
            SwItem *planned=sw_find_item(row->path);
            if(planned && planned!=item && planned->phase!=SW_DONE && !planned->published){
                const char *suffix=row->path+strlen(item->source);
                while(*suffix=='/')suffix++;
                if(item->destination[0]){status=sw_join(item->destination,suffix,planned->destination);
                    if(status){sw_retry_problem(status);
                        return SC32_PENDING;
                    }}
                if(row->kind==2 && !sw_can_read(row->uid,row->gid,row->rw)){sw_error(planned,SC32_E_ACCESS);
                    planned->skip_children=1;
                }
            }
        }
        sw.retry_cursor=page.cursor;
        if(page.flags&SC32_LEASE_PAGE_FINISHED)sw.retry_phase=3;
        else if(!page.count)sw_retry_problem(SC32_E_FAILED);
        return SC32_PENDING;
    }
    if(sw.retry_phase==3){
        status=sw_lease_release(&sw.retry_lease);
        if(status==SC32_PENDING)return status;
        if(status){sw_retry_problem(status);
            return SC32_PENDING;
        }
        /* 退休项目从未运行；它们只计skip，不伪造复制/删除的ITEM事实。 */
        for(SwItem *old=sw.items;old;old=old->next)if(old->covered && old->phase!=SW_DONE){old->phase=SW_DONE;
            old->outcome=SW_OUT_SKIPPED;
            old->reported=2;
            sw.items_done++;
            sw.items_skipped++;
        }
        item->status=0;
        sw_record(&item->cause,sizeof(item->cause));
        item->skip_children=0;
        root->plan_failed=0;
        root->planned=1;
        SwRoot *running=sw.running_root;
        ScU32 finalizing=sw.executing_deletes;
        status=sw_plan_finish();
        if(status){sw_retry_problem(status);
            return SC32_PENDING;
        }
        sw.running_root=running;
        sw.executing_deletes=finalizing;
        sw.active_index=root->first;
        while(sw.active_index<root->last && sw.sorted[sw.active_index]!=item)sw.active_index++;
        if(finalizing){sw.delete_index=root->last;
            item->finalizing=0;
            sw.executing_deletes=0;
        }
        sw.plan_incomplete=0;
        for(SwItem *old=sw.items;old;old=old->next)if(old->root->plan_failed || old->skip_children
            || (old->root->scope.info.flags&SC32_LEASE_FLAG_METADATA_ONLY))sw.plan_incomplete=1;
        item->phase=SW_SOURCE;
        sw.state=SHW_RUNNING;
        sw.retry_item=0;
        return sw_emit_progress(item);
    }
    status=sw_lease_release(&sw.retry_lease);
    if(status==SC32_PENDING)return status;
    ScStatus failure=sw.retry_error?sw.retry_error:status;
    if(!failure)failure=SC32_E_FAILED;
    /* 失败也重建索引：此前已申请的新item仍归拥有链，不能让旧
     * sorted容量/边界与item_counter分离而越界。它们尚未产生副作用。 */
    SwRoot *running=sw.running_root;
    status=sw_plan_finish();
    if(status)return status;
    sw.running_root=running;
    sw.active_index=root->first;
    while(sw.active_index<root->last && sw.sorted[sw.active_index]!=item)sw.active_index++;
    sw.retry_item=0;
    if(sw.cancelled)return sw_finish_item(item,SW_OUT_CANCELLED,SC32_E_CANCELLED);
    return sw_begin_conflict(item,failure);
}
