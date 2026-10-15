#include "SC32API.H"
#include "SC32BROKER.H"
#include "SC32FONT.H"
#include "SC32PALETTE.H"

/* 普通应用只用正式cdecl。父与孩子是两个真实SCX进程，control与data
 * 分开，才不会让孩子提前消费而掩盖小队列的真实背压。 */
enum {
    PROBE_LARGE_BYTES=131079,PROBE_SHARED_BYTES=73728,
    PROBE_SHARED_WORDS=18432,PROBE_WAIT_TICKS=500,
    FRAME_HELLO=1,FRAME_GRANTS=2,FRAME_REVOKE=3,FRAME_DRAIN=4,
    FRAME_ACK=5,FRAME_FINISH=6,FRAME_DONE=7,FRAME_FILL=8,FRAME_ZERO=9,
    MARK_INITIAL=0x12345678u,MARK_PARENT=0xa55a1901u,MARK_CHILD=0xb77b1902u,
    FENCE=0x319ac75du
};
typedef struct {ScU32 size,version,kind,a,b,c,d,e;} ProbeFrame;
typedef unsigned char ProbeByte;

static ScFile output;
static ScProcessRef self;
static ScIpcEndpoint control_endpoint,data_endpoint;
static ScIpcChannel control_channel,data_channel;
static ScClipboardCandidate candidate;
static ScClipboardSnapshot seed_snapshot,large_snapshot;
static ScSharedMemory created_memory,readonly_memory,writable_memory;
static ScSharedGrant readonly_grant,writable_grant;
static ScSharedMapping creator_mapping,readonly_mapping,writable_mapping;
static ScJob child_job;
static ScU32 watched,current_epoch,checks;
static void *scratch;
static char failed_step[96];
static ScErrorInfo failed_error;

static ScU32 text_length(const char *text)
{
    ScU32 bytes=0;
    while(text[bytes])bytes++;
    return bytes;
}
static int text_same(const char *left,const char *right)
{
    while(*left && *left==*right){left++;right++;}
    return *left==*right;
}
static void bytes_copy(void *to,const void *from,ScU32 bytes)
{
    ProbeByte *dst=to;
    const ProbeByte *src=from;
    for(ScU32 n=0;n<bytes;n++)dst[n]=src[n];
}
static int bytes_same(const void *left,const void *right,ScU32 bytes)
{
    const ProbeByte *a=left,*b=right;
    for(ScU32 n=0;n<bytes;n++)if(a[n]!=b[n])return 0;
    return 1;
}
static void record(void *memory,ScU32 bytes)
{
    ProbeByte *p=memory;
    for(ScU32 n=0;n<bytes;n++)p[n]=0;
    ((ScU32 *)memory)[0]=bytes;
    ((ScU32 *)memory)[1]=1;
}
static void append_text(char *buffer,ScU32 capacity,const char *text)
{
    ScU32 used=text_length(buffer),n=0;
    while(text[n] && used+1u<capacity)buffer[used++]=text[n++];
    buffer[used]=0;
}
static void append_number(char *buffer,ScU32 capacity,ScU32 value)
{
    char reversed[10],text[11];
    ScU32 used=0,at=0;
    do {reversed[used++]=(char)('0'+value%10u);value/=10u;} while(value);
    while(used)text[at++]=reversed[--used];
    text[at]=0;
    append_text(buffer,capacity,text);
}
static int number_parse(const char *text,ScU32 *out)
{
    ScU32 value=0;
    if(!*text)return 0;
    while(*text){
        ScU32 digit=(ScU32)(ProbeByte)*text-'0';
        if(digit>9u || value>(0xffffffffu-digit)/10u)return 0;
        value=value*10u+digit;
        text++;
    }
    *out=value;
    return 1;
}
static char *next_argument(char **input)
{
    while(**input==' ')(*input)++;
    char *start=*input;
    while(**input && **input!=' ')(*input)++;
    if(**input)*(*input)++=0;
    return start;
}
static int say(const char *text)
{
    ScU32 bytes=text_length(text),offset=0,tries=0;
    while(offset<bytes && tries++<32u){
        ScIoResult result;
        record(&result,sizeof(result));
        ScStatus status=ScFileWrite(output,text+offset,bytes-offset,&result);
        if(result.transferred>bytes-offset)return 0;
        offset+=result.transferred;
        if(status==SC32_E_AGAIN){
            ScU32 events=0;
            if(ScSystemWait(SC32_EVENT_STREAM,100,&events)<0)return 0;
        }else if(status || !result.transferred)return 0;
    }
    return offset==bytes;
}
static int require(int truth,const char *step)
{
    checks++;
    if(truth)return 1;
    if(!failed_step[0]){
        append_text(failed_step,sizeof(failed_step),step);
        record(&failed_error,sizeof(failed_error));
        ScErrorQuery(&failed_error);
    }
    return 0;
}
static void report_failure(void)
{
    char line[256]="PLATFORM FAIL ";
    append_text(line,sizeof(line),failed_step[0]?failed_step:"initialization");
    append_text(line,sizeof(line)," code_bits=");
    append_number(line,sizeof(line),(ScU32)failed_error.code);
    append_text(line,sizeof(line)," domain=");
    append_number(line,sizeof(line),failed_error.domain);
    append_text(line,sizeof(line)," raw_bits=");
    append_number(line,sizeof(line),(ScU32)failed_error.raw);
    append_text(line,sizeof(line)," operation=");
    append_number(line,sizeof(line),failed_error.operation);
    append_text(line,sizeof(line),"\n");
    say(line);
}
static int tick_now(ScU32 *tick)
{
    ScMonotonicClock clock;
    record(&clock,sizeof(clock));
    if(ScSystemMonotonic(&clock))return 0;
    *tick=clock.ticks.lo;
    return 1;
}
static ScStatus wait_ready(ScU32 handle,ScU32 flags,ScU32 minimum,ScU32 timeout)
{
    ScU32 start=0;
    if(!tick_now(&start))return SC32_E_FAILED;
    for(ScU32 attempts=0;attempts<512u;attempts++){
        ScBrokerReadyInfo ready;
        record(&ready,sizeof(ready));
        ScStatus status=ScBrokerReady(handle,&ready);
        if(status)return status;
        ScU32 available=ready.flags&flags;
        if((available&SC32_BROKER_READY_WRITE) && ready.write_bytes<minimum)
            available&=~SC32_BROKER_READY_WRITE;
        if(available)return 0;
        ScU32 now=0;
        if(!tick_now(&now))return SC32_E_FAILED;
        ScU32 elapsed=now-start;
        if(elapsed>=timeout)return SC32_TIMEOUT;
        ScBrokerWaitDesc wait={sizeof(ScBrokerWaitDesc),1,handle,flags,minimum,0,timeout-elapsed,0};
        ScU32 events=0;
        status=ScBrokerWait(&wait,&events);
        if(status<0)return status;
        /* 唤醒只给事实，不是队列预约；其它channel或clip事件会再检查。 */
    }
    return SC32_E_FAILED;
}
static int send_frame(ScIpcChannel channel,ScU32 type,const ProbeFrame *frame)
{
    for(ScU32 tries=0;tries<32u;tries++){
        ScStatus status=ScIpcSend(channel,type,frame,frame?sizeof(*frame):0u);
        if(!status)return 1;
        if(status!=SC32_E_AGAIN || wait_ready(channel,SC32_BROKER_READY_WRITE,
            frame?sizeof(*frame):0u,PROBE_WAIT_TICKS))return 0;
    }
    return 0;
}
static int receive_frame(ScIpcChannel channel,ProbeFrame *frame,ScIpcPacket *packet)
{
    if(wait_ready(channel,SC32_BROKER_READY_READ,0,PROBE_WAIT_TICKS))return 0;
    record(packet,sizeof(*packet));
    return ScIpcReceive(channel,frame,sizeof(*frame),packet)==0;
}
static int sender_same(const ScIpcPacket *packet,const ScBrokerIdentity *peer)
{
    return packet->sender.size==sizeof(ScBrokerIdentity) && packet->sender.version==1
        && bytes_same(&packet->sender,peer,sizeof(*peer));
}
static int frame_valid(const ProbeFrame *frame,const ScIpcPacket *packet,ScU32 kind,
                       const ScBrokerIdentity *peer)
{
    return packet->flags==SC32_IPC_FRAME_DATA && packet->type==kind
        && packet->bytes==sizeof(*frame) && frame->size==sizeof(*frame)
        && frame->version==1 && frame->kind==kind && sender_same(packet,peer);
}
static void make_frame(ProbeFrame *frame,ScU32 kind)
{
    record(frame,sizeof(*frame));
    frame->kind=kind;
}
static ProbeByte large_byte(ScU32 at)
{
    if(at==PROBE_LARGE_BYTES-1u)return 0;
    if(at==65535u)return 0xf0;
    if(at==65536u)return 0x9f;
    if(at==65537u)return 0x99;
    if(at==65538u)return 0x82;
    return (ProbeByte)('a'+at%26u);
}

