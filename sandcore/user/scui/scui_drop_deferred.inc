/* v2只延迟业务完成。短寿命WM票在确认框之前消费，不能拿原鼠标落点
 * 当数分钟后的文件授权；后半程只认同一认证channel/nonce/owner和真实job结果。 */
enum {UI_D2_HOLD=7,UI_D2_READY=8,UI_D2_FINAL=9};
static UiDropPeer *ui_drop_receipts,*ui_drop_current_peer;
static const ScUiEvent *ui_drop_current_event;
static ScU32 ui_drop_receipt_serial=1;
static ScStatus ui_drop_send(ScIpcChannel,const ScDragEnvelope *,const void *,ScU32);
static ScStatus ui_drop_receive(ScIpcChannel,unsigned char **,ScIpcPacket *);
static int ui_drop_same_round(const ScDragEnvelope *,const ScDragEnvelope *);
static int ui_drop_identity(const ScBrokerIdentity *,const ScBrokerIdentity *);
static ScStatus ui_d2_receive(ScIpcChannel channel,unsigned char **out,ScIpcPacket *packet)
{
    memset(packet,0,sizeof(*packet));packet->size=sizeof(*packet);packet->version=1;
    ScStatus s=ScIpcPeek(channel,packet);
    if(s==SC32_E_AGAIN){
        ScIpcInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
        ScStatus query=ScIpcQuery(channel,&info);if(query<0)return query;
        if(info.peer_closed || info.state==SC32_IPC_STATE_CLOSED)return SC32_E_BROKEN_PIPE;
    }
    if(s<0)return s;
    /* 先领完已排队DATA再认EOF，关闭对端不能吞掉它刚送出的真实终态。 */
    if(packet->flags&SC32_IPC_FRAME_EOF){ScIpcReceive(channel,0,0,packet);return SC32_E_BROKEN_PIPE;}
    return ui_drop_receive(channel,out,packet);
}

