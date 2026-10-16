struct SwOutbound {
    SwOutbound *next;
    ScU32 bytes;
    unsigned char data[1];
};
static int sw_identity_same(const ScBrokerIdentity *a,const ScBrokerIdentity *b)
{
    return a->size==sizeof(*a) && a->version==1 && a->pid==b->pid && a->generation==b->generation
        && a->uid==b->uid && a->gid==b->gid && a->realm==b->realm && a->session==b->session
        && a->subject_uid==b->subject_uid && a->subject_gid==b->subject_gid && a->flags==b->flags && !a->reserved;
}
static ScStatus sw_queue(ScU32 opcode,const void *data,ScU32 bytes,ScU32 flags,ScStatus status)
{
    if(bytes>SHW_FRAME_BYTES-SHW_HEADER_BYTES || sw.epoch==0xffffffffu)return SC32_E_RANGE;
    ScU32 total=SHW_HEADER_BYTES+bytes;
    if(sw.output_tail && opcode==SHW_JOB_PROGRESS && flags==SHW_EVENT){
        ScShellWireHeader *old=(ScShellWireHeader *)sw.output_tail->data;
        if(old->opcode==opcode && old->flags==flags && sw.output_tail->bytes==total){
            old->state_epoch=++sw.epoch;
            old->request_lo=sw.last_request_lo;
            old->request_hi=sw.last_request_hi;
            old->status=status;
            memcpy(sw.output_tail->data+SHW_HEADER_BYTES,data,bytes);
            return 0;
        }
    }
    /* 这是每个连接的有界发送背压，不是作业/任务数量门槛。主循环
     * 有待发结果就停止新的FS副作用，持续洪泛query则明确失败。 */
    if(sw.output_bytes>4u*SHW_FRAME_BYTES-total)return SC32_E_BUSY;
    SwOutbound *candidate=(SwOutbound *)malloc(sizeof(*candidate)+total-1);
    if(!candidate)return SC32_E_MEMORY;
    candidate->next=0;
    candidate->bytes=total;
    ScShellWireHeader header;
    sw_record(&header,sizeof(header));
    header.opcode=opcode;
    header.flags=flags;
    header.request_lo=sw.last_request_lo;
    header.request_hi=sw.last_request_hi;
    header.body_bytes=header.total_bytes=bytes;
    header.state_epoch=++sw.epoch;
    header.status=status;
    memcpy(candidate->data,&header,sizeof(header));
    memcpy(candidate->data+sizeof(header),data,bytes);
    if(sw.output_tail)sw.output_tail->next=candidate;
    else sw.output_head=candidate;
    sw.output_tail=candidate;
    sw.output_bytes+=total;
    return 0;
}
static ScStatus sw_protocol_flush(void)
{
    if(!sw.output_head)return 0;
    SwOutbound *front=sw.output_head;
    ScStatus status=ScIpcSend(sw.channel,SC32_IPC_FRAME_DATA,front->data,front->bytes);
    if(status==SC32_E_AGAIN){sw.output_blocked=1;
        return status;
    }
    if(status){sw.peer_closed=1;
        return status;
    }
    sw.output_blocked=0;
    sw.output_bytes-=front->bytes;
    sw.output_head=front->next;
    if(!sw.output_head)sw.output_tail=0;
    free(front);
    return 0;
}
static void sw_progress(ScShellWireJobProgress *out,SwItem *item)
{
    sw_record(out,sizeof(*out));
    out->job_lo=sw.request.lo;
    out->job_hi=sw.request.hi;
    out->state=sw.state;
    out->stage=item?item->stage:0;
    out->current_item=item?item->id:0;
    out->items_total=sw.item_counter;
    out->items_done=sw.items_done;
    out->items_failed=sw.items_failed;
    out->items_skipped=sw.items_skipped;
    out->total_known=sw.planning_complete && !sw.plan_incomplete;
    ScWideCounter progress=sw.confirmed_bytes;
    if(item && item->kind==1 && !item->published && item->offset && (sw.operation==SW_COPY || sw.operation==SW_MOVE)){
        sw_counter_add(&progress,item->offset);
        out->flags=SHW_PROGRESS_PRIVATE_PREFIX;
    }
    out->bytes_lo=progress.lo;
    out->bytes_hi=progress.hi;
    out->total_lo=sw.total_bytes.lo;
    out->total_hi=sw.total_bytes.hi;
    if(item){out->conflict_epoch=item->conflict_epoch;
        out->status=item->status;
        out->cause=item->cause.code;
        out->domain=item->cause.domain;
        out->raw=item->cause.raw;
    }
    else{out->status=sw.failure;
        out->cause=sw.failure_cause.code;
        out->domain=sw.failure_cause.domain;
        out->raw=sw.failure_cause.raw;
    }
}
static ScStatus sw_emit_progress(SwItem *item)
{
    ScShellWireJobProgress value;
    sw_progress(&value,item);
    return sw_queue(SHW_JOB_PROGRESS,&value,sizeof(value),SHW_EVENT,0);
}
static void sw_ack(SwItem *item,ScShellWireJobAck *ack,int live)
{
    sw_record(ack,sizeof(*ack));
    ack->job_lo=sw.request.lo;
    ack->job_hi=sw.request.hi;
    ack->operation=sw.operation;
    ack->worker_pid=sw.self.pid;
    ack->worker_generation=sw.self.generation;
    if(!item){ack->state=sw.state;
        ack->status=sw.failure?sw.failure:sw.user_cancel?SC32_E_CANCELLED:0;
        ack->cause=sw.failure_cause.code;
        ack->domain=sw.failure_cause.domain;
        ack->raw=sw.failure_cause.raw;
        return;
    }
    ack->item_id=item->id;
    ack->stage=item->stage;
    ack->status=item->status;
    ack->cause=item->cause.code;
    ack->domain=item->cause.domain;
    ack->raw=item->cause.raw;
    ack->bytes_lo=item->confirmed_bytes;
    ack->source_revision=item->source_revision;
    ack->destination_revision=item->destination_revision;
    ack->conflict_epoch=item->conflict_epoch;
    ack->state=item->outcome==SW_OUT_SKIPPED?SHW_RESULT_SKIPPED:item->outcome==SW_OUT_CANCELLED?SHW_RESULT_CANCELLED:
        item->outcome==SW_OUT_FAILED?SHW_RESULT_FAILED:item->outcome==SW_OUT_UNKNOWN?SHW_RESULT_UNCERTAIN:
        item->outcome==SW_OUT_SOURCE_RETAINED?SHW_RESULT_SOURCE_RETAINED:SHW_RESULT_DONE;
    if(item->durable && item->recovery_written)ack->flags|=SHW_DURABLE;
    if(item->recovery_written)ack->flags|=SHW_RECOVERY_WRITTEN;
    if(item->outcome==SW_OUT_SOURCE_RETAINED)ack->flags|=SHW_SOURCE_RETAINED;
    if(live){
        if(sw.source && sw.source->valid && !item->source_removed && sw.source->info.object_id==item->source_object
           && sw_same(sw.source->info.path,item->source))ack->source_object=item->source_object;
        if(sw.target_lease.valid && sw_same(sw.target_lease.info.path,item->destination))ack->destination_object=sw.target_lease.info.object_id;
        else if(sw.candidate_lease.valid && item->published && sw_same(sw.candidate_lease.info.path,item->destination))ack->destination_object=sw.candidate_lease.info.object_id;
        if(ack->source_object || ack->destination_object)ack->flags|=SHW_OBSERVATION_LIVE;
    }
    memcpy(ack->source,item->source,68);
    memcpy(ack->destination,item->destination,68);
}
static ScStatus sw_emit_result(SwItem *item)
{
    ScShellWireJobAck value;
    sw_ack(item,&value,0);
    /* 完成消息只给revision历史事实，不承诺消息排队后仍有open pin。 */
    return sw_queue(SHW_JOB_ITEM,&value,sizeof(value),SHW_EVENT,item->status);
}
static ScStatus sw_emit_conflict(SwItem *item)
{
    ScShellWireJobAck value;
    sw_ack(item,&value,1);
    value.source_revision=item->conflict_source_revision;
    value.destination_revision=item->conflict_destination_revision;
    if(item->published && sw.operation==SW_MOVE && !item->source_removed){value.state=SHW_RESULT_SOURCE_RETAINED;
        value.flags|=SHW_SOURCE_RETAINED;
    }
    return sw_queue(SHW_JOB_CONFLICT,&value,sizeof(value),SHW_EVENT,item->status);
}
static int sw_mark_range(unsigned char *used,ScU32 total,ScU32 offset,ScU32 count)
{
    if(offset>total || count>total-offset)return 0;
    for(ScU32 i=offset;i<offset+count;i++){
        unsigned char mask=(unsigned char)(1u<<(i&7u));
        if(used[i>>3]&mask)return 0;
        used[i>>3]|=mask;
    }
    return 1;
}
static ScStatus sw_wire_path(const unsigned char *body,ScU32 bytes,unsigned char *used,
    ScU32 floor,ScU32 offset,ScU32 count,int optional,char out[68])
{
    memset(out,0,68);
    if(!count)return optional && !offset?0:SC32_E_INVALID;
    if(count>65 || (offset&3u) || offset<floor || offset>bytes || count>bytes-offset)return SC32_E_INVALID;
    if(body[offset+count-1] || memchr(body+offset,0,count-1))return SC32_E_INVALID;
    ScU32 rounded=(count+3u)&~3u;
    if(!sw_mark_range(used,bytes,offset,rounded))return SC32_E_INVALID;
    for(ScU32 i=count;i<rounded;i++)if(body[offset+i])return SC32_E_INVALID;
    return sw_path((const char *)body+offset,out);
}
static ScStatus sw_parse_plan(const unsigned char *body,ScU32 bytes)
{
    if(bytes<sizeof(ScShellWireJobBegin))return SC32_E_INVALID;
    const ScShellWireJobBegin *begin=(const ScShellWireJobBegin *)body;
    if(begin->size!=sizeof(*begin) || begin->version!=1 || begin->operation<SHW_COPY || begin->operation>SHW_MKDIR
       || (begin->flags&~(SHW_RECURSIVE|SHW_PRESERVE_MODE)) || begin->job_lo!=sw.request.lo || begin->job_hi!=sw.request.hi
       || !begin->count || begin->items_offset!=sizeof(*begin) || begin->count>(bytes-sizeof(*begin))/sizeof(ScShellWireJobItem)
       || begin->conflict_policy || begin->final_mode>63 || begin->reserved[0] || begin->reserved[1]
       || ((begin->flags&SHW_PRESERVE_MODE) && begin->operation!=SHW_COPY && begin->operation!=SHW_MOVE))return SC32_E_INVALID;
    if(begin->count>0xffffffffu/(ScU32)sizeof(SwRoot *) || bytes>0xfffffff8u)return SC32_E_RANGE;
    ScU32 floor=sizeof(*begin)+begin->count*(ScU32)sizeof(ScShellWireJobItem);
    unsigned char *used=(unsigned char *)calloc((bytes+7u)/8u,1);
    if(!used)return SC32_E_MEMORY;
    memset(used,255,floor/8u);
    for(ScU32 bit=(floor&~7u);bit<floor;bit++)used[bit>>3]|=(unsigned char)(1u<<(bit&7u));
    char global[68];
    ScStatus status=sw_wire_path(body,bytes,used,floor,begin->destination_offset,begin->destination_bytes,1,global);
    if(!status)status=sw_wire_path(body,bytes,used,floor,begin->recovery_offset,begin->recovery_bytes,0,sw.recovery_path);
    if(!status && global[0] && begin->operation!=SW_COPY && begin->operation!=SW_MOVE)status=SC32_E_INVALID;
    ScHash *ids=0;
    ScHashDesc id_desc;
    sw_record(&id_desc,sizeof(id_desc));
    id_desc.value_bytes=4;
    if(!status)status=ScHashCreate(&id_desc,&ids);
    if(!status){sw.selected=(SwRoot **)calloc(begin->count,sizeof(SwRoot *));
        if(!sw.selected)status=SC32_E_MEMORY;
    }
    if(!status){sw.operation=begin->operation;
        sw.flags=begin->flags;
        sw.final_mode=begin->final_mode;
    }
    for(ScU32 i=0;!status && i<begin->count;i++){
        const ScShellWireJobItem *input=(const ScShellWireJobItem *)(body+begin->items_offset+i*sizeof(*input));
        if(input->size!=sizeof(*input) || input->version!=1 || !input->item_id || (input->kind!=1 && input->kind!=2)
           || (input->flags&~7u) || ((input->flags&SHW_ITEM_EXPECT_ABSENT) && (input->flags&SHW_ITEM_EXPECT_DESTINATION)) || ((input->flags&SHW_ITEM_EXPECT_SOURCE)?!input->expected_source_revision:input->expected_source_revision!=0)
           || ((input->flags&SHW_ITEM_EXPECT_DESTINATION)?(!input->expected_destination_revision || (input->expected_destination_kind!=1 && input->expected_destination_kind!=2)):
                (input->expected_destination_revision || input->expected_destination_kind))) {status=SC32_E_INVALID;
            break;
        }
        if(sw.operation==SW_MKDIR && (input->kind!=2 || (input->flags&SHW_ITEM_EXPECT_SOURCE))){status=SC32_E_INVALID;
            break;
        }
        ScU32 marker=1;
        status=ScHashSet(ids,&input->item_id,4,&marker,SC_HASH_INSERT_ONLY);
        if(status)break;
        SwRoot *root=(SwRoot *)calloc(1,sizeof(*root));
        if(!root){status=SC32_E_MEMORY;
            break;
        }
        root->id=i+1;
        root->client_id=input->item_id;
        root->kind=input->kind;
        root->flags=input->flags;
        root->expected_revision=input->expected_source_revision;
        root->expected_destination_revision=input->expected_destination_revision;
        root->expected_destination_kind=input->expected_destination_kind;
        if(input->item_id>sw.next_item_id)sw.next_item_id=input->item_id;
        if(sw.last_root)sw.last_root->next=root;
        else sw.roots=root;
        sw.last_root=root;
        sw.selected[i]=root;
        sw.root_count++;
        status=sw_wire_path(body,bytes,used,floor,input->source_offset,input->source_bytes,sw.operation==SW_MKDIR,root->source);
        if(!status && sw.operation==SW_MKDIR && input->source_bytes)status=SC32_E_INVALID;
        if(!status)status=sw_wire_path(body,bytes,used,floor,input->destination_offset,input->destination_bytes,sw.operation==SW_COPY || sw.operation==SW_MOVE || sw.operation==SW_DELETE,root->destination);
        if(!status && sw.operation==SW_DELETE && root->destination[0])status=SC32_E_INVALID;
        if(!status && !root->destination[0] && (sw.operation==SW_COPY || sw.operation==SW_MOVE)){
            const char *leaf=strrchr(root->source,'/');
            if(!global[0] || !leaf || !leaf[1])status=SC32_E_INVALID;
            else status=sw_join(global,leaf+1,root->destination);
        }
        if(!status && root->source[0] && root->destination[0]
           && (sw_child(root->destination,root->source) || sw_child(root->source,root->destination)
               || (sw_same(root->source,root->destination) && sw.operation!=SW_RENAME)))status=SC32_E_INVALID;
    }
    if(!status)for(ScU32 i=floor;i<bytes;i++)if(!(used[i>>3]&(1u<<(i&7u))) && body[i]){status=SC32_E_INVALID;
        break;
    }
    if(ids)ScHashRelease(ids);
    free(used);
    if(status)return status;
    qsort(sw.selected,sw.root_count,sizeof(SwRoot *),sw_root_compare);
    SwRoot *ancestor=0;
    for(ScU32 i=0;i<sw.root_count;i++){
        SwRoot *root=sw.selected[i];
        if(ancestor && root->source[0] && (sw_same(root->source,ancestor->source) || sw_child(root->source,ancestor->source))){
            if((sw.flags&SHW_RECURSIVE) && ancestor->kind==2 && sw.operation!=SW_RENAME)root->skip=1;
            else if(sw_same(root->source,ancestor->source))return SC32_E_INVALID;
        }else ancestor=root;
        if(!root->skip && root->source[0] && root->kind!=2)ancestor=0;
    }
    /* 先按source排序，再对每个target二分寻找可能覆盖它的source
     * 前缀。目录深度受64B路径限制，不能用roots²检查大选择。 */
    for(SwRoot *root=sw.roots;root;root=root->next){
        if(!root->destination[0] || sw.operation==SW_MKDIR)continue;
        char prefix[68];
        memcpy(prefix,root->destination,68);
        ScU32 lower=0,upper=sw.root_count;
        while(lower<upper){ScU32 middle=lower+(upper-lower)/2;
            if(sw_path_compare(sw.selected[middle]->source,root->destination)<0)lower=middle+1;
            else upper=middle;
        }
        if(lower<sw.root_count && sw_child(sw.selected[lower]->source,root->destination))return SC32_E_INVALID;
        for(;;){
            ScU32 first=0,last=sw.root_count;
            while(first<last){ScU32 middle=first+(last-first)/2;
                int compare=sw_path_compare(sw.selected[middle]->source,prefix);
                if(compare<0)first=middle+1;
                else last=middle;
            }
            if(first<sw.root_count && sw.selected[first]->source[0] && sw_same(sw.selected[first]->source,prefix)
               && !(sw.operation==SW_RENAME && sw.selected[first]==root && sw_same(root->source,root->destination)))return SC32_E_INVALID;
            ScU32 n=(ScU32)strlen(prefix);
            while(n && prefix[n-1]!='/')n--;
            if(n<=1)break;
            prefix[n-1]=0;
        }
    }
    /* 全DELETE没有target，仍要排除日志位于任一待删source之内。 */
    for(SwRoot *root=sw.roots;root;root=root->next)
        if((root->source[0] && (sw_same(sw.recovery_path,root->source) || sw_child(sw.recovery_path,root->source)))
           || (root->destination[0] && (sw_same(sw.recovery_path,root->destination) || sw_child(sw.recovery_path,root->destination))))return SC32_E_INVALID;
    sw.input_complete=1;
    return 0;
}
static int sw_sequence_next(const ScShellWireHeader *header)
{
    ScU32 low=sw.last_request_lo+1,high=sw.last_request_hi+(low==0);
    return (low || high) && header->request_lo==low && header->request_hi==high;
}
static ScStatus sw_control_frame(const ScShellWireHeader *header,const unsigned char *body)
{
    if(header->opcode==SHW_JOB_RESOLVE){
        if(header->body_bytes<sizeof(ScShellWireJobDecision))return SC32_E_INVALID;
        const ScShellWireJobDecision *q=(const ScShellWireJobDecision *)body;
        if(q->size!=sizeof(*q) || q->version!=1 || q->job_lo!=sw.request.lo || q->job_hi!=sw.request.hi || q->decision<1 || q->decision>6)return SC32_E_INVALID;
        char path[68];
        memset(path,0,68);
        if(q->decision==SHW_DECISION_RENAME){
            if(!q->new_path_bytes || q->new_path_bytes>65 || header->body_bytes!=sizeof(*q)+((q->new_path_bytes+3u)&~3u)
               || body[sizeof(*q)+q->new_path_bytes-1] || memchr(body+sizeof(*q),0,q->new_path_bytes-1))return SC32_E_INVALID;
            for(ScU32 i=q->new_path_bytes;i<((q->new_path_bytes+3u)&~3u);i++)if(body[sizeof(*q)+i])return SC32_E_INVALID;
            ScStatus checked=sw_path((const char *)body+sizeof(*q),path);
            if(checked)return checked;
        }else if(q->new_path_bytes || header->body_bytes!=sizeof(*q))return SC32_E_INVALID;
        return sw_decide(q,path);
    }
    if(header->body_bytes!=sizeof(ScShellWireJobControl))return SC32_E_INVALID;
    const ScShellWireJobControl *q=(const ScShellWireJobControl *)body;
    if(q->size!=sizeof(*q) || q->version!=1 || q->job_lo!=sw.request.lo || q->job_hi!=sw.request.hi
       || q->item_id || q->flags || q->reserved[0] || q->reserved[1])return SC32_E_INVALID;
    if(header->opcode==SHW_JOB_COMMIT){
        if(!sw.input_complete || sw.committed || header->request_lo!=2 || header->request_hi)return SC32_E_INVALID;
        sw.committed=sw.started=1;
        sw.state=SHW_PLANNING;
        sw.planning_root=sw.roots;
        ScShellWireJobProgress progress;
        sw_progress(&progress,0);
        return sw_queue(SHW_JOB_ACCEPTED,&progress,sizeof(progress),SHW_REPLY,0);
    }
    if(header->opcode==SHW_JOB_CANCEL){
        sw.cancelled=sw.user_cancel=1;
        sw.state=SHW_CANCELLING;
        return 0;
    }
    if(header->opcode==SHW_JOB_QUERY){
        ScShellWireJobProgress progress;
        sw_progress(&progress,sw.active);
        return sw_queue(SHW_JOB_PROGRESS,&progress,sizeof(progress),SHW_REPLY,0);
    }
    return SC32_E_INVALID;
}
static ScStatus sw_protocol_pump(void)
{
    ScIpcPacket packet;
    sw_record(&packet,sizeof(packet));
    ScStatus status=ScIpcPeek(sw.channel,&packet);
    if(status==SC32_E_AGAIN)return 0;
    if(status)return status;
    if(!sw_identity_same(&packet.sender,&sw.peer))return SC32_E_ACCESS;
    if(packet.type==SC32_IPC_FRAME_EOF){
        status=ScIpcReceive(sw.channel,0,0,&packet);
        sw.peer_closed=sw.cancelled=1;
        return status;
    }
    if(packet.type!=SC32_IPC_FRAME_DATA || packet.bytes<SHW_HEADER_BYTES || packet.bytes>SHW_FRAME_BYTES)return SC32_E_INVALID;
    unsigned char *frame=(unsigned char *)malloc(packet.bytes);
    if(!frame)return SC32_E_MEMORY;
    ScU32 bytes=packet.bytes;
    sw_record(&packet,sizeof(packet));
    status=ScIpcReceive(sw.channel,frame,bytes,&packet);
    if(status){free(frame);
        return status==SC32_E_AGAIN?0:status;
    }
    if(packet.bytes!=bytes || packet.type!=SC32_IPC_FRAME_DATA || !sw_identity_same(&packet.sender,&sw.peer)){free(frame);
        return SC32_E_ACCESS;
    }
    ScShellWireHeader *header=(ScShellWireHeader *)frame;
    const unsigned char *body=frame+SHW_HEADER_BYTES;
    if(header->size!=sizeof(*header) || header->version!=1 || header->body_bytes!=bytes-SHW_HEADER_BYTES
       || header->status || header->reserved || (header->flags&~SHW_MORE))status=SC32_E_INVALID;
    else if(header->opcode==SHW_HELLO){
        if(sw.hello || header->request_lo || header->request_hi || header->flags || header->offset
           || header->total_bytes!=header->body_bytes || header->body_bytes!=sizeof(ScShellWireHello))status=SC32_E_INVALID;
        else{
            const ScShellWireHello *hello=(const ScShellWireHello *)body;
            if(hello->size!=sizeof(*hello) || hello->version!=1 || hello->protocol!=1 || hello->job_lo!=sw.request.lo
               || hello->job_hi!=sw.request.hi || hello->max_frame_bytes!=SHW_FRAME_BYTES || hello->flags)status=SC32_E_INVALID;
            else{ScShellWireHello reply=*hello;
                reply.features=255;
                sw.hello=1;
                status=sw_queue(SHW_HELLO_ACK,&reply,sizeof(reply),SHW_REPLY,0);
            }
        }
    }else if(!sw.hello)status=SC32_E_INVALID;
    else if(header->opcode==SHW_JOB_PLAN){
        if(sw.input_complete || sw.committed || header->request_lo!=1 || header->request_hi || header->offset!=sw.input_bytes
           || !header->body_bytes || header->total_bytes<header->offset || header->body_bytes>header->total_bytes-header->offset
           || (sw.input_expected && header->total_bytes!=sw.input_expected))status=SC32_E_INVALID;
        else{
            sw.input_expected=header->total_bytes;
            if(!sw.incoming)status=ScBufferCreate(0,&sw.incoming);
            if(!status)status=ScBufferAppend(sw.incoming,body,header->body_bytes);
            if(!status){sw.input_bytes+=header->body_bytes;
                if((header->flags&SHW_MORE)?sw.input_bytes>=sw.input_expected:sw.input_bytes!=sw.input_expected)status=SC32_E_INVALID;
                else if(!(header->flags&SHW_MORE)){ScView view;
                    sw_record(&view,sizeof(view));
                    status=ScBufferView(sw.incoming,&view);
                    if(!status)status=sw_parse_plan((const unsigned char *)view.data,view.bytes);
                    if(!status){sw.last_request_lo=1;
                        sw.last_request_hi=0;
                        ScBufferRelease(sw.incoming);
                        sw.incoming=0;
                    }}
            }
        }
    }else if(header->flags || header->offset || header->total_bytes!=header->body_bytes || !sw_sequence_next(header))status=SC32_E_STALE;
    else{
        sw.last_request_lo=header->request_lo;
        sw.last_request_hi=header->request_hi;
        status=sw_control_frame(header,body);
        if(status==SC32_E_STALE || status==SC32_E_INVALID || status==SC32_E_RANGE){
            ScStatus refusal=status;
            ScShellWireJobProgress progress;
            sw_progress(&progress,sw.active);
            progress.status=refusal;
            status=sw_queue(SHW_JOB_PROGRESS,&progress,sizeof(progress),SHW_REPLY,refusal);
            /* client在发送resolve后清本地conflict借用；拒绝继续时重发
             * 当前询问，不能只回error把双方卡在无可处理的CONFLICT。 */
            if(!status && sw.active && sw.active->phase==SW_CONFLICT)status=sw_emit_conflict(sw.active);
        }
    }
    free(frame);
    return status;
}
