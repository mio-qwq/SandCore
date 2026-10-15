static ScStatus sh_fail(ScStatus code,ScStatus cause,ScU32 operation,ScU32 item) {
    memset(&sh_error,0,sizeof(sh_error));
    sh_error.size=sizeof(sh_error);
    sh_error.version=1;
    sh_error.code=code;
    sh_error.cause=cause;
    sh_error.operation=operation;
    sh_error.item=item;
    if(cause<=-101&&cause>=-199) {
        ScErrorInfo e;
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        if(!ScErrorQuery(&e)) {
            sh_error.domain=e.domain;
            sh_error.raw=e.raw;
        }
    }
    return code;
}
static ScStatus sh_cause(ScStatus cause,ScU32 operation,ScU32 item) {
    ScStatus code=SC_SHELL_E_IO;
    if(cause>=0)return cause;
    if(cause<=SC_SHELL_E_INVALID&&cause>=SC_SHELL_E_NOT_SUPPORTED) {
        if(sh_error.code==cause) {
            sh_error.operation=operation;
            sh_error.item=item;
            return cause;
        }
        return sh_fail(cause,cause,operation,item);
    }
    if(cause==SC32_E_MEMORY||cause==SC_UTIL_E_MEMORY||cause==SC_UI_E_MEMORY)code=SC_SHELL_E_MEMORY;
    else if(cause==SC32_E_ACCESS)code=SC_SHELL_E_ACCESS;
    else if(cause==SC32_E_NOT_FOUND||cause==SC_UTIL_E_NOT_FOUND||cause==SC_UI_E_NOT_FOUND)code=SC_SHELL_E_NOT_FOUND;
    else if(cause==SC32_E_STALE||cause==SC_UTIL_E_STALE||cause==SC_UI_E_STALE)code=SC_SHELL_E_STALE;
    else if(cause==SC32_E_BUSY||cause==SC_UTIL_E_BUSY||cause==SC_UI_E_BUSY)code=SC_SHELL_E_BUSY;
    else if(cause==SC32_E_RANGE||cause==SC_UTIL_E_RANGE||cause==SC_UI_E_RANGE)code=SC_SHELL_E_RANGE;
    else if(cause==SC32_E_EXISTS||cause==SC_UTIL_E_EXISTS||cause==SC_UI_E_EXISTS)code=SC_SHELL_E_EXISTS;
    else if(cause==SC_UTIL_E_UTF8||cause==SC_UI_E_UTF8)code=SC_SHELL_E_UTF8;
    else if(cause==SC32_E_CANCELLED||cause==SC_UTIL_E_CANCELLED||cause==SC_UI_E_CANCELLED)code=SC_SHELL_E_CANCELLED;
    else if(cause==SC32_E_NOT_SUPPORTED||cause==SC_UTIL_E_NOT_SUPPORTED||cause==SC_UI_E_NOT_SUPPORTED)code=SC_SHELL_E_NOT_SUPPORTED;
    return sh_fail(code,cause,operation,item);
}
static ScStatus sh_record(const void *record,ScU32 bytes) {
    const ScU32 *h=(const ScU32 *)record;
    if(!h||h[0]<bytes)return SC_SHELL_E_INVALID;
    return h[1]==1?0:SC_SHELL_E_VERSION;
}
static ScStatus sh_output(void *out,const void *value,ScU32 bytes) {
    ScStatus s=sh_record(out,bytes);
    if(s<0)return s;
    memcpy(out,value,bytes);
    return 0;
}
static ScStatus sh_text(const char *text,ScU32 maximum,ScU32 empty) {
    ScU32 n=0;
    if(!text)return empty?0:SC_SHELL_E_INVALID;
    while(n<=maximum&&text[n])++n;
    if(n>maximum)return SC_SHELL_E_RANGE;
    if(!n&&!empty)return SC_SHELL_E_INVALID;
    return ScUtf8Validate(text,n,0)<0?SC_SHELL_E_UTF8:0;
}
static ScStatus sh_copy_text(const char *text,ScU32 maximum,ScU32 empty,char **out) {
    ScStatus s=sh_text(text,maximum,empty);
    ScU32 n;
    char *p;
    if(s<0)return s;
    if(!text)text="";
    n=(ScU32)strlen(text);
    p=(char *)malloc(n+1);
    if(!p)return SC_SHELL_E_MEMORY;
    memcpy(p,text,n+1);
    *out=p;
    return 0;
}
static ScStatus sh_absolute(const char *path,char **out) {
    char resolved[65];
    ScU32 n=0;
    ScStatus s=sh_text(path,127,0);
    char *p;
    if(s<0)return s;
    s=ScPathResolve(path,resolved,sizeof(resolved),&n);
    if(s<0)return sh_cause(s,1,0);
    if(n>64||!n||resolved[0]!='/')return SC_SHELL_E_RANGE;
    p=(char *)malloc(n+1);
    if(!p)return SC_SHELL_E_MEMORY;
    /* 正式ScPathResolve已输出根斜杠，不能再按旧raw73添第二次。 */ memcpy(p,resolved,n+1);
    *out=p;
    return 0;
}
static int sh_valid(const void *object,ScU32 kind) {
    const ShObject *o=(const ShObject *)object;
    return o&&o->magic==0x4C485353&&o->kind==kind&&!o->closing;
}
static void *sh_new(ScU32 bytes,ScU32 kind,void (*dispose)(ShObject *)) {
    ShObject *o;
    if(sh_quiet||bytes<sizeof(ShObject))return 0;
    o=(ShObject *)calloc(1,bytes);
    if(!o)return 0;
    o->magic=0x4C485353;
    o->kind=kind;
    o->references=1;
    o->dispose=dispose;
    o->next=sh_objects;
    if(sh_objects)sh_objects->previous=o;
    sh_objects=o;
    return o;
}
static ScStatus sh_retain(ShObject *o,ScU32 kind) {
    if(!sh_valid(o,kind))return SC_SHELL_E_STALE;
    if(o->references==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    ++o->references;
    return 0;
}
static void sh_object_free(ShObject *o) {
    if(o->previous)o->previous->next=o->next;
    else sh_objects=o->next;
    if(o->next)o->next->previous=o->previous;
    o->closing=1;
    o->magic=0;
    o->dispose(o);
    free(o);
}
static void sh_release(ShObject *o) {
    if(!o||o->magic!=0x4C485353||!o->references)return;
    --o->references;
    if(!o->references) {
        o->closing=1;
        if(!o->depth&&!sh_finalizing)sh_object_free(o);
    }
}
static void sh_enter(ShObject *o) {
    ++o->depth;
}
static void sh_leave(ShObject *o) {
    if(o->depth)--o->depth;
    if(!o->depth&&!o->references&&!sh_finalizing)sh_object_free(o);
}
static ScStatus sh_pin(ScShellEventProc procedure,ScDropProc drop) {
    ScStatus s=0;
    if(procedure)s=ScCallbackPin((const void *)procedure);
    if(s>=0&&drop)s=ScCallbackPin((const void *)drop);
    return sh_cause(s,2,0);
}
static int sh_same_identity(const ScBrokerIdentity *a,const ScBrokerIdentity *b) {
    return a->pid==b->pid&&a->generation==b->generation&&a->uid==b->uid&&a->gid==b->gid&& a->realm==b->realm&&a->session==b->session&&a->subject_uid==b->subject_uid&& a->subject_gid==b->subject_gid&&a->flags==b->flags;
}
static ScStatus sh_self(ScBrokerIdentity *out) {
    ScIdentityInfo id;
    ScProcessRef ref;
    ScStatus s;
    memset(&id,0,sizeof(id));
    id.size=sizeof(id);
    id.version=1;
    memset(&ref,0,sizeof(ref));
    ref.size=sizeof(ref);
    ref.version=1;
    s=ScIdentityQuery(&id);
    if(s<0)return sh_cause(s,3,0);
    s=ScProcessSelf(&ref);
    if(s<0)return sh_cause(s,3,0);
    if(id.realm!=SC32_BROKER_REALM_NORMAL||id.uid<0||id.gid<0||!id.session||(id.flags&SC32_IDENTITY_MODULES))return SC_SHELL_E_ACCESS;
    memset(out,0,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->pid=ref.pid;
    out->generation=ref.generation;
    out->uid=id.uid;
    out->gid=id.gid;
    out->realm=id.realm;
    out->session=id.session;
    out->subject_uid=id.uid;
    out->subject_gid=id.gid;
    return 0;
}
static int sh_subject(const ScBrokerIdentity *a,const ScBrokerIdentity *b) {
    return a->realm==SC32_BROKER_REALM_NORMAL&&!a->flags&&a->uid>=0&&a->gid>=0&& a->uid==b->uid&&a->gid==b->gid&&a->session==b->session&& a->subject_uid==b->subject_uid&&a->subject_gid==b->subject_gid;
}
static ScStatus sh_next_counter(ScWideCounter *c) {
    if(c->lo==0xFFFFFFFFu) {
        if(c->hi==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
        c->lo=0;
        ++c->hi;
    } else ++c->lo;
    return 0;
}
static int sh_counter_equal(const ScWideCounter *a,const ScWideCounter *b) {
    return a->lo==b->lo&&a->hi==b->hi;
}
static int sh_counter_before(const ScWideCounter *a,const ScWideCounter *b) {
    return a->hi<b->hi||(a->hi==b->hi&&a->lo<b->lo);
}
static void sh_deadline(ScWideCounter *out,const ScWideCounter *now,ScU32 ticks) {
    out->lo=now->lo+ticks;
    out->hi=now->hi+(out->lo<now->lo);
}
static ScStatus sh_buffer_data(const ScBuffer *buffer,const unsigned char **data,ScU32 *bytes) {
    ScView v;
    ScStatus s;
    if(!data||!bytes)return SC_SHELL_E_INVALID;
    /* 错误翻译函数不是编译器内建的负值断言；输出在所有失败路径也
     * 明确定义为空，调用者仍须检查原错误，不用未初始化地址/长度。 */
    *data=0;
    *bytes=0;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    s=ScBufferView(buffer,&v);
    if(s<0)return sh_cause(s,4,0);
    *data=(const unsigned char *)v.data;
    *bytes=v.bytes;
    return 0;
}
static ScStatus sh_buffer_copy(const ScBuffer *source,ScBuffer **out) {
    const unsigned char *data;
    ScU32 n;
    ScBuffer *b=0;
    ScStatus s=sh_buffer_data(source,&data,&n);
    if(s<0)return s;
    s=ScBufferCreate(n,&b);
    if(s>=0)s=ScBufferAppend(b,data,n);
    if(s<0) {
        ScBufferRelease(b);
        return sh_cause(s,4,0);
    }
    *out=b;
    return 0;
}
static ScStatus sh_append_string(ScBuffer *b,const char *s,ScU32 *offset,ScU32 *bytes) {
    const unsigned char *p;
    ScU32 n,k,z=0;
    ScStatus status=sh_buffer_data(b,&p,&n);
    if(status<0)return status;
    k=(ScU32)strlen(s)+1;
    if(k>0xFFFFFFFCu||n>0xFFFFFFFFu-k-3)return SC_SHELL_E_RANGE;
    status=ScBufferAppend(b,s,k);
    if(status>=0&&((4-(k&3))&3))status=ScBufferAppend(b,&z,(4-(k&3))&3);
    if(status<0)return sh_cause(status,4,0);
    *offset=n;
    *bytes=k;
    return 0;
}
static ScStatus sh_string_at(const unsigned char *p,ScU32 total,ScU32 floor,ScU32 offset,ScU32 bytes,ScU32 max,const char **out) {
    ScU32 i,end;
    if(!bytes||bytes>max+1||offset<floor||(offset&3)||offset>total||bytes>total-offset)return SC_SHELL_E_PROTOCOL;
    if(p[offset+bytes-1]||memchr(p+offset,0,bytes-1))return SC_SHELL_E_PROTOCOL;
    end=(bytes+3)&~3u;
    if(end>total-offset)return SC_SHELL_E_PROTOCOL;
    for(i=bytes;i<end;++i)if(p[offset+i])return SC_SHELL_E_PROTOCOL;
    if(ScUtf8Validate(p+offset,bytes-1,0)<0)return SC_SHELL_E_UTF8;
    *out=(const char *)(p+offset);
    return 0;
}
ScStatus ScShellErrorQuery(ScShellErrorInfo *out) {
    return sh_output(out,&sh_error,sizeof(sh_error));
}
