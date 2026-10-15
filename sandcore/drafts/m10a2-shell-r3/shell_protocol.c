static ScStatus sh_frame_send(ScIpcChannel channel,const ScShellWireHeader *base,const ScBuffer *body,ScU32 *offset,unsigned char *scratch) {
    ScShellWireHeader h=*base;
    const unsigned char *data=0;
    ScU32 n=0,chunk;
    ScStatus s;
    if(body) {
        s=sh_buffer_data(body,&data,&n);
        if(s<0)return s;
    }
    if(*offset>n)return SC_SHELL_E_PROTOCOL;
    chunk=n-*offset;
    if(chunk>SHW_FRAME_BYTES-sizeof(h))chunk=SHW_FRAME_BYTES-sizeof(h);
    h.size=sizeof(h);
    h.version=1;
    h.body_bytes=chunk;
    h.offset=*offset;
    h.total_bytes=n;
    h.reserved=0;
    h.flags&=~SHW_MORE;
    if(chunk<n-*offset)h.flags|=SHW_MORE;
    memcpy(scratch,&h,sizeof(h));
    if(chunk)memcpy(scratch+sizeof(h),data+*offset,chunk);
    s=ScIpcSend(channel,SC32_IPC_FRAME_DATA,scratch,sizeof(h)+chunk);
    if(s==SC32_E_AGAIN)return SC_SHELL_PENDING;
    if(s<0)return sh_cause(s,30,0);
    *offset+=chunk;
    return (*offset==n)?0:SC_SHELL_PENDING;
}
static ScStatus sh_frame_receive(ScIpcChannel channel,const ScBrokerIdentity *peer,unsigned char *scratch,ScShellWireHeader *out,const unsigned char **body) {
    ScIpcPacket packet;
    ScShellWireHeader h;
    ScStatus s;
    memset(&packet,0,sizeof(packet));
    packet.size=sizeof(packet);
    packet.version=1;
    s=ScIpcPeek(channel,&packet);
    if(s==SC32_E_AGAIN)return SC_SHELL_PENDING;
    if(s<0)return sh_cause(s,31,0);
    if(packet.type==SC32_IPC_FRAME_EOF)return SC_SHELL_END;
    if(packet.type!=SC32_IPC_FRAME_DATA||packet.bytes<sizeof(h)||packet.bytes>SHW_FRAME_BYTES||!sh_same_identity(&packet.sender,peer))return SC_SHELL_E_PROTOCOL;
    s=ScIpcReceive(channel,scratch,SHW_FRAME_BYTES,&packet);
    if(s<0)return sh_cause(s,31,0);
    if(!sh_same_identity(&packet.sender,peer)||packet.type!=SC32_IPC_FRAME_DATA)return SC_SHELL_E_PROTOCOL;
    memcpy(&h,scratch,sizeof(h));
    if(h.size!=sizeof(h)||h.version!=1||h.reserved||(h.flags&~7u)||h.body_bytes!=packet.bytes-sizeof(h)|| h.offset>h.total_bytes||h.body_bytes>h.total_bytes-h.offset|| ((h.flags&SHW_MORE)?h.body_bytes==h.total_bytes-h.offset:h.body_bytes!=h.total_bytes-h.offset))return SC_SHELL_E_PROTOCOL;
    if(!h.body_bytes&&h.total_bytes)return SC_SHELL_E_PROTOCOL;
    *out=h;
    *body=scratch+sizeof(h);
    return 0;
}
static ScStatus sh_collect(ScBuffer **buffer,const ScShellWireHeader *h,const unsigned char *body,ScU32 budget,ScU32 *complete) {
    const unsigned char *p;
    ScU32 n=0;
    ScStatus s;
    *complete=0;
    if(h->total_bytes>budget)return SC_SHELL_E_RANGE;
    if(!*buffer) {
        if(h->offset)return SC_SHELL_E_PROTOCOL;
        s=ScBufferCreate(h->total_bytes,buffer);
        if(s<0)return sh_cause(s,32,0);
    }
    s=sh_buffer_data(*buffer,&p,&n);
    if(s<0)return s;
    if(h->offset!=n)return SC_SHELL_E_PROTOCOL;
    s=ScBufferAppend(*buffer,body,h->body_bytes);
    if(s<0)return sh_cause(s,32,0);
    *complete=!(h->flags&SHW_MORE);
    return 0;
}
static ScU32 sh_frame_bytes(const ScBuffer *body,ScU32 offset) {
    const unsigned char *p;
    ScU32 n=0;
    if(body&&sh_buffer_data(body,&p,&n)<0)return SHW_HEADER_BYTES;
    if(offset>n)return SHW_HEADER_BYTES;
    n-=offset;
    if(n>SHW_FRAME_BYTES-SHW_HEADER_BYTES)n=SHW_FRAME_BYTES-SHW_HEADER_BYTES;
    return SHW_HEADER_BYTES+n;
}
static int sh_io_ready(ScU32 handle,const ScBuffer *outgoing,ScU32 offset,ScU32 listener) {
    ScBrokerReadyInfo ready;
    ScStatus s;
    if(!handle)return 0;
    memset(&ready,0,sizeof(ready));
    ready.size=sizeof(ready);
    ready.version=1;
    s=ScBrokerReady(handle,&ready);
    if(s<0)return 1;
    if(ready.flags&(listener?SC32_BROKER_READY_ACCEPT|SC32_BROKER_READY_CLOSED:SC32_BROKER_READY_READ|SC32_BROKER_READY_CLOSED))return 1;
    return outgoing&&(ready.flags&SC32_BROKER_READY_WRITE)&&ready.write_bytes>=sh_frame_bytes(outgoing,offset);
}
static ScStatus sh_wait_min(ScU32 handle,ScU32 flags,ScU32 minimum,ScU32 timeout) {
    ScBrokerWaitDesc d;
    ScU32 events;
    ScStatus s;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.handle=handle;
    d.ready_flags=flags;
    d.minimum_write_bytes=minimum;
    d.event_mask=SC32_BROKER_EVENT;
    d.timeout=timeout;
    s=ScBrokerWait(&d,&events);
    if(s==SC32_TIMEOUT||s==SC32_PENDING)return SC_SHELL_PENDING;
    return sh_cause(s,33,0);
}
static ScStatus sh_wait(ScU32 handle,ScU32 flags,ScU32 timeout) {
    return sh_wait_min(handle,flags,1,timeout);
}
static ScStatus sh_event(ShObject *owner,ScShellEventProc proc,void *context,ScU32 type,ScU32 epoch,ScShellRequest *request,ScShellFileJob *job,ScStatus status,const void *data,ScU32 bytes,ScU32 item) {
    ScShellEvent e;
    ScStatus s;
    if(sh_quiet||!proc)return 0;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    e.type=type;
    e.epoch=epoch;
    e.request=request;
    e.job=job;
    e.status=status;
    e.data=data;
    e.bytes=bytes;
    e.item=item;
    sh_enter(owner);
    s=proc(&e,context);
    if(s<0)owner->notification_error=s;
    sh_leave(owner);
    return 0;
}
