#include "sc32_core.h"
#include "paging.h"
#include "../user/sc32/SC32GUIWIRE.H"
#include "../user/sc32/SC32FILEWIRE.H"
#include "../user/sc32/SC32BROKERWIRE.H"
#include "../user/fontapi/SC32FONTWIRE.H"
#include "../user/debugapi/SC32DEBUGWIRE.H"
#include "login_resource_issue.h"
static u32 login_resource_generations[LOGIN_RESOURCE_COUNT];

static int login_resource_verify(u32 index)
{
    const login_resource_issue_t *issue=&login_issued_resources[index];
    u32 stat[2],meta[8];
    if(fs_stat(issue->path,stat) || stat[0]!=1 || stat[1]!=issue->bytes || fs_metadata(issue->path,meta))return 0;
    if(meta[7] && login_resource_generations[index]==meta[7])return 1;
    /* 身份/长度/摘要绑定实际受保护文件，不能把旧同名用户文件改个
     * owner就授予登录服务读取。缓存仅在同一真实内容revision有效。 */
    sha256_t hash;
    sha256_init(&hash);
    u8 chunk[512],digest[32];
    for(u32 at=0;at<stat[1];){u32 count=stat[1]-at;
        if(count>sizeof(chunk))count=sizeof(chunk);
        if(fs_read_at(issue->path,chunk,count,at)!=(int)count){crypto_zero(&hash,sizeof(hash));
            crypto_zero(chunk,sizeof(chunk));
            return 0;
        }
        sha256_update(&hash,chunk,count);
        at+=count;
    }
    sha256_final(&hash,digest);
    int accepted=crypto_equal(digest,issue->sha256,32);
    crypto_zero(digest,sizeof(digest));
    crypto_zero(chunk,sizeof(chunk));
    if(accepted)login_resource_generations[index]=meta[7];
    return accepted;
}
static int login_resources_ready(void)
{for(u32 i=0;i<LOGIN_RESOURCE_COUNT;i++)if(!login_resource_verify(i))return 0;
    return 1;
}
int auth_login_screen_role(int pid)
{
    credential_t *c=credential(pid);
    session_t *s=c?session_get(session_task(pid)):0;
    return c && c->login_role && c->uid==AUTH_SYSTEM && c->gid==AUTH_SYSTEM && c->realm==AUTH_SERVICE
    && c->subject_uid==-2 && c->subject_gid==-2 && s && !s->closing && s->kind==SESSION_LOCKED;
}
int auth_launch_issued_login(void){return launch_set && login_launch;
}
int auth_login_resource_access(int pid,const char *path,u32 access)
{
    if(!auth_login_screen_role(pid) || !access || (access&~(FS_ACCESS_READ|FS_ACCESS_META)))return 0;
    char canonical[64];
    if(fs_normalize(path,canonical)<0)return 0;
    for(u32 i=0;i<LOGIN_RESOURCE_COUNT;i++)if(same(canonical,login_issued_resources[i].path))return login_resource_verify(i);
    return 0;
}
static int login_sc32_allowed(int pid,u32 operation,u32 request)
{
    if(operation>=SC32_GUI_CREATE && operation<=SC32_GUI_CURSOR)return operation!=SC32_GUI_POST;
    if(operation>=SC32_WIRE_PROBE && operation<=SC32_WIRE_MONOTONIC)return 1;
    if(operation==SC32_FONT_QUERY || operation==SC32_FONT_GLYPH)return 1;
    if(operation==SC32_D2_REGISTER || operation==SC32_D2_UNREGISTER || operation==SC32_D2_READY)return 1;
    if(operation==SC32_F_READ || operation==SC32_F_SEEK || operation==SC32_F_TELL || operation==SC32_F_CLOSE || operation==SC32_F_QUERY || operation==0x223u)return 1;
    if(operation==SC32_F_OPEN){Sc32FileOpenRequest input;
        if(sc32_core_input(pid,request,&input,sizeof(input)))return 0;
        return input.flags==SC32_FILE_READ && !input.create_mode && !input.reserved[0] && !input.reserved[1] && !input.reserved[2];
    }
    if(operation==SC32_B_WAIT){ScBrokerWaitDesc input;
        if(sc32_core_input(pid,request,&input,sizeof(input)))return 0;
        return !input.handle && !input.ready_flags && !input.minimum_write_bytes && !input.reserved && !(input.event_mask&~(TASK_EVENT_INPUT|TASK_EVENT_VISIBILITY|TASK_EVENT_STREAM|TASK_EVENT_AUTH));
    }
    return 0;
}
int auth_login_syscall_allowed(int pid,const u32 *frame)
{
    if(!auth_login_screen_role(pid))return 1;
    u32 call=frame[11];
    if(call==SYS_SC32)return login_sc32_allowed(pid,frame[8],frame[10]);
    if(call==SYS_USERQUERY)return frame[8]==3;
    if(call==SYS_AUTHEXEC){u8 first=1;
        return frame[9]==1 && !paging_user_copy(TASK(pid).pd,frame[10],&first,1,0) && !first;
    }
    switch(call){
        case SYS_EXIT:case SYS_GETTICK:case SYS_GETARGS:case SYS_ARGUMENTS2:case SYS_YIELD:
        case SYS_USERALLOC:case SYS_USERFREE:case SYS_MODULEALLOC:case SYS_MODULEPROTECT:case SYS_MODULEFREE:case SYS_SCLBOOT:
        case SYS_PROCESSSELF:case SYS_AUTHINFO:case SYS_AUTHLOGIN:case SYS_AUTHSTATUS:case SYS_AUTHCANCEL:
        case SYS_EVENTWAIT:case SYS_THEMEINFO:case SYS_DISPLAYINFO:case SYS_FSREAD:case SYS_FSREADAT:case SYS_STAT:
        return 1;
        default:return 0;
    }
}
