#include "SC32TERMINALWIRE.H"
static ScStatus sc32_terminal_status(ScI32 raw,ScU32 operation)
{
    if(!raw)return 0;
    return sc32_fail(raw<=SC32_E_INVALID && raw>=SC32_E_FAILED?raw:SC32_E_FAILED,SC32_DOMAIN_KERNEL,raw,operation);
}
ScStatus ScProcessSpawnInteractive(const char *command,const ScTerminalDesc *desc,ScInteractiveInfo *out)
{
    ScU32 operation=0x1600;
    ScStatus s=sc32_record_in(desc,sizeof(*desc));
    if(s<0)return sc32_invalid(s,operation);
    s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_invalid(s,operation);
    s=sc32_string(command,1087,0);
    if(s<0)return sc32_invalid(s,operation);
    if(desc->flags || desc->reserved[0] || desc->reserved[1] || desc->reserved[2]
    || !desc->columns || desc->columns>512 || !desc->rows || desc->rows>256)return sc32_invalid(SC32_E_INVALID,operation);
    Sc32TerminalSpawnRequest q;
    sc32_zero(&q,sizeof(q));
    q.size=sizeof(q);
    q.version=1;
    q.command=(ScU32)command;
    q.columns=desc->columns;
    q.rows=desc->rows;
    ScInteractiveInfo value;
    sc32_zero(&value,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    ScI32 raw=sc32_call(SC32_WIRE_GATE,SC32_T_SPAWN,(ScI32)&q,(ScI32)&value,0,0,0);
    s=sc32_terminal_status(raw,operation);
    if(!s)sc32_copy(out,&value,sizeof(value));
    return s;
}
static ScI32 sc32_terminal_call(ScFile file,ScU32 a,ScU32 b,ScU32 selector,void *out)
{
    Sc32TerminalRequest q;
    sc32_zero(&q,sizeof(q));
    q.size=sizeof(q);
    q.version=1;
    q.handle=file;
    q.a=a;
    q.b=b;
    return sc32_call(SC32_WIRE_GATE,(ScI32)selector,(ScI32)&q,(ScI32)out,0,0,0);
}
ScStatus ScTerminalQuery(ScFile file,ScTerminalInfo *out)
{
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_invalid(s,0x1601);
    ScTerminalInfo value;
    sc32_zero(&value,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    ScI32 raw=sc32_terminal_call(file,0,0,SC32_T_QUERY,&value);
    s=sc32_terminal_status(raw,0x1601);
    if(!s)sc32_copy(out,&value,sizeof(value));
    return s;
}
ScStatus ScTerminalResize(ScFile file,ScU32 columns,ScU32 rows)
{
    if(!columns || columns>512 || !rows || rows>256)return sc32_invalid(SC32_E_INVALID,0x1602);
    return sc32_terminal_status(sc32_terminal_call(file,columns,rows,SC32_T_RESIZE,0),0x1602);
}
ScStatus ScTerminalControl(ScFile file,ScU32 action)
{
    if(action<1 || action>3)return sc32_invalid(SC32_E_INVALID,0x1603);
    return sc32_terminal_status(sc32_terminal_call(file,action,0,SC32_T_CONTROL,0),0x1603);
}
