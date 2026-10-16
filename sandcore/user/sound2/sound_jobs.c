static ScU32 sound_tick(void)
{ScMonotonicClock c;memset(&c,0,sizeof(c));c.size=sizeof(c);c.version=1;return ScSystemMonotonic(&c)<0?0:c.ticks.lo;}
static void sound_slot_stop(SndSlot *s)
{
    if(!s || s->cancel)return;
    s->cancel=1;s->phase=S2_CLOSING;
    if(s->channel){ScIpcClose(s->channel);s->channel=0;}
    if(s->endpoint){ScIpcClose(s->endpoint);s->endpoint=0;}
    if(s->job)ScJobCancel(s->job);
}
static ScStatus sound_send(SndSlot *s,ScU32 opcode,ScU32 value)
{
    if(!s || !s->channel || s->inflight)return SC32_E_BUSY;
    if(s->sequence==0xffffffffu)return SC32_E_RANGE;
    SndMessage m;memset(&m,0,sizeof(m));m.size=sizeof(m);m.version=1;
    m.opcode=opcode;m.sequence=s->sequence+1;m.value=value;
    if(opcode==SND_OPEN){strcpy(m.path,s->path);m.seconds=s->seconds;
        m.file.file_revision=s->revision;m.flags=s->skip_metadata?SND_SKIP_METADATA:0;}
    ScStatus status=ScIpcSend(s->channel,SC32_IPC_FRAME_DATA,&m,sizeof(m));
    if(!status){s->sequence=m.sequence;s->inflight=opcode;s->expected_opcode=m.sequence;}
    return status;
}
static int sound_peer(SndSlot *s,const ScBrokerIdentity *p)
{
    return p->pid==s->peer.pid && p->generation==s->peer.generation
        && p->uid==sound_app.identity.uid && p->gid==sound_app.identity.gid
        && p->subject_uid==p->uid && p->subject_gid==p->gid
        && p->session==sound_app.identity.session && p->realm==SC32_BROKER_REALM_NORMAL && !p->flags;
}
static ScStatus sound_begin(const char *path,ScU32 seconds,ScU32 revision)
{
    if(sound_app.candidate || sound_app.retiring)return SC32_E_BUSY;
    if(!path || path[0]!='/' || strlen(path)>=SC32_PATH_CAPACITY)return SC32_E_INVALID;
    if(sound_app.nonce==0xffffffffu)return SC32_E_RANGE;
    SndSlot *s=calloc(1,sizeof(*s));if(!s)return SC32_E_MEMORY;
    strcpy(s->path,path);s->seconds=seconds;s->revision=revision;
    s->want_pause=sound_app.paused;s->want_volume=sound_app.volume;
    s->skip_metadata=sound_app.current && revision && sound_app.current->state.file.file_revision==revision
        && !strcmp(sound_app.current->path,path);
    snprintf(s->endpoint_name,sizeof(s->endpoint_name),"SND2.%u.%u.%u.%u",
        sound_app.self.pid,sound_app.self.generation,sound_app.identity.session,++sound_app.nonce);
    ScIpcEndpointDesc d;memset(&d,0,sizeof(d));d.size=sizeof(d);d.version=1;
    d.name=s->endpoint_name;d.session=sound_app.identity.session;
    d.queue_budget_bytes=32768;d.max_frame_bytes=sizeof(SndCoverChunk);d.pending_budget_bytes=8192;
    d.acl_flags=SC32_IPC_ACL_SESSION|SC32_IPC_ACL_UID|SC32_IPC_ACL_GID|SC32_IPC_ACL_REALM;
    d.allowed_uid=sound_app.identity.uid;d.allowed_gid=sound_app.identity.gid;
    d.allowed_session=sound_app.identity.session;d.allowed_realm=SC32_BROKER_REALM_NORMAL;
    ScStatus status=ScIpcCreate(&d,&s->endpoint);
    if(!status)status=ScIpcListen(s->endpoint);
    char command[160];snprintf(command,sizeof(command),"/APPS/SOUNDWORK.SCX %s",s->endpoint_name);
    if(!status)status=ScProcessSpawn(command,0,SC32_SPAWN_NO_STREAM_INHERIT,&s->job);
    if(status<0){if(s->endpoint)ScIpcClose(s->endpoint);free(s);return status;}
    s->phase=S2_CONNECTING;s->deadline=sound_tick()+1500;sound_app.candidate=s;
    tool_status("正在准备音频；当前播放继续。取消可保留原曲。");return 0;
}
static void sound_slot_failed(SndSlot *s,ScStatus status)
{
    if(!s->failure){s->failure=status;tool_error(status,"音频任务；原播放与文件保留");}
    sound_slot_stop(s);
}
static ScStatus sound_cover_packet(SndSlot *s,const SndCoverChunk *m,ScU32 bytes)
{
    if(bytes<20 || m->size!=bytes || m->version!=1 || m->opcode!=SND_COVER
        || !m->bytes || m->bytes>SND_COVER_CHUNK || bytes!=20u+m->bytes
        || m->offset!=s->cover_at || m->offset>s->cover_bytes
        || m->bytes>s->cover_bytes-m->offset)return SC32_E_INVALID;
    if(s->cover)memcpy(s->cover+s->cover_at,m->data,m->bytes);
    s->cover_at+=m->bytes;
    return 0;
}
static ScStatus sound_receive(SndSlot *s,int *more)
{
    union {SndMessage state;SndCoverChunk cover;} body;
    for(ScU32 count=0;count<4;count++){
        ScIpcPacket packet;memset(&packet,0,sizeof(packet));packet.size=sizeof(packet);packet.version=1;
        ScStatus status=ScIpcReceive(s->channel,&body,sizeof(body),&packet);
        if(status==SC32_E_AGAIN)return 0;
        if(status<0)return status;
        if(packet.flags&SC32_IPC_FRAME_EOF)return SC32_E_BROKEN_PIPE;
        if(!sound_peer(s,&packet.sender) || packet.type!=SC32_IPC_FRAME_DATA)return SC32_E_BROKEN_PIPE;
        if(packet.bytes>=12 && body.state.opcode==SND_COVER){
            status=sound_cover_packet(s,&body.cover,packet.bytes);if(status<0)return status;
            *more=1;continue;
        }
        SndMessage *m=&body.state;
        if(packet.bytes!=sizeof(*m) || m->size!=sizeof(*m) || m->version!=1
            || !memchr(m->title,0,sizeof(m->title)) || !memchr(m->artist,0,sizeof(m->artist))
            || m->cover_bytes>SND_COVER_LIMIT || m->cover_kind>2 || m->value>256)return SC32_E_INVALID;
        if(m->opcode==SND_HELLO){if(s->phase!=S2_HELLO || m->sequence)return SC32_E_INVALID;
            s->phase=S2_OPENING;*more=1;continue;}
        if(m->opcode!=SND_STATUS){
            if(!s->inflight || m->sequence!=s->expected_opcode)return SC32_E_INVALID;
            ScU32 operation=s->inflight;s->inflight=0;
            if(m->opcode==SND_ERROR)return m->status<0?m->status:SC32_E_FAILED;
            if(operation==SND_OPEN){
                if(m->opcode!=SND_READY || s->phase!=S2_OPENING || !m->file.rate
                    || m->file.file_revision==0)return SC32_E_INVALID;
                s->phase=S2_READY;
            }else if(m->opcode!=SND_ACK)return SC32_E_INVALID;
            if(operation==SND_OPEN || operation==SND_SIDECAR){
                free(s->cover);s->cover=0;s->cover_bytes=m->cover_bytes;s->cover_at=0;
                /* 封面分配失败仍排空值协议，不能停止已确认的音频。 */
                if(m->cover_bytes)s->cover=malloc(m->cover_bytes);
                if(operation==SND_SIDECAR)s->fallback=2;
            }
            if(operation==SND_QUIT){s->quit_sent=2;s->phase=S2_CLOSING;}
        }else if(m->sequence>s->sequence)return SC32_E_INVALID;
        s->state=*m;
        if(s==sound_app.current)sound_changed();
        *more=1;
        /* STOP的ACK就是交接终点。worker会随即关闭连接，不能在同一
         * 接收批次继续把它的正常EOF解释为音频失败。 */
        if(s->quit_sent==2)return 0;
    }
    return 0;
}
static int sound_slot_step(SndSlot *s)
{
    if(!s)return 0;
    int more=0;ScStatus status=0;
    if(!s->cancel && s->phase==S2_CONNECTING){
        ScBrokerIdentity peer;memset(&peer,0,sizeof(peer));peer.size=sizeof(peer);peer.version=1;
        status=ScIpcAccept(s->endpoint,&s->channel,&peer);
        if(!status){ScJobInfo job;memset(&job,0,sizeof(job));job.size=sizeof(job);job.version=1;
            status=ScJobQuery(s->job,&job);s->peer=peer;
            if(!status && (!sound_peer(s,&peer) || peer.pid!=job.original_pid || peer.generation!=job.child_generation))status=SC32_E_ACCESS;
            if(!status){ScIpcClose(s->endpoint);s->endpoint=0;s->phase=S2_HELLO;more=1;}
        }else if(status==SC32_E_AGAIN){status=0;
            if((ScI32)(sound_tick()-s->deadline)>=0)status=SC32_E_FAILED;}
    }
    if(!status && !s->cancel && s->channel && s->quit_sent!=2)status=sound_receive(s,&more);
    if(!status && !s->cancel && s->phase==S2_OPENING && !s->open_sent){
        status=sound_send(s,SND_OPEN,s->want_volume);if(!status){s->open_sent=1;more=1;}
    }
    if(!status && !s->cancel && s->phase==S2_PLAYING && !s->inflight){
        if(s->fallback==1)status=sound_send(s,SND_SIDECAR,0);
        else if(s->state.value!=s->want_volume)status=sound_send(s,SND_VOLUME,s->want_volume);
        else if(((s->state.flags&SND_FLAG_PAUSED)!=0)!=s->want_pause)status=sound_send(s,SND_PAUSE,s->want_pause);
    }
    if(status<0 && status!=SC32_E_AGAIN && status!=SC32_E_BUSY)sound_slot_failed(s,status);
    return more;
}
static void sound_collect(SndSlot **slot)
{
    SndSlot *s=*slot;if(!s || !s->job)return;
    ScJobInfo job;memset(&job,0,sizeof(job));job.size=sizeof(job);job.version=1;
    ScStatus status=ScJobQuery(s->job,&job);
    if(status<0){sound_slot_failed(s,status);return;}
    if(job.state!=SC32_JOB_FINISHED)return;
    /* Query未消费票据；只有Wait成功才撤job，不能随后对旧ID再Close。 */
    status=ScJobWait(s->job,0,&job);if(status!=0)return;s->job=0;
    if(s->channel)ScIpcClose(s->channel);
    if(s->endpoint)ScIpcClose(s->endpoint);
    if(!s->cancel && !s->quit_sent)tool_error(SC32_E_FAILED,"音频任务意外退出");
    free(s->cover);free(s);*slot=0;
    /* 候选已撤而旧曲EOF后不再发telemetry，必须在清空candidate后
     * 刷回旧曲实际状态，不能让“正在准备”文案无限残留。 */
    if(slot==&sound_app.candidate && sound_app.current)sound_changed();
}
static void sound_promote(void)
{
    SndSlot *next=sound_app.candidate;
    if(!next || next->cancel || next->phase!=S2_READY)return;
    if(sound_app.current){SndSlot *old=sound_app.current;
        if(!old->quit_sent && !old->inflight){ScStatus s=sound_send(old,SND_QUIT,0);
            if(!s){old->quit_sent=1;old->phase=S2_STOPPING;}
            else if(s!=SC32_E_AGAIN && s!=SC32_E_BUSY)sound_slot_failed(old,s);}
        if(old->quit_sent!=2)return;
        sound_app.retiring=old;sound_app.current=0;
    }
    sound_app.candidate=0;sound_app.current=next;next->phase=S2_PLAYING;
    next->want_pause=sound_app.paused;next->want_volume=sound_app.volume;
    strcpy(sound_app.display_path,next->path);
    if(!next->skip_metadata){
        strcpy(sound_app.track_title,next->state.title);strcpy(sound_app.track_artist,next->state.artist);
        sound_image_cancel();ScGfxCanvasSetSurface(sound_app.canvas,0);
        ScGfxSurfaceRelease(sound_app.cover);sound_app.cover=0;}
    sound_changed();
}
static int sound_jobs_step(void)
{
    int more=sound_slot_step(sound_app.current)|sound_slot_step(sound_app.candidate)|sound_slot_step(sound_app.retiring);
    sound_collect(&sound_app.retiring);sound_collect(&sound_app.current);sound_collect(&sound_app.candidate);
    if(tool.quit){sound_slot_stop(sound_app.candidate);sound_slot_stop(sound_app.current);sound_slot_stop(sound_app.retiring);}
    else sound_promote();
    return more;
}
