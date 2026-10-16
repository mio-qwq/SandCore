static ScStatus mon_hash(ScHash **out)
{
    ScHashDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.value_bytes=4;
    return ScHashCreate(&d,out);
}
static ScStatus mon_lookup(ScHash *hash,const void *key,ScU32 bytes,ScU32 *index)
{
    ScHashEntry entry;
    memset(&entry,0,sizeof(entry));
    entry.size=sizeof(entry);
    entry.version=1;
    ScStatus s=ScHashGet(hash,key,bytes,&entry);
    if(s==0)memcpy(index,entry.value,4);
    return s;
}
static ScStatus mon_snapshot_create(MonitorSnapshot *snapshot)
{ScStatus s=mon_hash(&snapshot->identities);
    if(s==0)s=mon_hash(&snapshot->items);
    return s;
}
static void mon_snapshot_free(MonitorSnapshot *snapshot)
{ScHashRelease(snapshot->identities);
    ScHashRelease(snapshot->items);
    free(snapshot->rows);
    free(snapshot->indices);
    memset(snapshot,0,sizeof(*snapshot));
}
static void mon_name(char out[32],const ScProcessInfo *info)
{
    if(!(info->flags&SC32_PROCESS_DETAILS)){strcpy(out,"（无权查看）");
        return;
    }
    ScU32 bytes=0;
    while(bytes<sizeof(info->name) && info->name[bytes])bytes++;
    ScU32 at=0,end=0,scalar=0;
    while(at<bytes){ScStatus s=ScUtf8Decode(info->name,bytes,&at,&scalar);
        if(s<0 || scalar<32 || scalar==127)break;
        end=at;
    }
    memcpy(out,info->name,end);
    out[end]=0;
    if(!end)strcpy(out,info->pid?"（未命名）":"内核");
}
static ScStatus mon_add(const ScProcessInfo *process,ScU32 total)
{
    MonitorSnapshot *candidate=&mon.staging;
    if(candidate->count==UINT_MAX)return SC_UI_E_RANGE;
    ScStatus s=tool_reserve((void **)&candidate->rows,&candidate->capacity,candidate->count+1,sizeof(MonitorRow));
    if(s<0)return s;
    ScU32 identity[2]={process->pid,process->generation},previous_index;
    MonitorRow row;
    memset(&row,0,sizeof(row));
    row.process=*process;
    row.total=total;
    if(mon_lookup(mon.current.identities,identity,8,&previous_index)==0){MonitorRow *old=mon.current.rows+previous_index;
        row.item=old->item;
        ScU32 elapsed=total-old->total;
        if(mon.task_baseline && elapsed && (old->process.flags&process->flags&SC32_PROCESS_DETAILS)){
            row.percent=tool_percent(process->running_samples-old->process.running_samples,elapsed);
            row.valid=1;
    }}
    if(!row.item){if(mon.next_item==UINT_MAX)return SC_UI_E_RANGE;
        row.item=mon.next_item++;
    }
    mon_name(row.name,process);
    const char *states[]={"空","运行","退出中","暂停"};
    strcpy(row.state,process->state<4?states[process->state]:"未知状态");
    if(row.valid)snprintf(row.cpu,sizeof(row.cpu),"%u%%",row.percent);
    else strcpy(row.cpu,"--");
    snprintf(row.owner,sizeof(row.owner),"%d:%d",process->uid,process->gid);
    if(process->flags&SC32_PROCESS_DETAILS){snprintf(row.session,sizeof(row.session),"%u",process->session);
        snprintf(row.waiting,sizeof(row.waiting),process->waiting?"等待 0x%X":"就绪",process->wait_mask);
    }else {strcpy(row.session,"--");
        strcpy(row.waiting,"--");
    }
    ScU32 index=candidate->count;
    s=ScHashSet(candidate->identities,identity,8,&index,SC_HASH_INSERT_ONLY);
    if(s==0)s=ScHashSet(candidate->items,&row.item,4,&index,SC_HASH_INSERT_ONLY);
    if(s==0)candidate->rows[candidate->count++]=row;
    return s;
}
static ScU32 mon_match(const char *text,const char *needle)
{
    if(!*needle)return 1;
    for(;*text;text++){ScU32 i=0;
        while(needle[i] && text[i]){unsigned char a=(unsigned char)text[i],b=(unsigned char)needle[i];
            if(a>='a' && a<='z')a-=32;
            if(b>='a' && b<='z')b-=32;
            if(a!=b)break;
            i++;
        }
        if(!needle[i])return 1;
    }return 0;
}
static int mon_compare(MonitorRow *a,MonitorRow *b,ScU32 field,ScU32 descending)
{
    int s=0;
    if(field==0)s=strcmp(a->name,b->name);
    else if(field==1)s=a->process.pid<b->process.pid?-1:a->process.pid>b->process.pid?1:0;
    else if(field==2)s=a->process.generation<b->process.generation?-1:a->process.generation>b->process.generation?1:0;
    else if(field==3)s=a->process.state<b->process.state?-1:a->process.state>b->process.state?1:0;
    else if(field==4){if(a->valid!=b->valid)return a->valid?-1:1;
        s=a->percent<b->percent?-1:a->percent>b->percent?1:0;
    }
    else if(field==5)s=strcmp(a->owner,b->owner);
    else if(field==6)s=a->process.session<b->process.session?-1:a->process.session>b->process.session?1:0;
    else if(field==7)s=a->process.dispatches<b->process.dispatches?-1:a->process.dispatches>b->process.dispatches?1:0;
    else s=a->process.waiting<b->process.waiting?-1:a->process.waiting>b->process.waiting?1:0;
    if(!s)s=a->item<b->item?-1:a->item>b->item?1:0;
    return descending?-s:s;
}
static void mon_sample_finish(ScU32 tasks_valid)
{
    mon.task_valid=tasks_valid;
    mon.task_baseline=tasks_valid;
    mon.scan=0;
    if(!mon.frozen){MonitorPoint *point=mon.history+mon.cursor;
        memset(point,0,sizeof(*point));
        point->tick=mon.cpu.tick;
        point->busy=mon.pending_busy;
        point->cpu_valid=mon.cpu_valid;
        point->memory_valid=mon.memory_valid;
        point->used=mon.memory.used_bytes;
        point->total=mon.memory.managed_bytes;
        mon.cursor=(mon.cursor+1)%120;
        if(mon.history_count<120)mon.history_count++;}
    mon.samples++;
    if(mon.cpu.size && mon.cpu.version==1){mon.before=mon.cpu;
        mon.cpu_baseline=1;
    }else mon.cpu_baseline=0;
    mon_summary();
    if(tool.visible){ScUiInvalidate(tool.ui,mon.cpu_chart,0);
        ScUiInvalidate(tool.ui,mon.memory_chart,0);
    }
}
static ScStatus mon_index_begin(MonitorSnapshot *snapshot,ScU32 publish,ScU32 field,ScU32 descending)
{
    if(mon.order.active)return SC_UI_E_BUSY;
    ScStatus s=tool_reserve((void **)&mon.work_indices,&mon.work_capacity,snapshot->count?snapshot->count:1,12);
    if(s<0)return s;
    memset(&mon.order,0,sizeof(mon.order));
    mon.order.active=1;
    mon.order.publish=publish;
    mon.order.snapshot=snapshot;
    mon.order.field=field;
    mon.order.descending=descending;
    mon.order.width=1;
    mon.order.order=mon.work_indices;
    mon.order.scratch=mon.work_indices+mon.work_capacity;
    mon.order.rank=mon.work_indices+mon.work_capacity*2;
    return 0;
}
static ScStatus mon_index_step(void)
{
    MonitorOrder *job=&mon.order;
    if(!job->active)return 0;
    MonitorSnapshot *snapshot=job->snapshot;
    ScU32 budget=1024;
    if(job->phase==0){while(job->at<snapshot->count && budget--){ScU32 i=job->at++;
            job->rank[i]=UINT_MAX;
            MonitorRow *row=snapshot->rows+i;
            char pid[16];
            snprintf(pid,sizeof(pid),"%u",row->process.pid);
            if(mon_match(row->name,mon.filter_text) || mon_match(pid,mon.filter_text) || mon_match(row->owner,mon.filter_text))job->order[job->count++]=i;
        }
        if(job->at==snapshot->count){job->phase=1;
            job->at=0;
        }return 0;
    }
    if(job->phase==1){while(budget-- && job->width<job->count){
            if(job->left>=job->count){ScU32 *swap=job->order;
                job->order=job->scratch;
                job->scratch=swap;
                job->left=0;
                job->merging=0;
                job->width=job->width>job->count/2?job->count:job->width*2;
                continue;
            }
            if(!job->merging){job->a=job->left;
                job->middle=job->left+job->width;
                if(job->middle>job->count)job->middle=job->count;
                job->b=job->middle;
                job->last=job->middle+job->width;
                if(job->last<job->middle || job->last>job->count)job->last=job->count;
                job->out=job->left;
                job->merging=1;
            }
            if(job->out==job->last){job->left=job->last;
                job->merging=0;
                continue;
            }
            if(job->a<job->middle && (job->b>=job->last || mon_compare(snapshot->rows+job->order[job->a],snapshot->rows+job->order[job->b],job->field,job->descending)<=0))
            job->scratch[job->out++]=job->order[job->a++];
            else job->scratch[job->out++]=job->order[job->b++];
        }
        if(job->width>=job->count){job->phase=2;
            job->at=0;
        }return 0;
    }
    while(job->at<job->count && budget--){ScU32 i=job->at++;
        job->rank[job->order[i]]=i;
    }
    if(job->at<job->count)return 0;
    ScUiSelectionInfo selected;
    memset(&selected,0,sizeof(selected));
    selected.size=sizeof(selected);
    selected.version=1;
    ScUiSelectionQuery(mon.selection,&selected);
    ScU32 *old=snapshot->indices,old_capacity=snapshot->index_capacity;
    snapshot->indices=mon.work_indices;
    snapshot->index_capacity=mon.work_capacity;
    snapshot->order=job->order;
    snapshot->rank=job->rank;
    snapshot->visible=job->count;
    mon.work_indices=old;
    mon.work_capacity=old_capacity;
    mon.field=job->field;
    mon.descending=job->descending;
    ScU32 publish=job->publish;
    memset(job,0,sizeof(*job));
    if(publish){MonitorSnapshot swap=mon.current;
        mon.current=mon.staging;
        mon.staging=swap;
    }
    mon_notify(publish?SC_UI_DATA_RESET:SC_UI_DATA_SORTED);
    ScUiSelectionClear(mon.selection);
    ScU32 index;
    if(selected.current && mon_lookup(mon.current.items,&selected.current,4,&index)==0 && mon.current.rank[index]!=UINT_MAX)
    ScUiSelectionSet(mon.selection,selected.current,SC_UI_REPLACE);
    if(publish)mon_sample_finish(1);
    return 0;
}
static ScStatus mon_sample_begin(void)
{
    if((mon.frozen && !mon.refresh_once) || mon.scan || mon.order.active)return 0;
    mon.refresh_once=0;
    mon.sample_pending=0;
    mon.staging.count=0;
    ScHashClear(mon.staging.identities);
    ScHashClear(mon.staging.items);
    memset(&mon.page,0,sizeof(mon.page));
    mon.page.size=sizeof(mon.page);
    mon.page.version=1;
    mon.page.cursor=SC32_ENUM_BEGIN;
    memset(&mon.cpu,0,sizeof(mon.cpu));
    mon.cpu.size=sizeof(mon.cpu);
    mon.cpu.version=1;
    ScStatus cpu=ScSystemCpu(&mon.cpu);
    mon.cpu_valid=0;
    if(cpu==0 && mon.cpu_baseline){ScU32 elapsed=mon.cpu.total-mon.before.total;
        if(elapsed){mon.cpu_valid=1;
            mon.pending_idle=tool_percent(mon.cpu.idle-mon.before.idle,elapsed);
            mon.pending_busy=100-mon.pending_idle;
            mon.pending_kernel=tool_percent(mon.cpu.kernel-mon.before.kernel,elapsed);
            mon.pending_user=tool_percent(mon.cpu.user-mon.before.user,elapsed);
            mon.pending_graphics=tool_percent(mon.cpu.graphics-mon.before.graphics,elapsed);
    }}
    if(cpu<0){mon.cpu.size=0;
        mon.cpu_baseline=0;
    }
    ScMemoryInfo memory;
    memset(&memory,0,sizeof(memory));
    memory.size=sizeof(memory);
    memory.version=1;
    mon.memory_valid=ScSystemMemory(&memory)==0;
    if(mon.memory_valid)mon.memory=memory;
    ScStorageInfo storage;
    memset(&storage,0,sizeof(storage));
    storage.size=sizeof(storage);
    storage.version=1;
    mon.storage_valid=ScSystemStorage(&storage)==0;
    if(mon.storage_valid)mon.storage=storage;
    mon.scan=1;
    return 0;
}
static ScStatus mon_scan_step(void)
{
    if(mon.scan!=1)return 0;
    ScProcessInfo rows[16];
    ScU32 before=mon.page.cursor;
    ScStatus s=ScProcessEnumerate(&mon.page,rows,16);
    if(s==0 && mon.page.cursor!=SC32_ENUM_BEGIN && (mon.page.cursor==before || (before!=SC32_ENUM_BEGIN && mon.page.cursor<before)))s=SC_UI_E_STALE;
    for(ScU32 i=0;s==0 && i<mon.page.count;i++)if(rows[i].state!=SC32_PROCESS_EMPTY)s=mon_add(rows+i,mon.page.total_samples);
    if(s<0){mon_sample_finish(0);
        return s;
    }
    if(mon.page.cursor==SC32_ENUM_BEGIN){s=mon_index_begin(&mon.staging,1,mon.field,mon.descending);
        if(s<0){mon_sample_finish(0);
            return s;
        }mon.scan=2;
    }
    return 0;
}
