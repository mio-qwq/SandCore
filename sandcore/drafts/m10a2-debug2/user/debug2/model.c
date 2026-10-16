#include "D2APP.H"

ScStatus D2RowAppend(ScArray *array,D2Row *row)
{ScU32 count=D2Count(array);
    if(count==UINT_MAX)return SC32_E_RANGE;
    row->id=count+1;
    return D2Append(array,row);
}
void D2FrameDestroy(D2Frame *f)
{
    if(!f)return;
    if(f->sidecar_file)ScFileClose(f->sidecar_file);
    free(f->sidecar_bytes);
    for(ScU32 i=0;i<D2_PANES;i++)ScArrayRelease(f->rows[i]);
    ScArrayRelease(f->modules);
    ScArrayRelease(f->segments);
    ScArrayRelease(f->lines);
    free(f);
}
static ScStatus d2_memory(D2App *a,D2Frame *f,ScU32 address,ScU32 bytes,ScU32 flags,unsigned char *data,unsigned char *valid)
{
    ScDebugReadInfo info;
    D2Record(&info,sizeof(info));
    return ScDebugRead(&a->launch.target,f->snapshot.stop_generation,address,bytes,flags,data,valid,&info);
}
static ScStatus d2_primary(D2App *a,D2Frame *f)
{
    unsigned char data[1024],valid[1024];
    ScStatus s;
    ScU32 code_bytes=f->code_address>UINT_MAX-1023u?UINT_MAX-f->code_address+1u:1024u;
    s=d2_memory(a,f,f->code_address,code_bytes,SC32_DEBUG_READ_ORIGINAL_CODE,data,valid);
    if(s<0)return s;
    for(ScU32 at=0;at<code_bytes && D2Count(f->rows[D2_CODE])<96;){D2Instruction instruction;
        D2Decode(data+at,valid+at,code_bytes-at,f->code_address+at,&instruction);
        D2Row r;
        memset(&r,0,sizeof(r));
        r.address=instruction.address;
        r.aux=instruction.target;
        r.flags=instruction.branch;
        snprintf(r.text[0],sizeof(r.text[0]),r.address==f->snapshot.registers.eip?">":"");
        snprintf(r.text[1],sizeof(r.text[1]),"%08X",r.address);
        snprintf(r.text[2],sizeof(r.text[2]),"%s",instruction.bytes);
        snprintf(r.text[3],sizeof(r.text[3]),"%s",instruction.text);
        s=D2RowAppend(f->rows[D2_CODE],&r);
        if(s<0)return s;
        at+=instruction.length;
    }
    ScDebugRegisters *g=&f->snapshot.registers;
    const char *names[21]={"EAX","EBX","ECX","EDX","ESI","EDI","EBP","ESP","EIP","EFLAGS","CS","SS","DS","ES","FS","GS","VECTOR","ERROR","CR2","STOP","PID / epoch"};
    ScU32 values[21]={g->eax,g->ebx,g->ecx,g->edx,g->esi,g->edi,g->ebp,g->esp,g->eip,g->eflags,g->cs,g->ss,g->ds,g->es,g->fs,g->gs,g->vector,g->error,g->cr2,f->snapshot.stop_generation,a->launch.target.pid};
    for(ScU32 i=0;i<21;i++){D2Row r;
        memset(&r,0,sizeof(r));
        r.address=values[i];
        snprintf(r.text[0],sizeof(r.text[0]),"%s",names[i]);
        snprintf(r.text[1],sizeof(r.text[1]),"%08X",values[i]);
        if(i==9)snprintf(r.text[2],sizeof(r.text[2]),"CF%u PF%u AF%u ZF%u SF%u TF%u IF%u DF%u OF%u",g->eflags&1u,(g->eflags>>2)&1u,(g->eflags>>4)&1u,(g->eflags>>6)&1u,(g->eflags>>7)&1u,(g->eflags>>8)&1u,(g->eflags>>9)&1u,(g->eflags>>10)&1u,(g->eflags>>11)&1u);
        if(i==20)snprintf(r.text[2],sizeof(r.text[2]),"process %u / bind %u",a->launch.target.generation,a->launch.target.binding);
        s=D2RowAppend(f->rows[D2_REGISTERS],&r);
        if(s<0)return s;
    }
    ScU32 memory_bytes=f->memory_address>UINT_MAX-255u?UINT_MAX-f->memory_address+1u:256u;
    s=d2_memory(a,f,f->memory_address,memory_bytes,0,data,valid);
    if(s<0)return s;
    for(ScU32 at=0;at<memory_bytes;at+=16){D2Row r;
        memset(&r,0,sizeof(r));
        r.address=f->memory_address+at;
        snprintf(r.text[0],sizeof(r.text[0]),"%08X",r.address);
        ScU32 available=0,bytes=memory_bytes-at;
        if(bytes>16)bytes=16;
        for(ScU32 i=0;i<bytes;i++){ScU32 n=(ScU32)strlen(r.text[1]);
            snprintf(r.text[1]+n,sizeof(r.text[1])-n,valid[at+i]?"%02X ":"?? ",data[at+i]);
            r.text[2][i]=valid[at+i]?(data[at+i]>=32 && data[at+i]<127?(char)data[at+i]:'.'):'?';
            if(valid[at+i])available++;
        }
        snprintf(r.text[3],sizeof(r.text[3]),"%u/%u readable",available,bytes);
        s=D2RowAppend(f->rows[D2_MEMORY],&r);
        if(s<0)return s;
    }
    ScU32 stack_bytes=g->esp>UINT_MAX-255u?UINT_MAX-g->esp+1u:256u;
    s=d2_memory(a,f,g->esp,stack_bytes,0,data,valid);
    if(s<0)return s;
    for(ScU32 at=0;at+4<=stack_bytes;at+=4){D2Row r;
        memset(&r,0,sizeof(r));
        r.address=g->esp+at;
        snprintf(r.text[0],sizeof(r.text[0]),"%08X",r.address);
        int good=valid[at]&&valid[at+1]&&valid[at+2]&&valid[at+3];
        r.aux=(ScU32)data[at]|((ScU32)data[at+1]<<8)|((ScU32)data[at+2]<<16)|((ScU32)data[at+3]<<24);
        r.flags=(ScU32)good;
        if(good)snprintf(r.text[1],sizeof(r.text[1]),"%08X",r.aux);
        else snprintf(r.text[1],sizeof(r.text[1]),"????????");
        snprintf(r.text[2],sizeof(r.text[2]),r.address==g->esp?"ESP":r.address==g->ebp?"EBP":r.address==g->ebp+4 && g->ebp<=UINT_MAX-4?"saved return":good?"":"unreadable");
        s=D2RowAppend(f->rows[D2_STACK],&r);
        if(s<0)return s;
    }
    return 0;
}
ScStatus D2CaptureStart(D2App *a)
{
    if(!a->active)return SC32_E_STALE;
    D2Frame *f=calloc(1,sizeof(*f));
    if(!f)return SC32_E_MEMORY;
    D2Record(&f->snapshot,sizeof(f->snapshot));
    ScStatus s=ScDebugQuery(&a->launch.target,&f->snapshot);
    if(s<0 || f->snapshot.state!=SC32_DEBUG_PAUSED){D2FrameDestroy(f);
        return s<0?s:SC32_E_BUSY;
    }
    f->code_address=a->follow_ip?f->snapshot.registers.eip:a->code_address;
    f->memory_address=a->memory_address?a->memory_address:f->snapshot.registers.esp&~15u;
    for(ScU32 i=0;s==0 && i<D2_PANES;i++)s=D2Array(sizeof(D2Row),&f->rows[i]);
    if(s==0)s=D2Array(sizeof(ScDebugModule),&f->modules);
    if(s==0)s=D2Array(sizeof(ScDebugSegment),&f->segments);
    if(s==0)s=D2Array(sizeof(D2SourceLine),&f->lines);
    /* 模块代数不变时，immutable登记及SHA绑定sidecar属于同一装载
     * 寿命。单步只重读真实CPU/代码/内存/栈/断点，不反复读盘或
     * 重建几千个库符号；模块装卸/ready提交会改变内核module_epoch。 */
    if(s==0 && a->frame && a->frame->snapshot.module_epoch==f->snapshot.module_epoch){
        ScArray **dest[5]={&f->modules,&f->segments,&f->lines,&f->rows[D2_MODULES],&f->rows[D2_SYMBOLS]};
        ScArray *source[5]={a->frame->modules,a->frame->segments,a->frame->lines,a->frame->rows[D2_MODULES],a->frame->rows[D2_SYMBOLS]};
        for(ScU32 i=0;s==0 && i<5;i++){s=ScArrayRetain(source[i]);
            if(s==0){ScArrayRelease(*dest[i]);
                 *dest[i]=source[i];
        }}
        if(s==0){f->cached_modules=1;
            snprintf(f->sidecar_status,sizeof(f->sidecar_status),"%s",a->frame->sidecar_status);
        }
    }
    if(s<0){D2FrameDestroy(f);
        return s;
    }
    D2FrameDestroy(a->candidate);
    a->candidate=f;
    f->stage=1;
    a->current=f->snapshot;
    /* 新暂停一开始就撤旧现场可用性，界面不会把前一次暂停寄存器
     * 与本次内存/模块拼起来。候选全部收齐并复查后才一次发布。 */
    D2Clear(a);
    char text[128];
    snprintf(text,sizeof(text),"Reading pause %u - panes publish together",f->snapshot.stop_generation);
    D2Status(a,text);
    D2Schedule(a);
    return 0;
}
static ScStatus d2_capture_breaks(D2App *a,D2Frame *f)
{
    ScDebugBreakpoint rows[32];
    ScDebugPage p;
    D2Record(&p,sizeof(p));
    ScStatus s=ScDebugBreakpoints(&a->launch.target,f->snapshot.stop_generation,f->break_cursor,f->snapshot.breakpoint_epoch,rows,32,&p);
    if(s<0)return s;
    for(ScU32 i=0;i<p.count;i++){D2Row r;
        memset(&r,0,sizeof(r));
        r.address=rows[i].address;
        r.aux=rows[i].id;
        r.generation=rows[i].generation;
        r.flags=rows[i].flags;
        snprintf(r.text[0],sizeof(r.text[0]),"%08X",r.address);
        snprintf(r.text[1],sizeof(r.text[1]),rows[i].flags&SC32_DEBUG_BP_INVALID?"unmapped":rows[i].flags&SC32_DEBUG_BP_PENDING?"hit / rearm":"enabled");
        snprintf(r.text[2],sizeof(r.text[2]),"%u",rows[i].hits);
        snprintf(r.text[3],sizeof(r.text[3]),"%u:%u",rows[i].id,rows[i].generation);
        s=D2RowAppend(f->rows[D2_BREAKPOINTS],&r);
        if(s<0)return s;
    }
    f->break_cursor=p.cursor;
    if(p.flags&SC32_DEBUG_PAGE_FINISHED)f->stage=f->cached_modules?7:3;
    return 0;
}
static ScStatus d2_capture_modules(D2App *a,D2Frame *f)
{
    ScDebugModule rows[16];
    ScDebugPage p;
    D2Record(&p,sizeof(p));
    ScStatus s=ScDebugModules(&a->launch.target,f->snapshot.stop_generation,f->cursor,f->snapshot.module_epoch,rows,16,&p);
    if(s<0)return s;
    for(ScU32 i=0;i<p.count;i++){D2Row r;
        memset(&r,0,sizeof(r));
        r.address=rows[i].base;
        r.aux=rows[i].id;
        r.generation=rows[i].generation;
        snprintf(r.text[0],sizeof(r.text[0]),"%s",rows[i].name);
        snprintf(r.text[1],sizeof(r.text[1]),"%08X - %08X",rows[i].base,rows[i].base+rows[i].image_bytes-1);
        snprintf(r.text[2],sizeof(r.text[2]),rows[i].kind==SC32_DEBUG_MAIN?"SCX":"ABI %u.%u / v%u",rows[i].abi_major,rows[i].abi_minor,rows[i].library_version);
        snprintf(r.text[3],sizeof(r.text[3]),"%s",rows[i].source);
        snprintf(r.text[4],sizeof(r.text[4]),"%s / epoch %u",rows[i].flags&SC32_DEBUG_MODULE_INITIALIZING?"initializing":"ready",rows[i].generation);
        s=D2Append(f->modules,&rows[i]);
        if(s==0)s=D2RowAppend(f->rows[D2_MODULES],&r);
        if(s<0)return s;
    }
    f->cursor=p.cursor;
    if(p.flags&SC32_DEBUG_PAGE_FINISHED){f->stage=4;
        f->module_index=f->cursor=0;
    }return 0;
}
static ScStatus d2_capture_segments(D2App *a,D2Frame *f)
{
    ScU32 count=D2Count(f->modules);
    if(f->module_index>=count){f->stage=5;
        f->module_index=f->cursor=0;
        return 0;
    }
    ScDebugModule m;
    if(ScArrayGet(f->modules,f->module_index,&m)<0)return SC32_E_STALE;
    ScDebugSegment rows[32];
    ScDebugPage p;
    D2Record(&p,sizeof(p));
    ScStatus s=ScDebugSegments(&a->launch.target,f->snapshot.stop_generation,m.id,m.generation,f->cursor,rows,32,&p);
    if(s<0)return s;
    if(p.count)s=ScArrayInsert(f->segments,D2Count(f->segments),rows,p.count);
    if(s<0)return s;
    f->cursor=p.cursor;
    if(p.flags&SC32_DEBUG_PAGE_FINISHED){
        ScU32 ro=0,rw=0,declared_x=0,mixed=0;
        const ScDebugSegment *segments=D2Data(f->segments);
        for(ScU32 i=f->segment_cursor;i<D2Count(f->segments);i++){if(segments[i].access&SC32_DEBUG_WRITABLE)rw++;
            else ro++;
            if(segments[i].access&SC32_DEBUG_EXECUTABLE)declared_x++;
            if(segments[i].access&SC32_DEBUG_ACCESS_MIXED)mixed++;
        }
        D2Row module_row;
        if(ScArrayGet(f->rows[D2_MODULES],f->module_index,&module_row)==0){
            snprintf(module_row.text[4],sizeof(module_row.text[4]),"%s / RO%u RW%u X%u mixed%u / g%u",m.flags&SC32_DEBUG_MODULE_INITIALIZING?"initializing":"ready",ro,rw,declared_x,mixed,m.generation);
            s=ScArraySet(f->rows[D2_MODULES],f->module_index,&module_row);
            if(s<0)return s;
        }
        f->segment_cursor=D2Count(f->segments);
        f->module_index++;
        f->cursor=0;
    }return 0;
}
static ScStatus d2_capture_symbols(D2App *a,D2Frame *f)
{
    if(f->module_index>=D2Count(f->modules)){f->stage=6;
        return 0;
    }
    ScDebugModule m;
    if(ScArrayGet(f->modules,f->module_index,&m)<0)return SC32_E_STALE;
    ScDebugSymbol rows[32];
    ScDebugPage p;
    D2Record(&p,sizeof(p));
    ScStatus s=ScDebugSymbols(&a->launch.target,f->snapshot.stop_generation,m.id,m.generation,f->cursor,rows,32,&p);
    if(s<0)return s;
    for(ScU32 i=0;i<p.count;i++){D2Row r;
        memset(&r,0,sizeof(r));
        r.address=rows[i].address;
        r.aux=m.id;
        r.generation=m.generation;
        r.flags=rows[i].bytes;
        snprintf(r.text[0],sizeof(r.text[0]),"%s",m.name);
        snprintf(r.text[1],sizeof(r.text[1]),"%s",rows[i].name);
        snprintf(r.text[2],sizeof(r.text[2]),"%08X",rows[i].address);
        snprintf(r.text[3],sizeof(r.text[3]),rows[i].kind==SC32_DEBUG_SYMBOL_FUNCTION?"function / %uB":"data / %uB",rows[i].bytes);
        snprintf(r.text[4],sizeof(r.text[4]),"SCL export / epoch %u",m.generation);
        s=D2RowAppend(f->rows[D2_SYMBOLS],&r);
        if(s<0)return s;
    }
    f->cursor=p.cursor;
    if(p.flags&SC32_DEBUG_PAGE_FINISHED){f->module_index++;
        f->cursor=0;
    }return 0;
}
int D2AddressInModule(const D2Frame *f,const ScDebugModule *m,ScU32 address,ScU32 bytes,ScU32 access)
{
    if(!bytes || address>UINT_MAX-(bytes-1))return 0;
    const ScDebugSegment *segments=D2Data(f->segments);
    ScU32 n=D2Count(f->segments);
    for(ScU32 i=0;i<n;i++)if(segments[i].module==m->id && segments[i].module_generation==m->generation
    && address>=segments[i].address && address-segments[i].address<segments[i].bytes
    && bytes<=segments[i].bytes-(address-segments[i].address) && (segments[i].access&access)==access)return 1;
    return 0;
}
void D2Annotate(D2Frame *f)
{
    const D2Row *symbols=D2Data(f->rows[D2_SYMBOLS]),*breaks=D2Data(f->rows[D2_BREAKPOINTS]);
    ScU32 count=D2Count(f->rows[D2_SYMBOLS]),break_count=D2Count(f->rows[D2_BREAKPOINTS]);
    for(ScU32 pane=0;pane<2;pane++){ScArray *array=f->rows[pane?D2_STACK:D2_CODE];
        for(ScU32 i=0;i<D2Count(array);i++){D2Row r;
            if(ScArrayGet(array,i,&r)<0)continue;
            ScU32 address=pane?r.aux:r.address,best=0;
            int found=-1;
            if(pane && !r.flags)continue;
            for(ScU32 j=0;j<count;j++)if(address>=symbols[j].address && address-symbols[j].address<symbols[j].flags && (found<0 || symbols[j].address>best)){best=symbols[j].address;
                found=(int)j;
            }
            if(found>=0)snprintf(r.text[pane?3:4],160,"%.36s!%.96s+0x%X",symbols[found].text[0],symbols[found].text[1],address-best);
            if(!pane){const D2SourceLine *lines=D2Data(f->lines);
                ScU32 line_count=D2Count(f->lines),nearest=0;
                int match=-1;
                for(ScU32 j=0;j<line_count;j++)if(lines[j].address<=address && (match<0 || lines[j].address>=nearest)
                && found>=0 && lines[j].module==symbols[found].aux && lines[j].address>=symbols[found].address){nearest=lines[j].address;
                    match=(int)j;
                }
                if(match>=0){char previous[160];
                    snprintf(previous,sizeof(previous),"%s",r.text[4]);
                    snprintf(r.text[4],sizeof(r.text[4]),"%.70s | %.64s:%u",previous,lines[match].path,lines[match].line);
            }}
            if(!pane)for(ScU32 j=0;j<break_count;j++)if(breaks[j].address==r.address && !(breaks[j].flags&SC32_DEBUG_BP_INVALID))snprintf(r.text[0],sizeof(r.text[0]),r.address==f->snapshot.registers.eip?"> *":"*");
            (void)ScArraySet(array,i,&r);
        }
    }
}
ScStatus D2CaptureStep(D2App *a)
{
    D2Frame *f=a->candidate;
    if(!f)return 0;
    ScStatus s=0;
    if(f->stage==1){s=d2_primary(a,f);
        if(s==0)f->stage=2;
    }
    else if(f->stage==2)s=d2_capture_breaks(a,f);
    else if(f->stage==3)s=d2_capture_modules(a,f);
    else if(f->stage==4)s=d2_capture_segments(a,f);
    else if(f->stage==5)s=d2_capture_symbols(a,f);
    else if(f->stage==6){s=D2SidecarStep(a,f);
        if(s==SC_UI_END){s=0;
            f->stage=7;
    }}
    else{ScDebugSnapshot final;
        D2Record(&final,sizeof(final));
        s=ScDebugQuery(&a->launch.target,&final);
        if(s==0 && (final.state!=SC32_DEBUG_PAUSED || final.stop_generation!=f->snapshot.stop_generation
        || final.module_epoch!=f->snapshot.module_epoch || final.breakpoint_epoch!=f->snapshot.breakpoint_epoch))s=SC32_E_STALE;
        if(s==0){D2Annotate(f);
            a->candidate=0;
            D2Publish(a,f);
            return 0;
    }}
    if(s<0){D2FrameDestroy(a->candidate);
        a->candidate=0;
        D2Error(a,"Paused snapshot",s);
        return s;
    }
    return SC32_PENDING;
}
ScStatus D2Launch(D2App *a,const char *command)
{
    if(a->active)return SC32_E_BUSY;
    if(!command || !*command || strlen(command)>=sizeof(a->command))return SC32_E_RANGE;
    ScDebugLaunchDesc desc;
    D2Record(&desc,sizeof(desc));
    desc.command=command;
    for(ScU32 i=0;i<3;i++)desc.streams[i]=-1;
    ScDebugLaunchInfo info;
    D2Record(&info,sizeof(info));
    ScStatus s=ScDebugLaunch(&desc,&info);
    if(s<0)return s;
    snprintf(a->command,sizeof(a->command),"%s",command);
    a->launch=info;
    a->active=1;
    a->follow_ip=1;
    a->memory_address=0;
    D2Record(&a->current,sizeof(a->current));
    return D2CaptureStart(a);
}
ScStatus D2Control(D2App *a,ScU32 action)
{
    if(!a->active)return SC32_E_STALE;
    ScDebugSnapshot output;
    D2Record(&output,sizeof(output));
    ScStatus s=ScDebugControl(&a->launch.target,a->current.stop_generation,action,&output);
    if(s<0)return s;
    a->current=output;
    D2FrameDestroy(a->candidate);
    a->candidate=0;
    if(output.state==SC32_DEBUG_PAUSED)return D2CaptureStart(a);
    D2Clear(a);
    D2Status(a,output.state==SC32_DEBUG_RUNNING?"Running - paused panes unavailable":"Target terminating");
    D2Schedule(a);
    return 0;
}
void D2Poll(D2App *a)
{
    if(!a->active){if(a->closing)ScUiContextStop(a->ui,0);
        D2Schedule(a);
        return;
    }
    ScJobInfo job;
    D2Record(&job,sizeof(job));
    ScStatus s=ScJobQuery(a->launch.job,&job);
    if(s<0 || job.child_pid!=a->launch.target.pid || job.child_generation!=a->launch.target.generation || job.state==SC32_JOB_FINISHED){
        if(s==0 && job.state==SC32_JOB_FINISHED){ScJobInfo result;
            D2Record(&result,sizeof(result));
            (void)ScJobWait(a->launch.job,0,&result);
            char text[128];
            snprintf(text,sizeof(text),"Target exited (%d)",job.exit_code);
            D2Status(a,text);
        }
        else D2Error(a,"Target identity expired",s<0?s:SC32_E_STALE);
        a->active=0;
        D2FrameDestroy(a->candidate);
        a->candidate=0;
        D2Clear(a);
        if(a->closing)ScUiContextStop(a->ui,0);
        else if(a->restart_pending){a->restart_pending=0;
            char command[1088];
            snprintf(command,sizeof(command),"%s",a->command);
            ScStatus launched=D2Launch(a,command);
            if(launched<0)D2Error(a,"Restart",launched);
        }D2Schedule(a);
        return;
    }
    if(a->closing)return;
    if(a->candidate){(void)D2CaptureStep(a);
        D2Schedule(a);
        return;
    }
    ScDebugSnapshot now;
    D2Record(&now,sizeof(now));
    s=ScDebugQuery(&a->launch.target,&now);
    if(s<0){D2Error(a,"Debug state",s);
        return;
    }
    if(now.state!=SC32_DEBUG_PAUSED){if(a->current.state!=now.state){a->current=now;
            D2Clear(a);
            D2Status(a,"Running - paused panes unavailable");
        }return;
    }
    if(a->current.state!=now.state || a->current.stop_generation!=now.stop_generation
    || a->current.module_epoch!=now.module_epoch || a->current.breakpoint_epoch!=now.breakpoint_epoch)(void)D2CaptureStart(a);
}
void D2Close(D2App *a)
{
    if(a->closing)return;
    a->closing=1;
    D2FrameDestroy(a->candidate);
    a->candidate=0;
    if(a->active){ScDebugSnapshot result;
        D2Record(&result,sizeof(result));
        ScStatus s=ScDebugControl(&a->launch.target,0,SC32_DEBUG_TERMINATE,&result);
        if(s<0 && s!=SC32_E_STALE)D2Error(a,"Stop target",s);
        else D2Status(a,"Closing - reclaiming owned target");
    }
    else ScUiContextStop(a->ui,0);
    /* 关闭后即使窗口隐藏，领作业/回收仍推进；这不生成客户帧。 */
    a->timer_period=0;
    D2Schedule(a);
}
void D2Schedule(D2App *a)
{
    ScU32 period=a->closing?1:a->candidate?1:a->active?(a->current.state==SC32_DEBUG_RUNNING?5:25):0;
    ScUiNode root=a->float_root?a->float_root:a->root;
    if(!period){if(a->timer)ScUiTimerCancel(a->ui,a->timer);
        a->timer=a->timer_period=a->timer_root=0;
        return;
    }
    if(a->timer && a->timer_period==period && a->timer_root==root)return;
    ScUiTimerDesc timer;
    D2Record(&timer,sizeof(timer));
    timer.node=root;
    timer.flags=a->closing?SC_UI_TIMER_BACKGROUND:SC_UI_TIMER_DRAW_ONLY;
    ScMonotonicClock clock;
    D2Record(&clock,sizeof(clock));
    if(ScSystemMonotonic(&clock)<0)return;
    timer.deadline=clock.ticks;
    timer.deadline.lo+=period;
    if(timer.deadline.lo<period)timer.deadline.hi++;
    timer.period_ticks=period;
    ScStatus s=a->timer?ScUiTimerChange(a->ui,a->timer,&timer):ScUiTimerSet(a->ui,&timer,&a->timer);
    if(s==SC_UI_E_STALE){a->timer=0;
        s=ScUiTimerSet(a->ui,&timer,&a->timer);
    }
    if(s==0){a->timer_period=period;
        a->timer_root=root;
    }
}