static void ui_d2_result(ScUiDropResult *r,ScStatus status)
{memset(r,0,sizeof(*r));r->size=sizeof(*r);r->version=1;r->status=status;}
static UiDropPeer *ui_d2_receipt(ScUiContext *c,const ScUiDropReceipt *r)
{
    if(!r || r->size<sizeof(*r) || r->version!=1 || !r->id || r->generation!=r->id)return 0;
    for(UiDropPeer *p=ui_drop_receipts;p;p=p->receipt_next)
        if(p->root->context==c && p->receipt_id==r->id)return p;
    return 0;
}
static void ui_d2_fail(UiDropPeer *p,ScStatus status)
{
    if(!p->receipt_id || p->receipt_state==SC_UI_DROP_SENT)return;
    p->receipt_state=SC_UI_DROP_FAILED;p->receipt_status=status<0?status:SC_UI_E_CANCELLED;
    /* receipt仍可查询错误，但失败的传输不能因应用晚Release而吊住对端。 */
    if(p->channel){ScIpcClose(p->channel);p->channel=0;}
}
static ScStatus ui_d2_check_result(const ScUiDropResult *r,ScU32 actions)
{
    ScStatus s=ui_input(r,sizeof(*r));if(s<0)return s;
    if(r->size!=sizeof(*r) || r->reserved[0] || r->reserved[1] || r->flags&~3u ||
        (r->action!=0 && r->action!=SC_UI_DRAG_COPY && r->action!=SC_UI_DRAG_MOVE) ||
        (r->action && !(r->action&actions)) || r->items_durable>r->items_total ||
        r->source_changed>r->items_durable || (r->source_changed!=0)!=((r->flags&SC_UI_DROP_SOURCE_CHANGED)!=0) ||
        (r->source_changed && r->action!=SC_UI_DRAG_MOVE))return SC_UI_E_INVALID;
    if(r->flags&SC_UI_DROP_COMMITTED){
        if(r->status<0 || !r->action || !r->items_total || r->items_durable!=r->items_total ||
            r->retained || r->failed || r->skipped || r->uncertain ||
            (r->action==SC_UI_DRAG_MOVE && !r->source_changed))return SC_UI_E_INVALID;
    }else if(r->status>=0)return SC_UI_E_INVALID;
    return 0;
}
ScStatus ScUiDropDefer(ScUiContext *c,const ScUiEvent *e,ScUiDropReceipt *out)
{
    ScStatus s=ui_output(out,sizeof(*out));if(s<0)return s;
    UiDropPeer *p=ui_drop_current_peer;
    if(!p || e!=ui_drop_current_event || p->root->context!=c || !e || e->type!=SC_UI_EVENT_DROP ||
        e->bytes!=sizeof(ScUiDragData) || ((const ScUiDragData *)e->data)->phase!=SC32_DRAG_TRANSFER)return SC_UI_E_INVALID;
    if(p->envelope.version!=2)return SC_UI_E_NOT_SUPPORTED;
    if(p->receipt_id)return SC_UI_E_BUSY;
    if(ui_drop_receipt_serial==UINT_MAX)return SC_UI_E_RANGE;
    s=ui_object_retain(&p->object,UI_DROP_PEER_OBJECT);if(s<0)return s;
    s=ScUiContextRetain(c);if(s<0){ui_object_release(&p->object);return s;}
    p->receipt_id=ui_drop_receipt_serial++;
    p->receipt_state=SC_UI_DROP_HANDSHAKE;p->receipt_status=0;p->deferred=1;
    p->allowed_actions=((const ScUiDragData *)e->data)->actions;
    p->receipt_next=ui_drop_receipts;ui_drop_receipts=p;
    ui_d2_result(&p->receipt_result,SC_UI_E_CANCELLED);
    ScUiDropReceipt value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
    value.id=value.generation=p->receipt_id;memcpy(out,&value,sizeof(value));return 0;
}
ScStatus ScUiDropQuery(ScUiContext *c,const ScUiDropReceipt *receipt,ScUiDropReceiptInfo *out)
{
    ScStatus s=ui_output(out,sizeof(*out));if(s<0)return s;
    UiDropPeer *p=ui_d2_receipt(c,receipt);if(!p)return SC_UI_E_STALE;
    ScUiDropReceiptInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    info.state=p->receipt_state;info.status=p->receipt_status;
    info.transfer.size=sizeof(info.transfer);info.transfer.version=1;info.transfer.phase=SC32_DRAG_TRANSFER;
    info.transfer.format=p->format;info.transfer.actions=p->allowed_actions;info.transfer.chosen_action=p->action;
    info.transfer.bytes=p->total;info.transfer.data=p->body;info.transfer.sender=p->sender;
    info.result=p->receipt_result;memcpy(out,&info,sizeof(info));return 0;
}
ScStatus ScUiDropReply(ScUiContext *c,const ScUiDropReceipt *receipt,const ScUiDropResult *result)
{
    UiDropPeer *p=ui_d2_receipt(c,receipt);if(!p)return SC_UI_E_STALE;
    ScStatus s=ui_d2_check_result(result,p->allowed_actions);if(s<0)return s;
    if(p->receipt_state==SC_UI_DROP_FAILED)return p->receipt_status;
    if(p->receipt_state==SC_UI_DROP_HANDSHAKE)return SC_UI_E_BUSY;
    if(p->receipt_state==SC_UI_DROP_READY){p->receipt_result=*result;p->receipt_state=SC_UI_DROP_REPLYING;return SC_UI_PENDING;}
    if(memcmp(&p->receipt_result,result,sizeof(*result)))return SC_UI_E_BUSY;
    return p->receipt_state==SC_UI_DROP_SENT?0:SC_UI_PENDING;
}
ScStatus ScUiDropRelease(ScUiContext *c,ScUiDropReceipt *receipt)
{
    UiDropPeer *p=ui_d2_receipt(c,receipt);if(!p)return SC_UI_E_STALE;
    UiDropPeer **slot=&ui_drop_receipts;while(*slot && *slot!=p)slot=&(*slot)->receipt_next;
    if(*slot)*slot=p->receipt_next;
    p->receipt_next=0;p->receipt_id=0;
    /* 未发送业务终态时撤channel；对端收到真实EOF，只能判取消/不明，不能判成功。 */
    if(p->receipt_state!=SC_UI_DROP_SENT){if(p->channel)ScIpcClose(p->channel);p->channel=0;}
    receipt->id=receipt->generation=0;ui_object_release(&p->object);ScUiContextRelease(c);return 0;
}
static int ui_d2_peer_retired(UiDropPeer *p)
{
    UiNode *node=ui_node(p->root->context,p->node);
    return !p->root->owner_live || !node || node->generation!=p->node_generation;
}
static ScStatus ui_d2_peer_step(UiDropPeer *p)
{
    if(p->receipt_state==SC_UI_DROP_FAILED || !p->receipt_id)return SC_UI_END;
    if(ui_d2_peer_retired(p)){ui_d2_fail(p,SC_UI_E_CANCELLED);return SC_UI_END;}
    if(p->deferred==1){
        ScWideCounter now;ScStatus s=ui_now(&now);if(s<0)return s;
        if(ui_wide_compare(&now,&p->deadline)>=0)return SC_UI_E_CANCELLED;
        ScDragEnvelope reply=p->envelope;reply.type=UI_D2_HOLD;reply.format=p->format;reply.chosen_action=p->action;reply.status=0;reply.allowed_actions=p->allowed_actions;
        s=ui_drop_send(p->channel,&reply,0,0);if(s==0)p->deferred=2;return s;
    }
    if(p->receipt_state==SC_UI_DROP_REPLYING){
        ScDragEnvelope reply=p->envelope;reply.type=UI_D2_FINAL;reply.format=p->format;
        reply.chosen_action=p->receipt_result.action;reply.status=0;reply.allowed_actions=p->allowed_actions;
        ScStatus s=ui_drop_send(p->channel,&reply,&p->receipt_result,sizeof(p->receipt_result));
        if(s==0){p->receipt_state=SC_UI_DROP_SENT;return SC_UI_END;}return s;
    }
    unsigned char *frame=0;ScIpcPacket packet;memset(&packet,0,sizeof(packet));packet.size=sizeof(packet);packet.version=1;
    if(p->deferred==2){ScWideCounter now;ScStatus clock=ui_now(&now);if(clock<0)return clock;
        if(ui_wide_compare(&now,&p->deadline)>=0)return SC_UI_E_CANCELLED;}
    ScStatus s=ui_d2_receive(p->channel,&frame,&packet);if(s<0)return s;
    const ScDragEnvelope *e=(const ScDragEnvelope *)frame;
    if(!ui_drop_identity(&packet.sender,&p->sender) || !ui_drop_same_round(e,&p->envelope) || e->version!=2 || e->format!=p->format || e->body_bytes){free(frame);return SC_UI_E_INVALID;}
    if(e->type==SC32_DRAG_CANCEL || e->type==SC32_DRAG_REJECT){free(frame);return SC_UI_E_CANCELLED;}
    if(p->deferred!=2 || e->type!=UI_D2_READY || e->status<0 || e->chosen_action!=p->action || e->allowed_actions!=p->allowed_actions){free(frame);return SC_UI_E_INVALID;}
    p->deferred=3;p->receipt_state=SC_UI_DROP_READY;free(frame);return 0;
}
static ScStatus ui_d2_notice(UiDrag *d,ScU32 phase,const ScUiDropResult *result)
{
    UiNode *source=ui_node(d->context,d->source);if(!source)return 0;
    ScUiDragData data;memset(&data,0,sizeof(data));data.size=sizeof(data);data.version=1;
    data.phase=phase;data.format=d->format;data.actions=d->actions;
    data.chosen_action=result?result->action:d->action;data.sender=d->target.identity;
    if(result){data.data=result;data.bytes=sizeof(*result);}
    ScUiEvent event;ui_event_record(&event,source,SC_UI_EVENT_DROP);
    event.flags=SC_UI_EVENT_PROGRAMMATIC;event.command=d->token;event.data=&data;event.bytes=sizeof(data);
    /* 前一轮业务结果可晚于下一次物理拖动，token不能借节点command或全局bool猜。 */
    return ui_emit(d->context,source,&event);
}
static void ui_d2_detach_pointer(UiDrag *d)
{
    if(d->captured){d->captured=0;ScUiCapture(d->context,d->source,0);}
    if(d->preview){ScUiNodeDestroy(d->context,d->preview);d->preview=0;}
    ScPointerSetCursor(d->window,SC32_CURSOR_ARROW);
    free(d->offer);d->offer=0;
}
static int ui_d2_source_retired(UiDrag *d)
{
    UiNode *source=ui_node(d->context,d->source),*root=ui_node(d->context,d->root);
    return !source || !root || (source->private_flags&UI_NODE_RETIRED) || (root->private_flags&UI_NODE_RETIRED);
}
static ScStatus ui_d2_source_step(UiDrag *d)
{
    /* 退役先从节点表移除，clear可等活跃回调；不让闲置channel挡住终止。 */
    if(ui_d2_source_retired(d))return SC_UI_E_CANCELLED;
    if(d->deferred_stage==1){
        ScWideCounter now;ScStatus clock=ui_now(&now);if(clock<0)return clock;
        if(ui_wide_compare(&now,&d->deadline)>=0)return SC_UI_E_CANCELLED;
        ScDragEnvelope reply=d->last;reply.type=UI_D2_READY;reply.format=d->format;reply.chosen_action=d->action;reply.status=0;reply.allowed_actions=d->deferred_actions;
        ScStatus s=ui_drop_send(d->channel,&reply,0,0);if(s==0)d->deferred_stage=2;return s;
    }
    unsigned char *frame=0;ScIpcPacket packet;memset(&packet,0,sizeof(packet));packet.size=sizeof(packet);packet.version=1;
    ScStatus s=ui_d2_receive(d->channel,&frame,&packet);if(s<0)return s;
    const ScDragEnvelope *e=(const ScDragEnvelope *)frame;
    if(!ui_drop_identity(&packet.sender,&d->target.identity) || !ui_drop_same_round(e,&d->last) || e->version!=2 || e->format!=d->format ||
        e->type!=UI_D2_FINAL || e->status<0 || e->body_bytes!=sizeof(ScUiDropResult) || e->allowed_actions!=d->deferred_actions){free(frame);return SC_UI_E_INVALID;}
    ScUiDropResult result;memcpy(&result,frame+sizeof(*e),sizeof(result));
    s=ui_d2_check_result(&result,d->deferred_actions);
    if(s==0 && e->chosen_action!=result.action)s=SC_UI_E_INVALID;
    free(frame);if(s<0)return s;
    d->deferred_notified=1;s=ui_d2_notice(d,SC32_DRAG_COMPLETE,&result);
    if(s<0)d->context->notification_error=s;
    return SC_UI_END;
}
static int ui_d2_expired(const ScWideCounter *deadline)
{ScWideCounter now;return ui_now(&now)<0 || ui_wide_compare(&now,deadline)>=0;}
static int ui_d2_channel_work(ScIpcChannel channel,ScU32 write_bytes)
{
    ScBrokerReadyInfo ready;memset(&ready,0,sizeof(ready));ready.size=sizeof(ready);ready.version=1;
    if(ScBrokerReady(channel,&ready)<0)return 1;
    if(ready.flags&(SC32_BROKER_READY_READ|SC32_BROKER_READY_CLOSED))return 1;
    return write_bytes && (ready.flags&SC32_BROKER_READY_WRITE) && ready.write_bytes>=write_bytes;
}
static int ui_d2_peer_work(UiDropPeer *p)
{
    if(!p->deferred)return 1;
    if(!p->receipt_id || p->receipt_state==SC_UI_DROP_FAILED)return 1;
    /* 目标退役本身就是清理工作；READY后的空channel不能挡住peer_step的撤销。 */
    if(ui_d2_peer_retired(p))return 1;
    /* 超时本身也是待处理事件；不能只把Wait压到0却一直跳过真正清理。 */
    if(p->deferred<3 && ui_d2_expired(&p->deadline))return 1;
    return ui_d2_channel_work(p->channel,p->deferred==1?sizeof(ScDragEnvelope):
        p->receipt_state==SC_UI_DROP_REPLYING?sizeof(ScDragEnvelope)+sizeof(ScUiDropResult):0);
}
static int ui_d2_source_work(UiDrag *d)
{
    /* 取消与节点退役本身需要清理，不得先用无数据channel把它们过滤掉。
     * 基础引用仍由原close释放；这里不直接置cancelled，避免跳过那次release。 */
    if(d->cancelled)return 1;
    if(d->deferred_stage && ui_d2_source_retired(d))return 1;
    return !d->deferred_stage || (d->deferred_stage==1 && ui_d2_expired(&d->deadline)) || ui_d2_channel_work(d->channel,d->deferred_stage==1?sizeof(ScDragEnvelope):0);
}
static ScU32 ui_d2_timeout(const ScWideCounter *deadline,ScU32 timeout)
{
    ScWideCounter now;if(ui_now(&now)<0 || ui_wide_compare(&now,deadline)>=0)return 0;
    ScU32 lo=deadline->lo-now.lo,hi=deadline->hi-now.hi-(deadline->lo<now.lo);
    return !hi&&lo<timeout?lo:timeout;
}