static int publish_seed(const char *application)
{
    static const char seed[]="seed \xe4\xb8\xad";
    ScClipboardDesc desc={sizeof(ScClipboardDesc),1,4096,0,{0,0,0,0}};
    if(!require(ScClipboardBegin(&desc,&candidate)==0,"seed begin"))return 0;
    if(!require(ScClipboardOffer(candidate,SC32_CLIPBOARD_TEXT,seed,sizeof(seed))==0,"seed text"))return 0;
    ScPathInfo metadata;
    record(&metadata,sizeof(metadata));
    if(!require(ScPathMetadata(application,&metadata)==0 && metadata.kind==SC32_PATH_FILE
        && metadata.generation!=0,"file intent real revision"))return 0;
    ProbeByte files[104];
    for(ScU32 n=0;n<sizeof(files);n++)files[n]=0;
    ScClipboardFiles *header=(ScClipboardFiles *)files;
    record(header,sizeof(*header));
    header->count=1;
    header->operation=SC32_CLIPBOARD_COPY;
    ScClipboardFileRow *row=(ScClipboardFileRow *)(files+sizeof(*header));
    row->path_bytes=text_length(application)+1u;
    row->record_bytes=(sizeof(*row)+row->path_bytes+3u)&~3u;
    row->revision=metadata.generation;
    bytes_copy(files+sizeof(*header)+sizeof(*row),application,row->path_bytes);
    if(!require(row->path_bytes<=65u && ScClipboardOffer(candidate,SC32_CLIPBOARD_FILES,
        files,sizeof(*header)+row->record_bytes)==0,"typed file intent"))return 0;
    ScClipboardInfo info;
    record(&info,sizeof(info));
    if(!require(ScClipboardCommit(candidate,0,&info)==0 && info.handle==0
        && info.formats==2 && info.epoch!=0,"seed commit"))return 0;
    candidate=0;
    current_epoch=info.epoch;
    record(&info,sizeof(info));
    if(!require(ScClipboardOpen(&info)==0 && info.epoch==current_epoch,"seed snapshot"))return 0;
    seed_snapshot=info.handle;
    ScClipboardFormatInfo formats[2];
    ScClipboardPage page;
    record(&page,sizeof(page));
    if(!require(ScClipboardFormats(seed_snapshot,SC32_BROKER_ENUM_BEGIN,formats,2,&page)==0
        && page.count==2 && (page.flags&SC32_CLIPBOARD_FINISHED)
        && formats[0].format==SC32_CLIPBOARD_TEXT && formats[1].format==SC32_CLIPBOARD_FILES,
        "immutable format page"))return 0;
    ScU32 epoch=0;
    if(!require(ScClipboardWatch(1,&epoch)==0 && epoch==current_epoch,"clipboard watch"))return 0;
    watched=1;
    return 1;
}
static int check_clipboard(ScU32 child_epoch,const ScBrokerIdentity *publisher,const char *application)
{
    if(!require(wait_ready(0,SC32_BROKER_READY_READ,0,PROBE_WAIT_TICKS)==0,"clipboard ready wait"))return 0;
    ScClipboardInfo info;
    record(&info,sizeof(info));
    if(!require(ScClipboardOpen(&info)==0 && info.epoch==child_epoch && info.formats==1
        && bytes_same(&info.publisher,publisher,sizeof(*publisher)),"real clipboard publisher"))return 0;
    large_snapshot=info.handle;
    ScU32 previous_epoch=current_epoch;
    current_epoch=child_epoch;
    static const char seed[]="seed \xe4\xb8\xad";
    char old[16];
    ScBrokerIoResult io;
    record(&io,sizeof(io));
    if(!require(ScClipboardRead(seed_snapshot,SC32_CLIPBOARD_TEXT,0,old,sizeof(old),&io)==0
        && io.transferred==sizeof(seed) && bytes_same(old,seed,sizeof(seed)),"old snapshot after replace"))return 0;
    ProbeByte files[104];
    record(&io,sizeof(io));
    if(!require(ScClipboardRead(seed_snapshot,SC32_CLIPBOARD_FILES,0,files,sizeof(files),&io)==0
        && io.transferred>=36u && io.transferred<=sizeof(files)
        && ((ScClipboardFiles *)files)->size==16 && ((ScClipboardFiles *)files)->count==1
        && ((ScClipboardFiles *)files)->operation==SC32_CLIPBOARD_COPY
        && ((ScClipboardFileRow *)(files+16))->object_id==0
        && ((ScClipboardFileRow *)(files+16))->flags==0
        && ((ScClipboardFileRow *)(files+16))->path_bytes==text_length(application)+1u
        && io.transferred>=36u+text_length(application)+1u
        && bytes_same(files+36,application,text_length(application)+1u),"file intent stays typed"))return 0;
    ScClipboardDesc desc={sizeof(ScClipboardDesc),1,4096,0,{0,0,0,0}};
    if(!require(ScClipboardBegin(&desc,&candidate)==0
        && ScClipboardOffer(candidate,SC32_CLIPBOARD_TEXT,"CAS",4)==0,"CAS candidate"))return 0;
    struct {ScClipboardInfo info;ScU32 tail[4];} wide,before;
    record(&wide,sizeof(wide));
    for(ScU32 n=0;n<4;n++)wide.tail[n]=FENCE;
    bytes_copy(&before,&wide,sizeof(before));
    if(!require(ScClipboardCommit(candidate,previous_epoch,&wide.info)==SC32_E_STALE
        && bytes_same(&wide,&before,sizeof(wide)),"stale CAS keeps output and candidate"))return 0;
    record(&info,sizeof(info));
    if(!require(ScClipboardQuery(&info)==0 && info.epoch==child_epoch
        && ScClipboardAbort(candidate)==0,"stale CAS keeps current"))return 0;
    candidate=0;
    return 1;
}
static int read_large_snapshot(void)
{
    for(ScU32 offset=0;offset<PROBE_LARGE_BYTES;){
        ScU32 bytes=PROBE_LARGE_BYTES-offset;
        if(bytes>SC32_BROKER_IO_MAX)bytes=SC32_BROKER_IO_MAX;
        ScBrokerIoResult io;
        record(&io,sizeof(io));
        if(!require(ScClipboardRead(large_snapshot,SC32_CLIPBOARD_TEXT,offset,scratch,bytes,&io)==0
            && io.transferred==bytes && io.total_bytes==PROBE_LARGE_BYTES,"large snapshot chunk"))return 0;
        for(ScU32 n=0;n<bytes;n++)if(((ProbeByte *)scratch)[n]!=large_byte(offset+n))
            return require(0,"large UTF8 bytes after source exit");
        if(!require((offset+bytes==PROBE_LARGE_BYTES)==((io.flags&SC32_BROKER_IO_END)!=0),
            "large snapshot end flag"))return 0;
        offset+=bytes;
    }
    return 1;
}

