static ScStatus mon_notify(ScU32 kind)
{
    ScUiDataChange change;
    memset(&change,0,sizeof(change));
    change.size=sizeof(change);
    change.version=1;
    change.kind=kind;
    change.revision=++mon.revision;
    return ScUiProviderNotify(mon.provider,&change);
}
static void mon_metric(MonitorMetric *items,ScU32 *count,const char *label,ScU32 value,const char *unit,ScU32 valid)
{
    MonitorMetric *item=items+(*count)++;
    snprintf(item->label,sizeof(item->label),"%s",label);
    if(valid)snprintf(item->value,sizeof(item->value),"%u%s",value,unit);
    else strcpy(item->value,"不可用；未以零替代");
}
static void mon_summary(void)
{
    mon.system_count=mon.volume_count=0;
    mon_metric(mon.system_metrics,&mon.system_count,"CPU忙（PIT状态样本）",mon.pending_busy," %",mon.cpu_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"CPU空闲",mon.pending_idle," %",mon.cpu_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"内核（已含Gfx）",mon.pending_kernel," %",mon.cpu_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"用户态",mon.pending_user," %",mon.cpu_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"Gfx（内核子集，不能重复相加）",mon.pending_graphics," %",mon.cpu_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"PIT采样频率；不是FPS",mon.cpu.hz," Hz",mon.cpu.size!=0);
    mon_metric(mon.system_metrics,&mon.system_count,"系统受管内存",mon.memory.managed_bytes," B",mon.memory_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"系统已用内存",mon.memory.used_bytes," B",mon.memory_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"系统空闲内存",mon.memory.free_bytes," B",mon.memory_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"物理页大小",mon.memory.page_size," B",mon.memory_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"本Monitor已映射堆页",mon.memory.process_heap_pages," 页",mon.memory_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"本Monitor动态模块页",mon.memory.process_module_pages," 页",mon.memory_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"本Monitor总用户映射页",mon.memory.process_total_pages," 页",mon.memory_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"本Monitor页表页",mon.memory.page_table_pages," 页",mon.memory_valid);
    mon_metric(mon.system_metrics,&mon.system_count,"累计CPU样本（32位回绕）",mon.cpu.total,"",mon.cpu.size!=0);
    mon_metric(mon.system_metrics,&mon.system_count,"真实历史采样点",mon.history_count," / 120",1);
    mon_metric(mon.volume_metrics,&mon.volume_count,"挂载状态",mon.storage.mounted,"",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"SandFS格式版本",mon.storage.format,"",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"设备容量",mon.storage.disk_sectors," 扇区",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"数据区域起点",mon.storage.data_start," 扇区",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"数据区域终点",mon.storage.data_end," 扇区",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"目录容量",mon.storage.directory_capacity," 项",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"记录数量",mon.storage.record_count," 项",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"活跃正文占用",mon.storage.live_sectors," 扇区",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"保留扇区",mon.storage.reserved_sectors," 扇区",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"空闲扇区",mon.storage.free_sectors," 扇区",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"最大连续空闲段",mon.storage.largest_free_run," 扇区",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"对象序列",mon.storage.object_sequence,"",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"活动元数据bank",mon.storage.active_bank,"",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"元数据代数",mon.storage.metadata_generation,"",mon.storage_valid);
    mon_metric(mon.volume_metrics,&mon.volume_count,"存储故障状态",mon.storage.faulted,"",mon.storage_valid);
    char summary[256],memory_text[96];
    if(mon.memory_valid)snprintf(memory_text,sizeof(memory_text),"内存 %u / %u B",mon.memory.used_bytes,mon.memory.managed_bytes);
    else strcpy(memory_text,"内存不可用");
    if(mon.cpu_valid)snprintf(summary,sizeof(summary),"CPU忙 %u%% · 内核 %u%%（含Gfx %u%%）· 用户 %u%% · %s",
    mon.pending_busy,mon.pending_kernel,mon.pending_graphics,mon.pending_user,memory_text);
    else snprintf(summary,sizeof(summary),"等待CPU差分基点 · 内存%s · 本次进程快照%s",mon.memory_valid?"已读取":"不可用",mon.task_valid?"已读取":"不可用，保留旧数据");
    ScUiText text=tool_text(summary);
    ScUiNodeSetText(tool.ui,mon.summary,&text);
    snprintf(mon.status,sizeof(mon.status),"%s · %u个进程 · %u Hz PIT采样 · %s · 各页与全局数据不是原子快照",
    mon.frozen?"冻结":tool.visible?"约1秒更新":"隐藏：约5秒后台采样",mon.current.count,mon.cpu.hz,mon.task_valid?"进程数据有效":"旧进程快照");
    tool_status(mon.status);
    mon_notify(SC_UI_DATA_UPDATE);
}
static ScStatus mon_data(void *context,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    (void)context;
    reply->revision=mon.revision;
    if(mon.mode){ScU32 count=mon.mode==1?mon.system_count:mon.volume_count;
        MonitorMetric *items=mon.mode==1?mon.system_metrics:mon.volume_metrics;
        if(request->operation==SC_UI_DATA_COUNT || request->operation==SC_UI_DATA_CHILD_COUNT){reply->total=count;
            return 0;
        }
        ScU32 index=request->operation==SC_UI_DATA_CHILD_AT?request->index:request->item-0x80000000u;
        if(index>=count)return SC_UI_END;
        reply->item=0x80000000u+index;
        reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
        if(request->operation==SC_UI_DATA_INDEX_OF)reply->total=index;
        if(request->operation==SC_UI_DATA_CELL){const char *z=request->field?items[index].value:items[index].label;
            reply->value.type=SC_VALUE_UTF8;
            reply->value.data=z;
            reply->value.bytes=(ScU32)strlen(z);
        }return 0;
    }
    if(request->operation==SC_UI_DATA_COUNT || request->operation==SC_UI_DATA_CHILD_COUNT){reply->total=mon.current.visible;
        return 0;
    }
    if(request->operation==SC_UI_DATA_SORT){if(mon.scan || mon.order.active)return SC_UI_E_BUSY;
        ScStatus s=mon_index_begin(&mon.current,0,request->field,request->flags&1);
        return s<0?s:SC_UI_PENDING;
    }
    ScU32 index;
    if(request->operation==SC_UI_DATA_CHILD_AT){if(request->index>=mon.current.visible)return SC_UI_END;
        index=mon.current.order[request->index];
    }
    else if(mon_lookup(mon.current.items,&request->item,4,&index)<0)return SC_UI_E_STALE;
    MonitorRow *row=mon.current.rows+index;
    reply->item=row->item;
    reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(request->operation==SC_UI_DATA_INDEX_OF){if(mon.current.rank[index]==UINT_MAX)return SC_UI_E_NOT_FOUND;
        reply->total=mon.current.rank[index];
        return 0;
    }
    if(request->operation==SC_UI_DATA_CELL){ScU32 field=request->field;
        if(field==1 || field==2 || (field==7 && (row->process.flags&SC32_PROCESS_DETAILS))){reply->value.type=SC_VALUE_U32;
            reply->value.lo=field==1?row->process.pid:field==2?row->process.generation:row->process.dispatches;
        }
        else {const char *z=field==0?row->name:field==3?row->state:field==4?row->cpu:field==5?row->owner:field==6?row->session:field==8?row->waiting:"--";
            reply->value.type=SC_VALUE_UTF8;
            reply->value.data=z;
            reply->value.bytes=(ScU32)strlen(z);
    }}
    return 0;
}
static ScStatus mon_chart(ScUiContext *ui,ScUiNode node,ScUiPainter *painter,void *context)
{
    (void)ui;
    (void)context;
    ScU32 memory=node==mon.memory_chart;
    ScUiRect bounds;
    ScStatus s=ScUiPainterBounds(painter,&bounds);
    if(s<0)return s;
    ScUiPainterFill(painter,&bounds,SC32_COLOR_PAPER);
    ScUiRect title={10,4,bounds.width-20,24};
    ScUiText label=tool_text(memory?"内存使用比 · 真实受管内存":"CPU忙 · 真实PIT状态样本");
    ScUiPainterText(painter,&title,&label,SC_UI_FONT_DENSE,SC_UI_TEXT_ELLIPSIS);
    ScI32 left=10,top=30,width=bounds.width-20,height=bounds.height-40;
    if(width<2 || height<2)return 0;
    for(ScU32 i=0;i<=4;i++){ScI32 y=top+(ScI32)i*height/4;
        ScUiPainterLine(painter,left,y,left+width-1,y,SC32_COLOR_LINE);
    }
    for(ScU32 i=1;i<mon.history_count;i++){
        ScU32 a=(mon.cursor+120-mon.history_count+i-1)%120,b=(a+1)%120;
        MonitorPoint *x=mon.history+a,*y=mon.history+b;
        if(memory?(!x->memory_valid || !y->memory_valid):(!x->cpu_valid || !y->cpu_valid))continue;
        ScU32 v1=memory?tool_percent(x->used,x->total):x->busy,v2=memory?tool_percent(y->used,y->total):y->busy;
        ScUiPainterLine(painter,left+(ScI32)(i-1)*(width-1)/119,top+height-(ScI32)v1*height/100,
        left+(ScI32)i*(width-1)/119,top+height-(ScI32)v2*height/100,memory?SC32_COLOR_GOLD:SC32_COLOR_ACCENT);
    }
    return 0;
}
static ScStatus mon_timer(void)
{
    if(mon.frozen){if(mon.timer)ScUiTimerCancel(tool.ui,mon.timer);
        mon.timer=0;
        return 0;
    }
    ScMonotonicClock clock;
    memset(&clock,0,sizeof(clock));
    clock.size=sizeof(clock);
    clock.version=1;
    ScStatus s=ScSystemMonotonic(&clock);
    if(s<0)return s;
    ScUiTimerDesc timer;
    memset(&timer,0,sizeof(timer));
    timer.size=sizeof(timer);
    timer.version=1;
    timer.node=tool.root;
    timer.flags=SC_UI_TIMER_BACKGROUND;
    timer.period_ticks=(clock.hz?clock.hz:100)*(tool.visible?1u:5u);
    timer.deadline=clock.ticks;
    ScU32 old=timer.deadline.lo;
    timer.deadline.lo+=timer.period_ticks;
    if(timer.deadline.lo<old)timer.deadline.hi++;
    return mon.timer?ScUiTimerChange(tool.ui,mon.timer,&timer):ScUiTimerSet(tool.ui,&timer,&mon.timer);
}
static ScStatus mon_selected(ScProcessRef *ref)
{
    if(mon.mode)return SC_UI_E_INVALID;
    ScUiSelectionInfo selected;
    memset(&selected,0,sizeof(selected));
    selected.size=sizeof(selected);
    selected.version=1;
    ScStatus s=ScUiSelectionQuery(mon.selection,&selected);
    if(s<0)return s;
    ScU32 index;
    if(!selected.current || mon_lookup(mon.current.items,&selected.current,4,&index)<0)return SC_UI_E_NOT_FOUND;
    ScProcessInfo *process=&mon.current.rows[index].process;
    ref->size=sizeof(*ref);
    ref->version=1;
    ref->pid=process->pid;
    ref->generation=process->generation;
    return 0;
}
static ScStatus mon_details(void)
{
    ScProcessRef ref;
    ScStatus s=mon_selected(&ref);
    if(s<0)return s;
    if(!ref.pid)return tool_message("内核记录","PID 0 是公开枚举的内核采样行；不作为可终止或可查询的普通Ring3对象。");
    ScProcessInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScProcessQuery(&ref,&info);
    if(s<0)return s;
    char name[32],detail[700];
    mon_name(name,&info);
    snprintf(detail,sizeof(detail),"%s\nPID %u / 代数 %u\n状态 %u · UID:GID %d:%d · 会话 %u\nrealm %u · 明细flags 0x%X\n累计运行样本 %u · 调度次数 %u\n等待 %u / mask 0x%X / pending 0x%X\n这是当前公开查询；没有进程RSS或未发布内存字段。",
    name,info.pid,info.generation,info.state,info.uid,info.gid,info.session,info.realm,info.flags,info.running_samples,info.dispatches,info.waiting,info.wait_mask,info.pending_events);
    return tool_message("进程属性",detail);
}
static ScStatus mon_terminate_reply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;
    (void)context;
    if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;
    ScUiDialogInfo answer;
    memset(&answer,0,sizeof(answer));
    answer.size=sizeof(answer);
    answer.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&answer);
    if(s==0 && answer.stage==SC_UI_DIALOG_DONE && answer.command==SC_UI_DIALOG_OK){
        ScProcessInfo current;
        memset(&current,0,sizeof(current));
        current.size=sizeof(current);
        current.version=1;
        s=ScProcessQuery(&mon.target,&current);
        if(s==0)s=ScProcessTerminate(&mon.target);
        if(s==0){mon.sample_pending=1;
            tool_status("已发出对匹配PID与代数的终止请求");
    }}
    if(mon.confirm==dialog){mon.confirm=0;
        ScUiDialogRelease(dialog);
    }mon.confirm_active=0;
    if(s<0)tool_error(s,"终止进程");
    return 0;
}
static ScStatus mon_terminate(void)
{
    if(mon.confirm)return SC_UI_E_BUSY;
    ScStatus s=mon_selected(&mon.target);
    if(s<0)return s;
    if(!mon.target.pid || (mon.target.pid==mon.self.pid && mon.target.generation==mon.self.generation))return SC_UI_E_INVALID;
    ScProcessInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScProcessQuery(&mon.target,&info);
    if(s<0)return s;
    char text[240],name[32];
    mon_name(name,&info);
    snprintf(text,sizeof(text),"终止 %s（PID %u / 代数 %u）？\n可能丢失该程序未保存内容。内核会再次核对身份与权限。",name,info.pid,info.generation);
    ScUiText title=tool_text("终止进程"),message=tool_text(text);
    mon.confirm_active=1;
    return ScShellConfirm(tool.ui,tool_dialog_owner(),&title,&message,mon_terminate_reply,0,&mon.confirm);
}
static void mon_apply_layout(void)
{
    if(!mon.table || !tool.metrics.scale_percent)return;
    ScUiNodeInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    if(ScUiNodeQuery(tool.ui,tool.root,&info)<0)return;
    ScU32 compact=(ScU32)info.rectangle.height*100/tool.metrics.scale_percent<500;
    ScU32 stamp=mon.mode*2+compact;
    if(stamp==mon.layout_stamp)return;
    mon.layout_stamp=stamp;
    ScU32 history=mon.mode>=3;
    tool_visible(mon.table,!history);tool_visible(mon.filter_bar,!history);
    tool_visible(mon.chart_bar,history || !compact);
    tool_visible(mon.cpu_chart,mon.mode!=4);tool_visible(mon.memory_chart,mon.mode!=3);
    tool_layout(mon.chart_bar,SC_UI_HBOX,SC_UI_AUTO,history?SC_UI_AUTO:155,history?1:0);
}
static ScStatus mon_command(ScU32 command)
{
    if(command==TOOLS_EXIT){tool.quit=1;
        return ScUiContextStop(tool.ui,0);
    }
    if(command==TOOLS_REFRESH){mon.cpu_baseline=mon.task_baseline=0;
        mon.sample_pending=1;
        mon.refresh_once=1;
        return 0;
    }
    if(command==MON_FREEZE){mon.frozen=!mon.frozen;
        if(mon.frozen){mon.scan=0;
            memset(&mon.order,0,sizeof(mon.order));
            mon_notify(SC_UI_DATA_SORTED);
        }
        else {mon.cpu_baseline=mon.task_baseline=0;
            mon.sample_pending=1;
        }
        ScUiText caption=tool_text(mon.frozen?"继续":"冻结");ScUiNodeSetText(tool.ui,mon.freeze_button,&caption);
        mon_summary();
        return mon_timer();
    }
    if(command>=MON_PROCESSES && command<=MON_MEMORY_HISTORY){mon.mode=command-MON_PROCESSES;
        ScUiSelectionClear(mon.selection);
        ScStatus s=mon_columns();
        if(s==0)s=mon_notify(SC_UI_DATA_RESET);
        mon_apply_layout();
        return s;
    }
    if(command==MON_DETAILS)return mon_details();
    if(command==MON_TERMINATE)return mon_terminate();
    if(command==MON_RESET_HISTORY){memset(mon.history,0,sizeof(mon.history));
        mon.history_count=mon.cursor=0;
        if(tool.visible){ScUiInvalidate(tool.ui,mon.cpu_chart,0);
            ScUiInvalidate(tool.ui,mon.memory_chart,0);
        }return 0;
    }
    if(command==TOOLS_COPY){if(mon.mode){ScUiSelectionInfo selected;
            memset(&selected,0,sizeof(selected));
            selected.size=sizeof(selected);
            selected.version=1;
            ScStatus s=ScUiSelectionQuery(mon.selection,&selected);
            ScU32 index=selected.current-0x80000000u,count=mon.mode==1?mon.system_count:mon.volume_count;
            if(s<0 || index>=count)return s<0?s:SC_UI_E_INVALID;
            MonitorMetric *metric=(mon.mode==1?mon.system_metrics:mon.volume_metrics)+index;
            char copy[240];
            snprintf(copy,sizeof(copy),"%s: %s",metric->label,metric->value);
            return tool_copy(copy);
        }
        ScProcessRef ref;
        ScStatus s=mon_selected(&ref);
        if(s<0)return s;
        char text[96];
        snprintf(text,sizeof(text),"PID %u / generation %u",ref.pid,ref.generation);
        return tool_copy(text);
    }
    if(command==TOOLS_ABOUT)return tool_message("Monitor 2","CPU是客体PIT状态采样，不是FPS。Gfx包含于内核。\n任务按PID+代数匹配，各分页与CPU/内存/卷不是原子快照。\n隐藏每5秒采样，停止图表生成；冻结停止采样，恢复重建基点。");
    return SC_UI_E_NOT_SUPPORTED;
}
static ScStatus mon_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;
    (void)context;
    if(event->type==SC_UI_EVENT_LAYOUT && event->node==tool.root)mon_apply_layout();
    if(event->type==SC_UI_EVENT_KEY_DOWN && event->node==mon.table && event->key==' ' && !event->modifiers){
        ScStatus status=mon_command(MON_FREEZE);if(status<0)tool_error(status,"冻结/继续");return SC_UI_HANDLED;}
    if(event->type==SC_UI_EVENT_TIMER && event->data && event->bytes==sizeof(ScUiTimerEvent)){
        const ScUiTimerEvent *timer=event->data;
        if(timer->token==mon.timer && !mon.frozen)mon.sample_pending=1;
    }
    if(event->type==SC_UI_EVENT_VISIBILITY){mon_timer();
        if(tool.visible){if(!mon.frozen)mon.sample_pending=1;
            ScUiInvalidate(tool.ui,mon.cpu_chart,0);
            ScUiInvalidate(tool.ui,mon.memory_chart,0);
    }}
    if(event->type==SC_UI_EVENT_VALUE_CHANGED && event->node==mon.filter){ScString *value=0;
        const char *z=0;
        ScStatus s=tool_string_node(mon.filter,&value,&z);
        if(s==0 && strlen(z)>=sizeof(mon.filter_text))s=SC_UI_E_RANGE;
        if(s==0){strcpy(mon.filter_text,z);
            mon.reorder_pending=1;
        }ScStringRelease(value);
        if(s<0)tool_error(s,"过滤");
    }
    if(event->type==SC_UI_EVENT_KEY_DOWN && event->node==mon.table && event->key=='\n'){ScStatus s=mon_details();
        if(s<0)tool_error(s,"进程属性");
        return SC_UI_HANDLED;
    }
    return 0;
}
static ScStatus mon_columns(void)
{
    ScUiColumnDesc columns[9];
    memset(columns,0,sizeof(columns));
    const char *titles[]={"名称","PID","代数","状态","CPU","UID:GID","会话","调度次数","等待"};
    ScU32 count=mon.mode?2:9;
    for(ScU32 i=0;i<count;i++){columns[i].size=sizeof(columns[i]);
        columns[i].version=1;
        columns[i].column=i+1;
        columns[i].field=i;
        columns[i].title=tool_text(mon.mode?(i?"真实值 / 单位":"字段"):titles[i]);
        columns[i].width=mon.mode?(i?340:360):i==0?220:i==8?190:100;
        columns[i].min_width=44;
        columns[i].max_width=1800;
        columns[i].flags=mon.mode?0:SC_UI_COLUMN_SORTABLE;
        columns[i].format=!mon.mode && (i==1 || i==2 || i==7)?SC_UI_FORMAT_U32:SC_UI_FORMAT_TEXT;
    }
    return ScUiTableSetColumns(tool.ui,mon.table,columns,count);
}
static ScStatus mon_create(void)
{
    memset(&mon,0,sizeof(mon));
    mon.next_item=mon.revision=1;
    mon.field=1;
    mon.layout_stamp=UINT_MAX;
    ScStatus s=tool_create("Monitor 2",1080,730,mon_command,mon_event);
    if(s<0)return s;
    mon.self.size=sizeof(mon.self);
    mon.self.version=1;
    s=ScProcessSelf(&mon.self);
    if(s==0)s=mon_snapshot_create(&mon.current);
    if(s==0)s=mon_snapshot_create(&mon.staging);
    ScUiNode label;
    s=s<0?s:tool_widget(tool.content,SC_UI_LABEL,"等待读取实际系统快照",0,SC_UI_AUTO,32,0,&mon.summary);
    if(s==0)s=tool_widget(tool.content,SC_UI_PANEL,"",0,SC_UI_AUTO,155,0,&mon.chart_bar);
    ScUiClassDesc custom;
    memset(&custom,0,sizeof(custom));
    custom.size=sizeof(custom);
    custom.version=1;
    custom.name="SandCore.Monitor.Chart";
    custom.paint=mon_chart;
    if(s==0)s=ScUiClassRegister(tool.ui,&custom,&mon.chart_class);
    if(s==0)s=tool_widget(mon.chart_bar,mon.chart_class,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&mon.cpu_chart);
    if(s==0)s=tool_widget(mon.chart_bar,mon.chart_class,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&mon.memory_chart);
    if(s==0)s=tool_widget(tool.content,SC_UI_PANEL,"",0,SC_UI_AUTO,32,0,&mon.filter_bar);
    if(s==0)s=tool_widget(mon.filter_bar,SC_UI_LABEL,"名称 / PID / UID过滤",0,180,SC_UI_AUTO,0,&label);
    if(s==0)s=tool_widget(mon.filter_bar,SC_UI_TEXTBOX,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&mon.filter);
    if(s==0)s=tool_widget(mon.filter_bar,SC_UI_BUTTON,"冻结",MON_FREEZE,80,SC_UI_AUTO,0,&mon.freeze_button);
    if(s==0)s=tool_widget(tool.content,SC_UI_TABLE,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&mon.table);
    ScUiProviderDesc provider;
    memset(&provider,0,sizeof(provider));
    provider.size=sizeof(provider);
    provider.version=1;
    provider.flags=SC_UI_PROVIDER_SORT|SC_UI_PROVIDER_ASYNC;
    provider.procedure=mon_data;
    if(s==0)s=ScUiProviderCreate(&provider,&mon.provider);
    if(s==0)s=ScUiSelectionCreate(mon.provider,SC_UI_SELECTION_ALLOW_EMPTY,&mon.selection);
    if(s==0)s=ScUiViewBind(tool.ui,mon.table,mon.provider,mon.selection);
    if(s==0)s=mon_columns();
    ToolMenuEntry view[]={{"进程",MON_PROCESSES,0,0},{"CPU / 内存",MON_SYSTEM,0,0},{"卷 / SandFS",MON_STORAGE,0,0},{"冻结 / 继续",MON_FREEZE,' ',SC32_MOD_CONTROL},
        {"刷新并重建基点",TOOLS_REFRESH,SC32_KEY_F1+4,0},{"CPU历史图",MON_CPU_HISTORY,0,0},{"内存历史图",MON_MEMORY_HISTORY,0,0},
        {"清除历史图",MON_RESET_HISTORY,0,0},{"关闭",TOOLS_EXIT,'w',SC32_MOD_CONTROL}};
    ToolMenuEntry process[]={{"属性",MON_DETAILS,'\n',0},{"复制所选身份/数值",TOOLS_COPY,'c',SC32_MOD_CONTROL},{"终止…",MON_TERMINATE,SC32_KEY_DELETE,0}};
    ToolMenuEntry help[]={{"采样含义与边界",TOOLS_ABOUT,SC32_KEY_F1,0}};
    if(s==0)s=tool_menu_part("查看",view,9);
    if(s==0)s=tool_menu_part("进程",process,3);
    if(s==0)s=tool_menu_part("帮助",help,1);
    ScUiMenu *toolbar=0;
    if(s==0)s=ScUiMenuCreate(&toolbar);
    for(ScU32 i=0;s==0 && i<5;i++)s=tool_menu_item(toolbar,view+i,0);
    if(s==0)s=ScUiToolbarBind(tool.ui,tool.toolbar,toolbar);
    ScUiMenuRelease(toolbar);
    if(s==0)s=tool_listen(mon.filter,SC_UI_TARGET_PHASE,mon_event,0);
    if(s==0)s=tool_listen(mon.table,SC_UI_TARGET_PHASE,mon_event,0);
    if(s==0)s=mon_timer();
    if(s==0)s=ScUiFocus(tool.ui,mon.table);
    mon.sample_pending=1;
    return s;
}
