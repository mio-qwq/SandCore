typedef struct {
    unsigned char magic[8];
    ScU32 size,version,job_lo,job_hi,pid,generation;
    ScI32 uid,gid;
    ScU32 realm,session,flags,reserved[2],crc;
} SwJournalHeader;
typedef struct {ScU32 bytes,version,type,sequence,body_bytes,crc;
} SwJournalFrame;
typedef char sw_journal_header64[(sizeof(SwJournalHeader)==64)?1:-1];
typedef char sw_journal_frame24[(sizeof(SwJournalFrame)==24)?1:-1];
enum {SW_JOURNAL_START=0x100,SW_JOURNAL_FINISH=0x101,SW_JOURNAL_REMOVE_INTENT=0x102};
static ScU32 sw_crc(ScU32 prior,const void *data,ScU32 bytes)
{
    ScU32 value=prior^0xffffffffu;
    const unsigned char *p=(const unsigned char *)data;
    for(ScU32 i=0;i<bytes;i++){
        value^=p[i];
        for(ScU32 bit=0;bit<8;bit++)value=(value>>1)^((0u-(value&1u))&0xedb88320u);
    }
    return value^0xffffffffu;
}
static ScStatus sw_journal_flush(void)
{
    if(sw.recovery_failed)return SC32_E_IO;
    ScView view;
    sw_record(&view,sizeof(view));
    ScStatus status=ScBufferView(sw.journal,&view);
    if(status)return status;
    ScU32 offset=0;
    while(offset<view.bytes){
        ScU32 bytes=view.bytes-offset;
        if(bytes>SW_IO_BYTES)bytes=SW_IO_BYTES;
        ScIoResult result;
        sw_record(&result,sizeof(result));
        result.transferred=0xffffffffu;
        status=ScFileWrite(sw.recovery_lease.file,(const unsigned char *)view.data+offset,bytes,&result);
        ScU32 accepted=result.transferred<=bytes?result.transferred:0;
        offset+=accepted;
        if(status || !accepted || (result.flags&(SC32_FILE_STATE_FAULTED|SC32_FILE_STATE_UNCERTAIN))){
            sw.recovery_failed=1;
            return status?status:SC32_E_IO;
        }
        if(offset<view.bytes)ScProcessYield();
    }
    status=ScFileFlush(sw.recovery_lease.file);
    if(status){sw.recovery_failed=1;
        return status;
    }
    status=ScBufferClear(sw.journal);
    if(status){sw.recovery_failed=1;
        return status;
    }
    sw.record_count=0;
    while(sw.journal_head){
        SwItem *item=sw.journal_head;
        sw.journal_head=item->next_journal;
        item->next_journal=0;
        item->journal_pending=0;
        item->recovery_written=1;
    }
    sw.journal_tail=0;
    return 0;
}
static ScStatus sw_journal_record(SwItem *item,ScU32 stage,int durable)
{
    if(!sw.recovery_ready || sw.recovery_failed)return SC32_E_IO;
    if(sw.recovery_sequence==0xffffffffu || (item && item->journal_pending))return SC32_E_RANGE;
    ScShellWireJobAck body;
    sw_ack(item,&body,0);
    /* 磁盘record的DURABLE指此前FS提交已确认；新record自身的Flush
     * 只能在调用返回成功后确认，故不预写RECOVERY_WRITTEN自证。 */
    body.flags&=~SHW_RECOVERY_WRITTEN;
    if(item && item->durable)body.flags|=SHW_DURABLE;
    SwJournalFrame frame;
    memset(&frame,0,sizeof(frame));
    frame.bytes=sizeof(frame)+sizeof(body);
    frame.version=1;
    frame.type=stage;
    frame.sequence=sw.recovery_sequence+1;
    frame.body_bytes=sizeof(body);
    frame.crc=sw_crc(sw_crc(0,&frame,20),&body,sizeof(body));
    ScU32 original=0;
    ScContainerInfo query;
    sw_record(&query,sizeof(query));
    ScStatus status=ScBufferQuery(sw.journal,&query);
    if(status)return status;
    original=query.length;
    if(original>SW_IO_BYTES-frame.bytes){status=sw_journal_flush();
        if(status)return status;
        original=0;
    }
    status=ScBufferReserve(sw.journal,original+frame.bytes);
    if(status)return status;
    status=ScBufferAppend(sw.journal,&frame,sizeof(frame));
    if(!status)status=ScBufferAppend(sw.journal,&body,sizeof(body));
    if(status){ScBufferResize(sw.journal,original);
        return status;
    }
    sw.recovery_sequence++;
    sw.record_count++;
    if(item){
        item->recovery_written=0;
        item->journal_pending=1;
        item->next_journal=0;
        if(sw.journal_tail)sw.journal_tail->next_journal=item;
        else sw.journal_head=item;
        sw.journal_tail=item;
    }
    if(durable || sw.record_count>=64)return sw_journal_flush();
    return 0;
}
static ScStatus sw_journal_create(void)
{
    ScStatus status;
    if(sw.recovery_ready)return 0;
    if(sw.recovery_phase==0){
        status=ScBufferCreate(SW_IO_BYTES,&sw.journal);
        if(status)return status;
        status=sw_lease_open(&sw.recovery_lease,sw.recovery_path,1,SC32_LEASE_WRITE_EXCLUSIVE,0,
            SC32_FILE_READ|SC32_FILE_WRITE,0,1);
        if(status)return status;
        sw.recovery_phase=1;
        return SC32_PENDING;
    }
    if(sw.recovery_phase==1){
        SwJournalHeader header;
        memset(&header,0,sizeof(header));
        memcpy(header.magic,"SCSFJ1\0",7);
        header.size=sizeof(header);
        header.version=1;
        header.job_lo=sw.request.lo;
        header.job_hi=sw.request.hi;
        header.pid=sw.self.pid;
        header.generation=sw.self.generation;
        header.uid=sw.identity.uid;
        header.gid=sw.identity.gid;
        header.realm=sw.identity.realm;
        header.session=sw.identity.session;
        header.crc=sw_crc(0,&header,60);
        status=ScBufferAppend(sw.journal,&header,sizeof(header));
        if(status)return status;
        status=sw_journal_flush();
        if(status)return status;
        sw.recovery_phase=2;
        return SC32_PENDING;
    }
    if(sw.recovery_phase==2){
        status=sw_lease_query(&sw.recovery_lease);
        if(status)return status;
        status=sw_lease_publish(&sw.recovery_lease,0,0,sw.recovery_path,3,0);
        if(status!=SC32_PENDING)return status?status:SC32_E_FAILED;
        sw.recovery_phase=3;
        return SC32_PENDING;
    }
    status=sw_lease_advance(&sw.recovery_lease);
    if(status==SC32_PENDING)return status;
    if(status || sw.recovery_lease.info.state!=SC32_LEASE_COMMITTED
       || !(sw.recovery_lease.info.flags&SC32_LEASE_FLAG_DURABLE)
       || (sw.recovery_lease.info.flags&SC32_LEASE_FLAG_UNCERTAIN))return status?status:SC32_E_IO;
    sw.recovery_ready=1;
    sw.recovery_phase=4;
    return sw_journal_record(0,SW_JOURNAL_START,1);
}