static int child_run(const char *name,const ScProcessRef *parent)
{
    char data_name[64]="";
    append_text(data_name,sizeof(data_name),name);
    append_text(data_name,sizeof(data_name),".data");
    ScIpcConnectDesc connect={sizeof(ScIpcConnectDesc),1,name,SC32_IPC_NAMESPACE_SESSION,0,0,{0,0}};
    if(!require(ScIpcConnect(&connect,&control_channel)==0,"child control connect"))return 0;
    connect.name=data_name;
    if(!require(ScIpcConnect(&connect,&data_channel)==0,"child data connect"))return 0;
    if(!require(wait_ready(control_channel,SC32_BROKER_READY_WRITE,sizeof(ProbeFrame),PROBE_WAIT_TICKS)==0,
        "child accepted wait"))return 0;
    ScBrokerIdentity peer;
    record(&peer,sizeof(peer));
    if(!require(ScIpcPeer(control_channel,&peer)==0 && peer.pid==parent->pid
        && peer.generation==parent->generation && peer.uid==1000
        && peer.realm==SC32_BROKER_REALM_NORMAL && !(peer.flags&SC32_BROKER_ID_HARDWARE_SYSTEM),
        "child actual ordinary parent"))return 0;
    if(!require(ScMemoryAllocatePages(PROBE_LARGE_BYTES,&scratch)==0,"large private source pages"))return 0;
    for(ScU32 n=0;n<PROBE_LARGE_BYTES;n++)((ProbeByte *)scratch)[n]=large_byte(n);
    ScClipboardDesc clipboard={sizeof(ScClipboardDesc),1,262144,0,{0,0,0,0}};
    if(!require(ScClipboardBegin(&clipboard,&candidate)==0
        && ScClipboardOffer(candidate,SC32_CLIPBOARD_TEXT,scratch,PROBE_LARGE_BYTES)==0,
        "large staged UTF8 offer"))return 0;
    static const ProbeByte invalid_utf8[3]={0xc0,0x80,0};
    if(!require(ScClipboardOffer(candidate,SC32_CLIPBOARD_TEXT,invalid_utf8,sizeof(invalid_utf8))==SC32_E_INVALID,
        "invalid offer leaves valid format"))return 0;
    ScClipboardInfo clip;
    record(&clip,sizeof(clip));
    if(!require(ScClipboardCommit(candidate,0,&clip)==0 && clip.formats==1,"child atomic clip commit"))return 0;
    candidate=0;
    if(!require(ScMemoryFreePages(scratch)==0,"large source free after commit"))return 0;
    scratch=0;
    ProbeFrame frame;
    make_frame(&frame,FRAME_HELLO);
    frame.a=parent->pid; /* 故意给不同的正文claimed PID；真正sender只能来自packet。 */
    frame.b=clip.epoch;
    frame.c=PROBE_LARGE_BYTES;
    if(!require(send_frame(control_channel,FRAME_HELLO,&frame),"child hello"))return 0;

    ScSharedDesc shared={sizeof(ScSharedDesc),1,PROBE_SHARED_BYTES,
        SC32_SHARED_READ|SC32_SHARED_WRITE,0,{0,0,0}};
    if(!require(ScMemoryCreateShared(&shared,&created_memory)==0,"child create 18 shared pages"))return 0;
    ScSharedMappingInfo mapping;
    record(&mapping,sizeof(mapping));
    if(!require(ScMemoryMapShared(created_memory,0,PROBE_SHARED_BYTES,
        SC32_SHARED_READ|SC32_SHARED_WRITE,&mapping)==0 && mapping.state==SC32_SHARED_READY,
        "child shared map"))return 0;
    creator_mapping=mapping.handle;
    volatile ScU32 *memory=(volatile ScU32 *)mapping.address;
    for(ScU32 n=0;n<PROBE_SHARED_WORDS;n++)if(memory[n])return require(0,"all shared pages initially zero");
    memory[0]=MARK_INITIAL;
    memory[PROBE_SHARED_WORDS-1]=MARK_INITIAL;
    ScSharedGrantInfo grant,before;
    record(&grant,sizeof(grant));
    bytes_copy(&before,&grant,sizeof(before));
    ScProcessRef stale=*parent;
    stale.generation=parent->generation==1?2u:1u;
    if(!require(ScMemoryGrantShared(created_memory,&stale,0,PROBE_SHARED_BYTES,
        SC32_SHARED_READ,0,&grant)==SC32_E_STALE && bytes_same(&grant,&before,sizeof(grant)),
        "grant exact PID generation"))return 0;
    record(&grant,sizeof(grant));
    if(!require(ScMemoryGrantShared(created_memory,parent,0,PROBE_SHARED_BYTES,
        SC32_SHARED_READ,0,&grant)==0,"readonly grant"))return 0;
    readonly_grant=grant.grant;
    record(&grant,sizeof(grant));
    if(!require(ScMemoryGrantShared(created_memory,parent,0,PROBE_SHARED_BYTES,
        SC32_SHARED_READ|SC32_SHARED_WRITE,0,&grant)==0,"writable grant"))return 0;
    writable_grant=grant.grant;
    ScSharedMemory denied=FENCE;
    if(!require(ScMemoryOpenShared(readonly_grant,&denied)==SC32_E_ACCESS && denied==FENCE,
        "non-target cannot open grant"))return 0;
    ScSharedInfo information;
    record(&information,sizeof(information));
    if(!require(ScMemoryQueryShared(created_memory,&information)==0
        && information.ready_pages==18 && information.allocation_bytes==PROBE_SHARED_BYTES,
        "real shared object info"))return 0;
    make_frame(&frame,FRAME_GRANTS);
    frame.a=readonly_grant;
    frame.b=writable_grant;
    frame.c=information.object_id;
    frame.d=created_memory;
    frame.e=PROBE_SHARED_BYTES;
    if(!require(send_frame(control_channel,FRAME_GRANTS,&frame),"child grants packet"))return 0;

    for(ScU32 commands=0;commands<8u;commands++){
        ScIpcPacket packet;
        if(!require(receive_frame(control_channel,&frame,&packet),"child command receive"))return 0;
        if(frame_valid(&frame,&packet,FRAME_REVOKE,&peer)){
            if(!require(readonly_grant && ScMemoryRevokeShared(readonly_grant)==0,"revoke unopened future use"))return 0;
            readonly_grant=0;
            make_frame(&frame,FRAME_ACK);
            frame.a=FRAME_REVOKE;
        }else if(frame_valid(&frame,&packet,FRAME_DRAIN,&peer)){
            ScU32 count=frame.a,zero=frame.b;
            if(!require(count && count<=64u && zero<=1u,"bounded drain command"))return 0;
            for(ScU32 n=0;n<count;n++){
                ProbeFrame data;
                ScIpcPacket received;
                if(!require(receive_frame(data_channel,&data,&received),"data FIFO receive"))return 0;
                if(zero){
                    if(!require(received.flags==SC32_IPC_FRAME_DATA && received.type==FRAME_ZERO
                        && received.bytes==0 && sender_same(&received,&peer),"zero frame is DATA"))return 0;
                }else if(!require(frame_valid(&data,&received,FRAME_FILL,&peer) && data.a==n,
                    "atomic FIFO real sender"))return 0;
            }
            if(!require(memory[0]==MARK_PARENT && memory[PROBE_SHARED_WORDS-1]==MARK_PARENT,
                "child observes parent writes same pages"))return 0;
            memory[1]=MARK_CHILD;
            memory[PROBE_SHARED_WORDS-2]=MARK_CHILD;
            make_frame(&frame,FRAME_ACK);
            frame.a=FRAME_DRAIN;
            frame.b=count;
            frame.c=zero;
        }else if(frame_valid(&frame,&packet,FRAME_FINISH,&peer)){
            make_frame(&frame,FRAME_DONE);
            frame.a=checks;
            if(!require(send_frame(control_channel,FRAME_DONE,&frame),"child done before exit"))return 0;
            if(!say("PLATFORM_CHILD_PASS\n"))return require(0,"child output");
            /* 故意保留真实channel/ref/map/RW grant，main正常返回触发本人
             * kernel stop_step。父验证EOF、已打开mapping与正文仍活。 */
            return 1;
        }else return require(0,"unexpected child command");
        if(!require(send_frame(control_channel,FRAME_ACK,&frame),"child command ack"))return 0;
    }
    return require(0,"child command bound");
}

