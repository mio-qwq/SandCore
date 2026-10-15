#include "SCFILEWORKPRIVATE.H"

/* 诊断由本人环境显式选择普通可写文件，仍逐次受真实FS权限约束。
 * 不使用硬件调试串口，不把状态日志改成提交ACK；失败原因先冻结，
 * 再做可失败的日志IO，避免诊断覆盖原始ScErrorInfo。默认完全关闭。 */
static void sw_failure_trace(void)
{
    char path[128];ScU32 count=0;
    if(ScProcessEnvironment("SCSHELL_TRACE",path,sizeof(path),&count) || !count)return;
    FILE *stream=fopen(path,"a");if(!stream)return;
    fprintf(stream,"worker=%u/%u status=%d cause=%d domain=%u raw=%d state=%u planning=%u roots=%u items=%u recovery=%u active_phase=%u\n",
        sw.self.pid,sw.self.generation,sw.failure,sw.failure_cause.code,sw.failure_cause.domain,
        sw.failure_cause.raw,sw.state,sw.planning_complete,sw.root_count,sw.item_counter,
        sw.recovery_phase,sw.active?sw.active->phase:0);
    fclose(stream);
}

/* 聚合只包含worker自己的职责片段。四大库始终来自独立SCL，
 * 不包含SCAPI/内核结构/汇编，也不静态复制库正文。 */
#include "worker_lease.c"
#include "worker_plan.c"
#include "worker_protocol.c"
#include "worker_recovery.c"
#include "worker_transfer.c"
#include "worker_retry.c"

