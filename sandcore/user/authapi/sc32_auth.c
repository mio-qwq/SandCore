#include "../sc32/SC32PRIVATE.H"
#include "../SC32AUTH.H"
enum {SC32_AUTH_BEGIN=0x1800,SC32_AUTH_QUERY=0x1801,SC32_AUTH_ACTIVATE=0x1802,SC32_AUTH_CANCEL=0x1803};
static ScU32 sc32_auth_serial,sc32_auth_id,sc32_auth_raw,sc32_auth_generation;
static ScStatus sc32_auth_error(ScI32 raw,ScU32 op)
{ScStatus s=raw==-5?SC32_E_ACCESS:raw==-6?SC32_E_AGAIN:raw==-4?SC32_E_MEMORY:raw==-1?SC32_E_STALE:SC32_E_FAILED;
    return sc32_fail(s,SC32_DOMAIN_LEGACY,raw,op);
}
static void sc32_auth_finalize(void)
{if(sc32_auth_raw)(void)sc32_call(0x206,(ScI32)sc32_auth_raw,0,0,0,0,0);
    sc32_auth_raw=sc32_auth_id=sc32_auth_generation=0;
}
static ScStatus sc32_auth_ticket(const ScAuthTicket *ticket,ScU32 op)
{
    ScStatus s=sc32_record_in(ticket,sizeof(*ticket));
    if(s<0)return sc32_invalid(s,op);
    ScProcessRef self;
    sc32_zero(&self,sizeof(self));
    self.size=sizeof(self);
    self.version=1;
    s=ScProcessSelf(&self);
    if(s<0)return s;
    if(!ticket->id || ticket->id!=sc32_auth_id || ticket->owner_generation!=self.generation || ticket->owner_generation!=sc32_auth_generation || !sc32_auth_raw)return sc32_invalid(SC32_E_STALE,op);
    return 0;
}
static ScStatus sc32_auth_text(const char *text,ScU32 bytes,ScU32 maximum,ScU32 op)
{
    if(bytes>maximum || !text)return sc32_invalid(SC32_E_INVALID,op);
    ScStatus s=sc32_probe(text,bytes+1,SC32_WIRE_READ);
    if(s<0)return sc32_invalid(s,op);
    for(ScU32 i=0;i<bytes;i++)if(!text[i])return sc32_invalid(SC32_E_INVALID,op);
    if(text[bytes])return sc32_invalid(SC32_E_INVALID,op);
    return 0;
}
ScStatus ScAuthLoginBegin(const ScAuthLoginDesc *d,ScAuthTicket *out)
{
    ScStatus s=sc32_record_in(d,sizeof(*d));
    if(s<0)return sc32_invalid(s,SC32_AUTH_BEGIN);
    s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_invalid(s,SC32_AUTH_BEGIN);
    if(!d->username_bytes || d->flags || d->reserved)return sc32_invalid(SC32_E_INVALID,SC32_AUTH_BEGIN);
    if(sc32_auth_id)return sc32_invalid(SC32_E_BUSY,SC32_AUTH_BEGIN);
    if(sc32_auth_serial==0x7fffffffu)return sc32_invalid(SC32_E_RANGE,SC32_AUTH_BEGIN);
    s=sc32_auth_text(d->username,d->username_bytes,31,SC32_AUTH_BEGIN);
    if(s<0)return s;
    s=sc32_auth_text(d->password,d->password_bytes,128,SC32_AUTH_BEGIN);
    if(s<0)return s;
    if(sc32_overlap(out,out->size,d->password,d->password_bytes+1))return sc32_invalid(SC32_E_INVALID,SC32_AUTH_BEGIN);
    ScProcessRef self;
    sc32_zero(&self,sizeof(self));
    self.size=sizeof(self);
    self.version=1;
    s=ScProcessSelf(&self);
    if(s<0)return s;
    ScI32 raw=sc32_call(0x201,(ScI32)d->username,(ScI32)d->password,0,0,0,0);
    if(raw<=0)return sc32_auth_error(raw,SC32_AUTH_BEGIN);
    sc32_auth_id=++sc32_auth_serial;
    sc32_auth_raw=(ScU32)raw;
    sc32_auth_generation=self.generation;
    ScAuthTicket result={sizeof(result),1,sc32_auth_id,self.generation};
    sc32_copy(out,&result,sizeof(result));
    return 0;
}
ScStatus ScAuthLoginQuery(const ScAuthTicket *ticket,ScAuthLoginInfo *out)
{
    ScStatus s=sc32_auth_ticket(ticket,SC32_AUTH_QUERY);
    if(s<0)return s;
    s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_invalid(s,SC32_AUTH_QUERY);
    if(sc32_overlap(out,out->size,ticket,sizeof(*ticket)))return sc32_invalid(SC32_E_INVALID,SC32_AUTH_QUERY);
    ScU32 raw[8];
    sc32_zero(raw,sizeof(raw));
    ScI32 result=sc32_call(0x202,(ScI32)sc32_auth_raw,(ScI32)raw,0,0,0,0);
    if(result<0 && result!=-5)return sc32_auth_error(result,SC32_AUTH_QUERY);
    if(raw[0]!=1 || raw[1]<1 || raw[1]>3 || raw[5])return sc32_invalid(SC32_E_FAILED,SC32_AUTH_QUERY);
    ScAuthLoginInfo info;
    sc32_zero(&info,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    info.state=raw[1];
    info.uid=(ScI32)raw[4];
    if(info.state==SC32_AUTH_PENDING && raw[3]){info.flags=SC32_AUTH_PROGRESS;
        info.iterations_done=raw[2];
        info.iterations_total=raw[3];
    }
    if(info.state==SC32_AUTH_DENIED)info.cause=SC32_E_ACCESS;
    sc32_copy(out,&info,sizeof(info));
    return 0;
}
ScStatus ScAuthLoginActivate(const ScAuthTicket *ticket)
{
    ScStatus s=sc32_auth_ticket(ticket,SC32_AUTH_ACTIVATE);
    if(s<0)return s;
    static const char empty[]="";
    ScI32 raw=sc32_call(0x203,(ScI32)sc32_auth_raw,(ScI32)empty,1,0,0,0);
    if(raw<0)return sc32_auth_error(raw,SC32_AUTH_ACTIVATE);
    sc32_auth_finalize();
    return 0;
}
ScStatus ScAuthLoginCancel(const ScAuthTicket *ticket)
{ScStatus s=sc32_auth_ticket(ticket,SC32_AUTH_CANCEL);
    if(s<0)return s;
    ScI32 raw=sc32_call(0x206,(ScI32)sc32_auth_raw,0,0,0,0,0);
    sc32_auth_raw=sc32_auth_id=sc32_auth_generation=0;
    return raw<0?sc32_auth_error(raw,SC32_AUTH_CANCEL):0;
}