static int accept_child(ScIpcEndpoint endpoint,ScIpcChannel *channel,ScBrokerIdentity *peer)
{
    if(wait_ready(endpoint,SC32_BROKER_READY_ACCEPT,0,PROBE_WAIT_TICKS))return 0;
    record(peer,sizeof(*peer));
    return ScIpcAccept(endpoint,channel,peer)==0;
}
static int check_idle_timeout(void)
{
    ScU32 start=0,timeouts=0;
    if(!tick_now(&start))return 0;
    for(ScU32 tries=0;tries<64u;tries++){
        ScBrokerReadyInfo ready;
        record(&ready,sizeof(ready));
        if(ScBrokerReady(data_channel,&ready) || (ready.flags&SC32_BROKER_READY_READ))return 0;
        ScU32 now=0;
        if(!tick_now(&now))return 0;
        ScU32 elapsed=now-start;
        if(elapsed>=3u)return timeouts!=0;
        ScBrokerWaitDesc wait={sizeof(ScBrokerWaitDesc),1,data_channel,SC32_BROKER_READY_READ,0,0,3u-elapsed,0};
        ScU32 events=0;
        ScStatus status=ScBrokerWait(&wait,&events);
        if(status<0)return 0;
        if(status==SC32_TIMEOUT){
            if(!(events&SC32_EVENT_TIMEOUT))return 0;
            timeouts++;
        }
    }
    return 0;
}
static int command_ack(ScU32 kind,ScU32 a,ScU32 b,const ScBrokerIdentity *peer)
{
    ProbeFrame frame;
    make_frame(&frame,kind);
    frame.a=a;
    frame.b=b;
    if(!send_frame(control_channel,kind,&frame))return 0;
    ScIpcPacket packet;
    if(!receive_frame(control_channel,&frame,&packet))return 0;
    return frame_valid(&frame,&packet,FRAME_ACK,peer) && frame.a==kind
        && (kind!=FRAME_DRAIN || (frame.b==a && frame.c==b));
}
static int parent_run(const char *application,const ScIdentityInfo *identity)
{
    if(!require(identity->uid==1000 && identity->realm==SC32_BROKER_REALM_NORMAL
        && identity->session!=0 && !(identity->flags&SC32_IDENTITY_MODULES),
        "real mio login required"))return 0;
    if(!publish_seed(application))return 0;
    char name[64]="P.",data_name[64]="",command[256]="";
    append_number(name,sizeof(name),self.pid);
    append_text(name,sizeof(name),".");
    append_number(name,sizeof(name),self.generation);
    append_text(data_name,sizeof(data_name),name);
    append_text(data_name,sizeof(data_name),".data");
    ScIpcEndpointDesc desc;
    record(&desc,sizeof(desc));
    desc.name=name;
    desc.namespace_id=SC32_IPC_NAMESPACE_SYSTEM;
    desc.queue_budget_bytes=4096;
    desc.max_frame_bytes=1024;
    desc.pending_budget_bytes=4096;
    ScIpcEndpoint denied=FENCE;
    if(!require(ScIpcCreate(&desc,&denied)==SC32_E_ACCESS && denied==FENCE,
        "ordinary cannot register SYSTEM namespace"))return 0;
    desc.namespace_id=SC32_IPC_NAMESPACE_SESSION;
    if(!require(ScIpcCreate(&desc,&control_endpoint)==0 && ScIpcListen(control_endpoint)==0,
        "real session listener"))return 0;
    desc.name=data_name;
    desc.queue_budget_bytes=1024;
    desc.max_frame_bytes=128;
    if(!require(ScIpcCreate(&desc,&data_endpoint)==0 && ScIpcListen(data_endpoint)==0,
        "bounded data listener"))return 0;
    append_text(command,sizeof(command),application);
    append_text(command,sizeof(command)," child ");
    append_text(command,sizeof(command),name);
    append_text(command,sizeof(command)," ");
    append_number(command,sizeof(command),self.pid);
    append_text(command,sizeof(command)," ");
    append_number(command,sizeof(command),self.generation);
    if(!require(ScProcessSpawn(command,0,0,&child_job)==0,"spawn genuine second SCX"))return 0;
    ScJobInfo job;
    record(&job,sizeof(job));
    if(!require(ScJobQuery(child_job,&job)==0 && job.child_pid && job.child_generation,
        "spawn child generation"))return 0;
    ScProcessRef child={sizeof(ScProcessRef),1,job.child_pid,job.child_generation};
    ScBrokerIdentity peer,data_peer;
    if(!require(accept_child(control_endpoint,&control_channel,&peer)
        && accept_child(data_endpoint,&data_channel,&data_peer)
        && peer.pid==child.pid && peer.generation==child.generation
        && peer.uid==1000 && peer.realm==SC32_BROKER_REALM_NORMAL
        && peer.session==identity->session && !(peer.flags&SC32_BROKER_ID_HARDWARE_SYSTEM)
        && bytes_same(&peer,&data_peer,sizeof(peer)),"accepted real same-session child"))return 0;
    ScIpcInfo channel_info;
    record(&channel_info,sizeof(channel_info));
    if(!require(ScIpcQuery(control_channel,&channel_info)==0
        && channel_info.state==SC32_IPC_STATE_CONNECTED,"real connected state"))return 0;
    if(!require(wait_ready(control_channel,SC32_BROKER_READY_READ,0,PROBE_WAIT_TICKS)==0,
        "hello ready"))return 0;
    ScIpcPacket peek,packet,unchanged;
    record(&peek,sizeof(peek));
    if(!require(ScIpcPeek(control_channel,&peek)==0 && peek.type==FRAME_HELLO
        && peek.bytes==sizeof(ProbeFrame) && sender_same(&peek,&peer),"atomic peek required bytes"))return 0;
    record(&packet,sizeof(packet));
    bytes_copy(&unchanged,&packet,sizeof(packet));
    ProbeByte short_body=0x5a;
    if(!require(ScIpcReceive(control_channel,&short_body,1,&packet)==SC32_E_RANGE
        && short_body==0x5a && bytes_same(&packet,&unchanged,sizeof(packet)),"small receive does not consume"))return 0;
    record(&packet,sizeof(packet));
    if(!require(ScIpcPeek(control_channel,&packet)==0
        && packet.sequence_lo==peek.sequence_lo && packet.sequence_hi==peek.sequence_hi,
        "same head after RANGE"))return 0;
    ProbeFrame hello,grants;
    if(!require(receive_frame(control_channel,&hello,&packet)
        && frame_valid(&hello,&packet,FRAME_HELLO,&peer)
        && hello.a==self.pid && packet.sender.pid!=hello.a && hello.c==PROBE_LARGE_BYTES,
        "sender ignores forged payload PID"))return 0;
    if(!check_clipboard(hello.b,&peer,application))return 0;
    if(!require(receive_frame(control_channel,&grants,&packet)
        && frame_valid(&grants,&packet,FRAME_GRANTS,&peer) && grants.e==PROBE_SHARED_BYTES,
        "typed grant delivery"))return 0;
    if(!require(check_idle_timeout(),"bounded idle wait is not EOF"))return 0;
    ScSharedInfo shared_info;
    record(&shared_info,sizeof(shared_info));
    if(!require(ScMemoryQueryShared(grants.d,&shared_info)==SC32_E_STALE,
        "foreign handle is not authority"))return 0;
    if(!require(ScMemoryOpenShared(grants.a,&readonly_memory)==0
        && ScMemoryOpenShared(grants.b,&writable_memory)==0,"grant exact recipient opens"))return 0;
    ScSharedMappingInfo map,old_map;
    record(&map,sizeof(map));
    bytes_copy(&old_map,&map,sizeof(map));
    if(!require(ScMemoryMapShared(readonly_memory,0,PROBE_SHARED_BYTES,
        SC32_SHARED_READ|SC32_SHARED_WRITE,&map)==SC32_E_ACCESS
        && bytes_same(&map,&old_map,sizeof(map)),"readonly cannot map writable"))return 0;
    ScMemoryInfo before_maps,after_maps;
    record(&before_maps,sizeof(before_maps));
    if(!require(ScSystemMemory(&before_maps)==0,"before shared PTE count"))return 0;
    record(&map,sizeof(map));
    if(!require(ScMemoryMapShared(readonly_memory,0,PROBE_SHARED_BYTES,SC32_SHARED_READ,&map)==0
        && map.state==SC32_SHARED_READY && map.object_id==grants.c && map.access==SC32_SHARED_READ,
        "readonly real shared map"))return 0;
    readonly_mapping=map.handle;
    ScU32 readonly_address=map.address;
    const volatile ScU32 *ro=(const volatile ScU32 *)readonly_address;
    record(&map,sizeof(map));
    if(!require(ScMemoryMapShared(writable_memory,0,PROBE_SHARED_BYTES,
        SC32_SHARED_READ|SC32_SHARED_WRITE,&map)==0 && map.state==SC32_SHARED_READY
        && map.object_id==grants.c && map.address!=readonly_address,"second alias same object"))return 0;
    writable_mapping=map.handle;
    volatile ScU32 *rw=(volatile ScU32 *)map.address;
    record(&after_maps,sizeof(after_maps));
    if(!require(ScSystemMemory(&after_maps)==0
        && after_maps.process_total_pages==before_maps.process_total_pages+36u,
        "two aliases map 36 actual user PTEs"))return 0;
    ScU32 mapped_pages=after_maps.process_total_pages;
    if(!require(ro[0]==MARK_INITIAL && ro[PROBE_SHARED_WORDS-1]==MARK_INITIAL,
        "shared grant initial bytes"))return 0;
    rw[32]=sizeof(ScFontInfo);
    rw[33]=1;
    rw[34]=FENCE;
    if(!require(ScFontQuery(SC32_FONT_PHOENIX12,(ScFontInfo *)(readonly_address+128u))==SC32_E_INVALID
        && ro[34]==FENCE,"true readonly PTE rejects output"))return 0;
    if(!require(ScMemoryFreePages((void *)readonly_address)==SC32_E_INVALID
        && ro[0]==MARK_INITIAL,"private free cannot free shared PF"))return 0;
    if(!require(command_ack(FRAME_REVOKE,0,0,&peer),"revoke handshake"))return 0;
    ScSharedMemory rejected=FENCE;
    if(!require(ScMemoryOpenShared(grants.a,&rejected)==SC32_E_STALE && rejected==FENCE
        && ro[0]==MARK_INITIAL,"revoke stops new open keeps mapped ref"))return 0;
    rw[0]=MARK_PARENT;
    rw[PROBE_SHARED_WORDS-1]=MARK_PARENT;
    ProbeFrame frame;
    ScU32 filled=0;
    ScStatus full=0;
    for(;filled<64u;filled++){
        make_frame(&frame,FRAME_FILL);
        frame.a=filled;
        full=ScIpcSend(data_channel,FRAME_FILL,&frame,sizeof(frame));
        if(full)break;
    }
    if(!require(filled>0 && filled<64u && full==SC32_E_AGAIN,"real queue backpressure"))return 0;
    ScBrokerReadyInfo ready;
    record(&ready,sizeof(ready));
    if(!require(ScBrokerReady(data_channel,&ready)==0
        && (!(ready.flags&SC32_BROKER_READY_WRITE) || ready.write_bytes<sizeof(frame)),
        "write readiness reflects atomic space"))return 0;
    if(!require(command_ack(FRAME_DRAIN,filled,0,&peer),"bounded FIFO drain ack"))return 0;
    if(!require(ro[1]==MARK_CHILD && ro[PROBE_SHARED_WORDS-2]==MARK_CHILD,
        "parent observes child writes same pages"))return 0;
    if(!require(ScIpcSend(data_channel,FRAME_ZERO,0,0)==0
        && command_ack(FRAME_DRAIN,1,1,&peer),"zero DATA and queue space restored"))return 0;
    make_frame(&frame,FRAME_FINISH);
    if(!require(send_frame(control_channel,FRAME_FINISH,&frame)
        && receive_frame(control_channel,&frame,&packet)
        && frame_valid(&frame,&packet,FRAME_DONE,&peer),"child finish actual ACK"))return 0;
    record(&job,sizeof(job));
    if(!require(ScJobWait(child_job,PROBE_WAIT_TICKS,&job)==0 && job.exit_code==0,
        "source main returns normally"))return 0;
    child_job=0;
    if(!require(wait_ready(control_channel,SC32_BROKER_READY_CLOSED,0,PROBE_WAIT_TICKS)==0,
        "exit cleanup closes peer"))return 0;
    record(&packet,sizeof(packet));
    if(!require(ScIpcReceive(control_channel,0,0,&packet)==0
        && packet.flags==SC32_IPC_FRAME_EOF && packet.bytes==0 && sender_same(&packet,&peer)
        && ScIpcSend(control_channel,FRAME_FINISH,0,0)==SC32_E_BROKEN_PIPE,
        "real EOF separate from zero DATA"))return 0;
    rejected=FENCE;
    if(!require(ScMemoryOpenShared(grants.b,&rejected)==SC32_E_STALE && rejected==FENCE
        && ro[1]==MARK_CHILD,"issuer exit keeps opened map stops grant"))return 0;
    if(!require(ScMemoryCloseShared(readonly_memory)==0 && ScMemoryCloseShared(writable_memory)==0,
        "close reference preserves mapping"))return 0;
    readonly_memory=0;
    writable_memory=0;
    rw[2]=MARK_PARENT;
    if(!require(ro[2]==MARK_PARENT,"last opened refs closed aliases still share"))return 0;
    if(!require(ScMemoryUnmapShared(readonly_mapping)==0,"readonly unmap all 18 pages"))return 0;
    readonly_mapping=0;
    if(!require(ScFontQuery(SC32_FONT_PHOENIX12,(ScFontInfo *)(readonly_address+128u))==SC32_E_INVALID,
        "unmapped VA fails safe output probe"))return 0;
    if(!require(ScMemoryUnmapShared(writable_mapping)==0,"last mapping unmap"))return 0;
    writable_mapping=0;
    record(&after_maps,sizeof(after_maps));
    if(!require(ScSystemMemory(&after_maps)==0
        && after_maps.process_total_pages==before_maps.process_total_pages
        && after_maps.page_table_pages==before_maps.page_table_pages,"own shared PTE page recovery"))return 0;
    char evidence[192]="PLATFORM_SHARED_PTE before=";
    append_number(evidence,sizeof(evidence),before_maps.process_total_pages);
    append_text(evidence,sizeof(evidence)," mapped=");
    append_number(evidence,sizeof(evidence),mapped_pages);
    append_text(evidence,sizeof(evidence)," after=");
    append_number(evidence,sizeof(evidence),after_maps.process_total_pages);
    append_text(evidence,sizeof(evidence)," tables_before=");
    append_number(evidence,sizeof(evidence),before_maps.page_table_pages);
    append_text(evidence,sizeof(evidence)," tables_after=");
    append_number(evidence,sizeof(evidence),after_maps.page_table_pages);
    append_text(evidence,sizeof(evidence)," filled=");
    append_number(evidence,sizeof(evidence),filled);
    append_text(evidence,sizeof(evidence),"\n");
    if(!say(evidence))return require(0,"shared evidence output");
    if(!require(ScMemoryAllocatePages(SC32_BROKER_IO_MAX,&scratch)==0,"snapshot read buffer"))return 0;
    if(!read_large_snapshot())return 0;
    if(!require(ScMemoryFreePages(scratch)==0,"snapshot buffer free"))return 0;
    scratch=0;
    if(!require(ScClipboardClose(seed_snapshot)==0 && ScClipboardClose(large_snapshot)==0,
        "old and source-exit snapshot close"))return 0;
    seed_snapshot=0;
    large_snapshot=0;
    ScClipboardInfo cleared;
    record(&cleared,sizeof(cleared));
    if(!require(ScClipboardClear(current_epoch,&cleared)==0 && (cleared.flags&SC32_CLIPBOARD_EMPTY),
        "CAS clear own test epoch"))return 0;
    current_epoch=0;
    ScU32 epoch=0;
    if(!require(ScClipboardWatch(0,&epoch)==0,"watch close"))return 0;
    watched=0;
    if(!require(ScIpcClose(control_channel)==0,"parent control close"))return 0;
    control_channel=0;
    if(!require(ScIpcClose(data_channel)==0,"parent data close"))return 0;
    data_channel=0;
    if(!require(ScIpcClose(control_endpoint)==0,"parent listener close"))return 0;
    control_endpoint=0;
    if(!require(ScIpcClose(data_endpoint)==0,"parent data listener close"))return 0;
    data_endpoint=0;
    if(!say("PLATFORM_CLIPBOARD_PASS\nPLATFORM_IPC_SHARED_PASS\n"))return require(0,"parent workflow output");
    return 1;
}