static void sw_fail(ScStatus status)
{
    if(!sw.failure){
        sw.failure=status?status:SC32_E_FAILED;
        SwItem error;
        memset(&error,0,sizeof(error));
        sw_error(&error,sw.failure);
        sw.failure_cause=error.cause;
    }
    sw.cancelled=1;
}
static ScStatus sw_number(const char **cursor,ScU32 *out,int last)
{
    const char *text=*cursor;
    ScU32 result=0;
    if(*text<'0' || *text>'9' || (*text=='0' && text[1]>='0' && text[1]<='9'))return SC32_E_INVALID;
    while(*text>='0' && *text<='9'){
        ScU32 digit=(ScU32)(*text-'0');
        if(result>(0xffffffffu-digit)/10u)return SC32_E_RANGE;
        result=result*10u+digit;
        text++;
    }
    if(last?*text!=0:*text!='.')return SC32_E_INVALID;
    *cursor=last?text:text+1;
    *out=result;
    return 0;
}
static ScStatus sw_bootstrap(void)
{
    sw_record(&sw.self,sizeof(sw.self));
    ScStatus status=ScProcessSelf(&sw.self);
    if(status)return status;
    sw_record(&sw.identity,sizeof(sw.identity));
    status=ScIdentityQuery(&sw.identity);
    if(status)return status;
    /* 普通root的MANAGE不是硬件SYSTEM；Broker身份的flags另有定义，
     * 不能把本人的管理位误作MOD能力而禁掉合法root文件操作。 */
    if(sw.identity.realm!=SC32_BROKER_REALM_NORMAL || (sw.identity.flags&~SC32_IDENTITY_MANAGE) || sw.identity.uid<0
       || sw.identity.gid<0 || sw.identity.task_generation!=sw.self.generation)return SC32_E_ACCESS;
    ScCapabilities capabilities;
    sw_record(&capabilities,sizeof(capabilities));
    status=ScSystemCapabilities(&capabilities);
    if(status)return status;
    if(capabilities.abi_major!=1 || capabilities.abi_minor<3 || !(capabilities.capabilities_hi&SC32_CAP_LEASES_HI))return SC32_E_VERSION;
    ScU32 bytes=0;
    status=ScProcessArguments(sw.endpoint,sizeof(sw.endpoint),&bytes);
    if(status)return status;
    if(bytes>=sizeof(sw.endpoint) || strlen(sw.endpoint)!=bytes || strncmp(sw.endpoint,"SCSJ.",5))return SC32_E_INVALID;
    const char *cursor=sw.endpoint+5;
    ScU32 session=0;
    sw_record(&sw.parent,sizeof(sw.parent));
    status=sw_number(&cursor,&sw.parent.pid,0);
    if(!status)status=sw_number(&cursor,&sw.parent.generation,0);
    if(!status)status=sw_number(&cursor,&session,0);
    if(!status)status=sw_number(&cursor,&sw.request.lo,0);
    if(!status)status=sw_number(&cursor,&sw.request.hi,1);
    if(status)return status;
    if(!sw.parent.pid || !sw.parent.generation || session!=sw.identity.session || (!sw.request.lo && !sw.request.hi))return SC32_E_ACCESS;
    ScIpcConnectDesc desc;
    sw_record(&desc,sizeof(desc));
    desc.name=sw.endpoint;
    desc.namespace_id=SC32_IPC_NAMESPACE_SESSION;
    desc.session=session;
    status=ScIpcConnect(&desc,&sw.channel);
    if(status)return status;
    for(;;){
        sw_record(&sw.peer,sizeof(sw.peer));
        status=ScIpcPeer(sw.channel,&sw.peer);
        if(status!=SC32_E_AGAIN)break;
        ScBrokerWaitDesc wait;
        sw_record(&wait,sizeof(wait));
        wait.handle=sw.channel;
        wait.ready_flags=SC32_BROKER_READY_WRITE|SC32_BROKER_READY_CLOSED;
        wait.minimum_write_bytes=SHW_HEADER_BYTES;
        wait.timeout=0xffffffffu;
        ScU32 events=0;
        status=ScBrokerWait(&wait,&events);
        if(status!=0 && status!=SC32_TIMEOUT)return status;
    }
    if(status)return status;
    if(sw.peer.pid!=sw.parent.pid || sw.peer.generation!=sw.parent.generation || sw.peer.session!=session
       || sw.peer.realm!=SC32_BROKER_REALM_NORMAL || sw.peer.flags || sw.peer.uid!=sw.identity.uid || sw.peer.gid!=sw.identity.gid
       || sw.peer.subject_uid!=sw.identity.uid || sw.peer.subject_gid!=sw.identity.gid)return SC32_E_ACCESS;
    ScArrayDesc array;
    sw_record(&array,sizeof(array));
    array.item_bytes=sizeof(SwItem *);
    status=ScArrayCreate(&array,&sw.order);
    if(status)return status;
    ScHashDesc hash;
    sw_record(&hash,sizeof(hash));
    hash.value_bytes=sizeof(SwItem *);
    status=ScHashCreate(&hash,&sw.paths);
    if(status)return status;
    sw.io=(unsigned char *)malloc(SW_IO_BYTES);
    sw.rows=(ScLeaseEntry *)malloc(SW_PLAN_QUANTUM*sizeof(ScLeaseEntry));
    if(!sw.io || !sw.rows)return SC32_E_MEMORY;
    sw.state=SHW_CONNECTING;
    return 0;
}
static ScStatus sw_wait_channel(void)
{
    ScBrokerWaitDesc desc;
    sw_record(&desc,sizeof(desc));
    desc.handle=sw.channel;
    desc.ready_flags=SC32_BROKER_READY_READ|SC32_BROKER_READY_CLOSED;
    if(sw.output_head){desc.ready_flags|=SC32_BROKER_READY_WRITE;
        desc.minimum_write_bytes=sw.output_head->bytes;
    }
    desc.timeout=0xffffffffu;
    ScU32 events=0;
    ScStatus status=ScBrokerWait(&desc,&events);
    /* ready与安装wait由同一次typed gate处理；醒来只重新检查真实
     * packet，不把STREAM旧事实/100Hz tick当成队列数据或EOF。 */
    return status==SC32_TIMEOUT?0:status;
}
static ScStatus sw_job_step(void)
{
    if(sw.retry_item){ScStatus status=sw_retry_step();
        return status==SC32_PENDING?0:status;
    }
    if(!sw.planning_complete){
        if(sw.cancelled){sw.finished=1;
            return 0;
        }
        if(sw.planning_root){
            ScStatus status=sw_plan_root(sw.planning_root);
            if(status!=0 && status!=SC32_PENDING)return status;
            if(!status)sw.planning_root=sw.planning_root->next;
            return sw_emit_progress(0);
        }
        ScStatus status=sw_plan_finish();
        if(status)return status;
    }
    if(!sw.recovery_ready){
        if(sw.cancelled){sw.finished=1;
            return 0;
        }
        ScStatus status=sw_journal_create();
        if(status!=0 && status!=SC32_PENDING)return status;
        if(sw.recovery_ready)sw.state=SHW_RUNNING;
        return 0;
    }
    SwRoot *root=sw.running_root;
    if(!root){sw.finished=1;
        return 0;
    }
    if(!sw.executing_deletes && sw.active_index<root->last){
        SwItem *item=sw.sorted[sw.active_index];
        sw.active=item;
        ScStatus status=sw_item_step(item);
        if(status!=0 && status!=SC32_PENDING)return status;
        if(item->phase==SW_DONE || item->phase==SW_FINALIZE)sw.active_index++;
        return 0;
    }
    if(!sw.executing_deletes){sw.executing_deletes=1;
        sw.delete_index=root->last;
    }
    if(sw.delete_index>root->first){
        SwItem *item=sw.sorted[sw.delete_index-1];
        sw.active=item;
        if(item->phase==SW_FINALIZE)item->finalizing=1;
        ScStatus status=sw_item_step(item);
        if(status!=0 && status!=SC32_PENDING)return status;
        if(item->phase==SW_DONE)sw.delete_index--;
        return 0;
    }
    if(sw.record_count){ScStatus status=sw_journal_flush();
        if(status)return status;
        return 0;
    }
    if(sw.result_head){ScStatus status=sw_deliver_results();
        if(status)return status;
        return 0;
    }
    ScStatus status=sw_lease_release(&root->destination_scope);
    if(status==SC32_PENDING)return 0;
    if(status)return status;
    status=sw_lease_release(&root->scope);
    if(status==SC32_PENDING)return 0;
    if(status)return status;
    root->completed=1;
    sw.running_root=root->next;
    sw.executing_deletes=0;
    sw.active=0;
    if(sw.running_root)sw.active_index=sw.running_root->first;
    return sw_emit_progress(0);
}
static ScStatus sw_final(void)
{
    if(sw.final_queued)return 0;
    int uncertain=0;
    for(SwItem *item=sw.items;item;item=item->next)if(item->outcome==SW_OUT_UNKNOWN){uncertain=1;
        break;
    }
    sw.state=uncertain?SHW_UNKNOWN:sw.failure?SHW_FAILED:sw.user_cancel?SHW_CANCELLED:sw.items_failed?SHW_FAILED:SHW_FINISHED;
    if(!sw.failure && sw.items_failed && !sw.user_cancel)sw.failure=SC32_E_FAILED;
    if(sw.recovery_ready && !sw.recovery_failed){
        ScStatus status=sw_journal_record(0,SW_JOURNAL_FINISH,1);
        if(status){sw.recovery_failed=1;
            sw_fail(status);
            sw.state=uncertain?SHW_UNKNOWN:SHW_FAILED;
        }
    }
    ScShellWireJobProgress progress;
    sw_progress(&progress,0);
    if(sw.user_cancel && !sw.failure)progress.status=SC32_E_CANCELLED;
    ScStatus status=0;
    if(sw.user_cancel)status=sw_queue(SHW_JOB_CANCELLED_ACK,&progress,sizeof(progress),SHW_EVENT,progress.status);
    if(!status)status=sw_queue(SHW_JOB_RESULT,&progress,sizeof(progress),SHW_EVENT,progress.status);
    if(!status)sw.final_queued=1;
    return status;
}
static void sw_cleanup_lease(SwLease *lease)
{
    while(lease->valid){
        ScStatus status=sw_lease_release(lease);
        if(!status)break;
        /* 异常永久拒绝交给真正kernel owner-exit回收；kernel从不借
         * R3 buffer/pd跨续进，不能在atexit因坏句柄永久自旋。 */
        if(status!=SC32_PENDING)break;
        ScProcessYield();
    }
}
static void sw_cleanup(void)
{
    if(sw.cleaned)return;
    sw.cleaned=1;
    sw_cleanup_lease(&sw.target_lease);
    sw_cleanup_lease(&sw.retry_lease);
    sw_cleanup_lease(&sw.candidate_lease);
    sw_cleanup_lease(&sw.source_lease);
    for(SwRoot *root=sw.roots;root;root=root->next){
        sw_cleanup_lease(&root->destination_scope);
        sw_cleanup_lease(&root->scope);
        ScProcessYield();
    }
    if(sw.recovery_ready && !sw.recovery_failed && sw.record_count)sw_journal_flush();
    sw_cleanup_lease(&sw.recovery_lease);
    if(sw.channel){ScIpcClose(sw.channel);
        sw.channel=0;
    }
    while(sw.output_head){SwOutbound *next=sw.output_head->next;
        free(sw.output_head);
        sw.output_head=next;
    }
    while(sw.items){SwItem *next=sw.items->next;
        free(sw.items);
        sw.items=next;
    }
    while(sw.roots){SwRoot *next=sw.roots->next;
        free(sw.roots);
        sw.roots=next;
    }
    if(sw.paths)ScHashRelease(sw.paths);
    if(sw.order)ScArrayRelease(sw.order);
    if(sw.incoming)ScBufferRelease(sw.incoming);
    if(sw.journal)ScBufferRelease(sw.journal);
    free(sw.sorted);
    free(sw.selected);
    free(sw.rows);
    free(sw.io);
}
static ScStatus sw_stop_current(void)
{
    if(sw.retry_item){ScStatus status=sw_retry_step();
        if(status!=0)return status;
    }
    if(!sw.active || sw.active->phase==SW_DONE)return 0;
    ScStatus status=sw_item_step(sw.active);
    if(status!=0 && status!=SC32_PENDING)return status;
    return sw.active->phase==SW_DONE?0:SC32_PENDING;
}
int main(void)
{
    memset(&sw,0,sizeof(sw));
    if(atexit(sw_cleanup)){sw_cleanup();
        return 1;
    }
    ScStatus status=sw_bootstrap();
    if(status){sw_fail(status);
        sw_cleanup();
        return 1;
    }
    for(;;){
        status=sw_protocol_pump();
        if(status){
            /* 不消费的坏Peek帧不能让READ永远ready而忙转。协议损坏
             * 直接EOF，cleanup在真实Close安全点撤候选；caller把未收
             * 到ITEM的操作视UNKNOWN，不按超时/断线重放副作用。 */
            sw.protocol_error=1;
            sw_fail(status);
            break;
        }
        if(sw.peer_closed)break;
        status=sw_protocol_flush();
        if(status!=0 && status!=SC32_E_AGAIN){sw_fail(status);
            break;
        }
        if(sw.output_head && !sw.cancelled){status=sw_wait_channel();
            if(status){sw_fail(status);
                break;
            }continue;
        }
        if(sw.final_queued){if(!sw.output_head)break;
            status=sw_wait_channel();
            if(status)break;
            continue;
        }
        if(sw.protocol_error || sw.failure){
            status=sw_stop_current();
            if(status==SC32_PENDING){ScProcessYield();
                continue;
            }
            if(status)break;
            sw.finished=1;
        }
        else if(sw.started){status=sw_job_step();
            if(status){sw_fail(status);
                sw_failure_trace();
                sw.finished=1;
            }}
        else if(sw.cancelled)sw.finished=1;
        else{status=sw_wait_channel();
            if(status){sw_fail(status);
                break;
            }continue;
        }
        if(sw.result_head && !sw.output_head){status=sw_deliver_results();
            if(status){sw_fail(status);
                sw.finished=1;
            }}
        if(sw.finished){
            if(sw.record_count && !sw.recovery_failed){status=sw_journal_flush();
                if(status){sw.recovery_failed=1;
                    sw_fail(status);
                }}
            if(sw.result_head && !sw.output_head){status=sw_deliver_results();
                if(status)break;
                continue;
            }
            status=sw_final();
            if(status)break;
        }
        if(sw.active && sw.active->phase==SW_CONFLICT && !sw.output_head && !sw.cancelled){
            status=sw_wait_channel();
            if(status){sw_fail(status);
                break;
            }
        }else ScProcessYield();
    }
    ScI32 result=sw.failure?1:sw.user_cancel?2:0;
    sw_cleanup();
    return result;
}