static int font_palette(void)
{
    struct {ScFontInfo value;ScU32 tail[4];} query;
    struct {ScFontGlyph value;ScU32 tail[4];} glyph,before;
    for(ScU32 face=12;face<=16;face+=4){
        record(&query,sizeof(query));
        for(ScU32 n=0;n<4;n++)query.tail[n]=FENCE;
        if(!require(ScFontQuery(face,&query.value)==0 && query.value.size==sizeof(ScFontInfo)
            && query.value.active && query.value.face==face && query.value.generation
            && query.value.source_kind==SC32_FONT_SOURCE_PHOENIX_TTF,"real Phoenix typed query"))return 0;
        for(ScU32 n=0;n<4;n++)if(query.tail[n]!=FENCE)return require(0,"font query unknown tail");
        ScFontGlyphDesc desc={sizeof(ScFontGlyphDesc),1,face,0x10ffffu,face,
            query.value.generation==1?2u:1u,0,0};
        record(&glyph,sizeof(glyph));
        for(ScU32 n=0;n<4;n++)glyph.tail[n]=FENCE;
        bytes_copy(&before,&glyph,sizeof(before));
        if(!require(ScFontGetGlyph(&desc,&glyph.value)==SC32_E_STALE
            && bytes_same(&glyph,&before,sizeof(glyph)),"font generation error keeps entire output"))return 0;
        desc.expected_generation=query.value.generation;
        if(!require(ScFontGetGlyph(&desc,&glyph.value)==0 && glyph.value.size==sizeof(ScFontGlyph)
            && glyph.value.generation==query.value.generation && glyph.value.mapped==0
            && glyph.value.pixels==face && glyph.value.width<=128u && glyph.value.height<=96u,
            "legal missing scalar gives real notdef"))return 0;
        for(ScU32 n=0;n<4;n++)if(glyph.tail[n]!=FENCE)return require(0,"glyph unknown tail");
        for(ScU32 row=glyph.value.height;row<SC32_FONT_ROWS;row++)
            for(ScU32 word=0;word<SC32_FONT_ROW_WORDS;word++)
                if(glyph.value.rows[row][word])return require(0,"unused glyph rows zero");
    }
    ScU32 colors[260];
    for(ScU32 n=0;n<260;n++)colors[n]=FENCE;
    if(!require(ScDisplayPalette(colors,255)==SC32_E_RANGE,"palette short range rejects"))return 0;
    for(ScU32 n=0;n<260;n++)if(colors[n]!=FENCE)return require(0,"palette failure output unchanged");
    if(!require(ScDisplayPalette(colors,260)==0,"real palette prefix"))return 0;
    for(ScU32 n=0;n<256;n++)if(colors[n]&0xff000000u)return require(0,"palette RGB width");
    for(ScU32 n=256;n<260;n++)if(colors[n]!=FENCE)return require(0,"palette unknown tail");
    return say("PLATFORM_FONT_PALETTE_PASS\n");
}
static void cleanup(void)
{
    /* 父失败先停孩子，再撤本人map/ref；不能让仍在运行的源重新提交clip。 */
    if(child_job){
        ScJobCancel(child_job);
        ScJobInfo job;
        record(&job,sizeof(job));
        if(ScJobWait(child_job,PROBE_WAIT_TICKS,&job)==0)child_job=0;
    }
    if(readonly_mapping)ScMemoryUnmapShared(readonly_mapping);
    if(writable_mapping)ScMemoryUnmapShared(writable_mapping);
    if(creator_mapping)ScMemoryUnmapShared(creator_mapping);
    if(readonly_memory)ScMemoryCloseShared(readonly_memory);
    if(writable_memory)ScMemoryCloseShared(writable_memory);
    if(readonly_grant)ScMemoryRevokeShared(readonly_grant);
    if(writable_grant)ScMemoryRevokeShared(writable_grant);
    if(created_memory)ScMemoryCloseShared(created_memory);
    if(control_channel)ScIpcClose(control_channel);
    if(data_channel)ScIpcClose(data_channel);
    if(control_endpoint)ScIpcClose(control_endpoint);
    if(data_endpoint)ScIpcClose(data_endpoint);
    if(candidate)ScClipboardAbort(candidate);
    if(seed_snapshot)ScClipboardClose(seed_snapshot);
    if(large_snapshot)ScClipboardClose(large_snapshot);
    if(current_epoch){
        ScClipboardInfo cleared;
        record(&cleared,sizeof(cleared));
        ScClipboardClear(current_epoch,&cleared);
    }
    if(watched){ScU32 epoch=0;ScClipboardWatch(0,&epoch);}
    if(scratch)ScMemoryFreePages(scratch);
}
int main(void)
{
    if(ScFileGetStandard(1,&output))return 2;
    record(&self,sizeof(self));
    ScIdentityInfo identity;
    record(&identity,sizeof(identity));
    if(!require(ScProcessSelf(&self)==0 && ScIdentityQuery(&identity)==0,"actual caller identity")){
        report_failure();
        return 1;
    }
    char identity_line[192]="PLATFORM_ID pid=";
    append_number(identity_line,sizeof(identity_line),self.pid);
    append_text(identity_line,sizeof(identity_line)," gen=");
    append_number(identity_line,sizeof(identity_line),self.generation);
    append_text(identity_line,sizeof(identity_line)," uid_bits=");
    append_number(identity_line,sizeof(identity_line),(ScU32)identity.uid);
    append_text(identity_line,sizeof(identity_line)," realm=");
    append_number(identity_line,sizeof(identity_line),identity.realm);
    append_text(identity_line,sizeof(identity_line)," sid=");
    append_number(identity_line,sizeof(identity_line),identity.session);
    append_text(identity_line,sizeof(identity_line)," flags=");
    append_number(identity_line,sizeof(identity_line),identity.flags);
    append_text(identity_line,sizeof(identity_line),"\n");
    if(!say(identity_line))return 3;
    ScCapabilities caps;
    record(&caps,sizeof(caps));
    if(!require(ScSystemCapabilities(&caps)==0 && caps.abi_major==1 && caps.abi_minor>=2
        && (caps.capabilities_lo&SC32_CAP_FONT_GLYPHS),"current SC32 ABI and font service")){
        report_failure();
        return 1;
    }
    char arguments[256];
    ScU32 bytes=0;
    if(!require(ScProcessArguments(arguments,sizeof(arguments),&bytes)==0,"process arguments")){
        report_failure();
        return 1;
    }
    char *cursor=arguments,*mode=next_argument(&cursor);
    int success=0,is_child=0;
    if(text_same(mode,"child")){
        is_child=1;
        char *name=next_argument(&cursor),*pid=next_argument(&cursor),*generation=next_argument(&cursor);
        ScProcessRef parent={sizeof(ScProcessRef),1,0,0};
        if(require(*name && text_length(name)<48u && number_parse(pid,&parent.pid)
            && number_parse(generation,&parent.generation) && parent.pid && parent.generation
            && !*next_argument(&cursor),"child launch fields"))success=child_run(name,&parent);
    }else if(text_same(mode,"font")){
        if(require(!*next_argument(&cursor),"font mode arguments"))success=font_palette();
    }else{
        char *application=next_argument(&cursor);
        if(!*mode)application="/HOME/M10P/PLATFORMPROBE.SCX";
        if(require((!*mode || text_same(mode,"all")) && *application=='/'
            && text_length(application)<=63u && !*next_argument(&cursor),"parent launch path")
            && require((caps.capabilities_lo&(SC32_CAP_CLIPBOARD|SC32_CAP_IPC|SC32_CAP_SHARED_MEMORY))
                ==(SC32_CAP_CLIPBOARD|SC32_CAP_IPC|SC32_CAP_SHARED_MEMORY),"real broker capabilities"))
        {
            char canonical[65];ScU32 path_bytes=0;
            if(require(ScPathResolve(application,canonical,sizeof(canonical),&path_bytes)==0
                && path_bytes && canonical[0]=='/',"canonical app path for typed FILES"))
                success=parent_run(canonical,&identity) && font_palette();
        }
    }
    if(!success){
        report_failure();
        cleanup();
        return 1;
    }
    if(is_child)return 0;
    cleanup();
    char done[96]="";
    append_text(done,sizeof(done),text_same(mode,"font")?"PLATFORM_FONT_MODE_PASS checks=":"PLATFORM_FOUNDATION_PASS checks=");
    append_number(done,sizeof(done),checks);
    append_text(done,sizeof(done),"\n");
    if(!say(done))return 3;
    return 0;
}
