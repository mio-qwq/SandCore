#include "session.h"
#include "objpool.h"
#include "task.h"
#include "task_store.h"
#include "auth.h"
#include "wm.h"
#include "theme.h"
#include "desktop.h"
#include "userspace.h"
#include "interrupts.h"
#include "fs.h"

static objpool_t sessions;
static u32 serial,visible,system_id,locked_id;
static int login_screen_pid=-1;
static u32 login_screen_generation;
static void copy(char *out,const char *in,u32 size)
{u32 i=0;while(i+1<size && in[i]){out[i]=in[i];i++;}out[i]=0;}
session_t *session_get(u32 id)
{return id && id<=0x7FFFFFFFu?objpool_get(&sessions,(int)id):0;}
u32 session_active(void){return visible;}
u32 session_task(int pid){return task_owned(pid)?task_record(pid)->session:0;}
u32 session_current(void){return task_pid()>0?session_task(task_pid()):visible;}
u32 session_system(void){return system_id;}
u32 session_create(int uid,int gid,u32 kind,const char *name,const char *home)
{
    if(serial==0x7FFFFFFFu || kind>SESSION_LOCKED)return 0;
    u32 id=serial+1;if(objpool_claim(&sessions,(int)id))return 0;
    session_t *s=session_get(id);s->id=id;s->uid=uid;s->gid=gid;s->kind=kind;s->epoch=1;s->task_head=-1;
    copy(s->name,name,32);copy(s->home,home,64);serial=id;return id;
}
void session_init(int uid,int gid,const char *name,const char *home)
{
    if(objpool_init(&sessions,sizeof(session_t),0))panic("SESSION MEMORY",0);
    visible=session_create(uid,gid,SESSION_NORMAL,name,home);
    system_id=session_create(AUTH_SYSTEM,AUTH_SYSTEM,SESSION_SYSTEM,"SYSTEM","SYS");
    locked_id=session_create(-2,-2,SESSION_LOCKED,"Locked","SYS");
    if(!visible || !system_id || !locked_id)panic("SESSION MEMORY",0);
    session_get(visible)->retained=session_get(system_id)->retained=session_get(locked_id)->retained=1;
    (void)session_bind(0,system_id);
}
int session_bind(int pid,u32 id)
{
    session_t *s=session_get(id);if(!task_owned(pid) || !s || s->closing)return -1;
    if(task_record(pid)->session==id)return 0;
    session_unbind(pid);task_record_t *p=task_record(pid);
    p->session=id;p->session_previous=-1;p->session_next=s->task_head;
    if(s->task_head>=0)task_record(s->task_head)->session_previous=pid;
    s->task_head=pid;s->references++;return 0;
}
static void release(session_t *s)
{
    u32 id=s->id;wm_session_destroy(id);desktop_session_destroy(id);theme_session_destroy(id);userspace_session_destroy(id);
    objpool_release(&sessions,(int)id);
}
void session_unbind(int pid)
{
    if(!task_owned(pid))return;task_record_t *p=task_record(pid);session_t *s=session_get(p->session);
    if(!s){p->session=0;p->session_previous=p->session_next=-1;return;}
    if(p->session_previous>=0)task_record(p->session_previous)->session_next=p->session_next;else s->task_head=p->session_next;
    if(p->session_next>=0)task_record(p->session_next)->session_previous=p->session_previous;
    p->session=0;p->session_previous=p->session_next=-1;if(s->references)s->references--;
    if(!s->references && (s->closing || (!s->retained && s->id!=visible)))release(s);
}
void session_discard(u32 id)
{
    session_t *s=session_get(id);if(s && !s->references && id!=visible && id!=system_id && id!=locked_id)release(s);
}
int session_switch_trusted(u32 id)
{
    session_t *s=session_get(id);if(!s || s->closing)return -1;
    if(id==visible)return 0;
    /* 先构造目标桌面；申请失败时原会话和输入归属原封保留。
     * 切换在IF=0进行，渲染持有期间IRQ不会调用本入口。 */
    if(wm_session_prepare(id))return -4;
    u32 previous=visible;visible=id;s->retained=1;s->epoch++;if(!s->epoch)s->epoch=1;
    wm_session_switched(previous,id);return 0;
}
static int permitted(int pid,session_t *s)
{
    return auth_can_mod(pid) || (s->kind==SESSION_NORMAL && s->uid>=0 && s->uid==auth_uid(pid));
}
int session_control(int pid,u32 id,u32 action)
{
    if(action==2){if(!auth_can_mod(pid) && (session_task(pid)!=visible || !wm_focused(pid) || auth_uid(pid)<0))return -5;
        return session_show_login();}
    session_t *s=session_get(id);if(!s || !permitted(pid,s))return -5;
    if(action==0)return session_switch_trusted(id);
    if(action!=1 || id==system_id || id==locked_id)return -1;
    if(id==visible && session_switch_trusted(locked_id))return -4;
    /* 只沿本会话成员链停止；每个任务先撤运行/授权，退出解除成员。
     * 不用UID扫描杀掉同账户在另一个登录会话的后台任务。 */
    s->closing=1;
    for(int pid=s->task_head;pid>=0;){int following=task_record(pid)->session_next;task_stop(pid);pid=following;}
    s=session_get(id);if(s && !s->references)release(s);
    if(visible==locked_id)(void)session_show_login();return 0;
}
int session_show_login(void)
{
    if(login_screen_pid<1 || !task_owned(login_screen_pid) || TASK(login_screen_pid).state!=1
        || task_generation(login_screen_pid)!=login_screen_generation){
        int child=auth_login_screen_exec(locked_id);if(child<1)return child;
        login_screen_pid=child;login_screen_generation=task_generation(child);
    }
    return session_switch_trusted(locked_id);
}
int session_page(int pid,u32 *out,u32 words,u32 after)
{
    if(words<32 || words>1040 || (after!=0xFFFFFFFFu && after>0x7FFFFFFFu))return -1;
    for(u32 i=0;i<16;i++)out[i]=0;
    u32 capacity=(words-16)/16,count=0,examined=0;int cursor=after==0xFFFFFFFFu?-1:(int)after;
    int next=objpool_next(&sessions,cursor);
    while(next>=0 && count<capacity && examined<256){
        session_t *s=session_get((u32)next);
        cursor=next;examined++;
        if(permitted(pid,s)){
            u32 *row=out+16+count++*16;
            for(u32 j=0;j<16;j++)row[j]=0;
            row[0]=s->id;row[1]=(u32)s->uid;row[2]=(u32)s->gid;row[3]=s->kind;
            row[4]=s->id==visible;row[5]=s->closing;row[6]=s->references;row[7]=s->epoch;
            copy((char *)(row+8),s->name,32);
        }
        next=objpool_next(&sessions,cursor);
    }
    if(next<0)cursor=-1;
    out[0]=1;out[1]=16;out[2]=16;out[3]=count;out[4]=(u32)cursor;
    out[5]=session_task(pid);out[6]=auth_can_mod(pid)?visible:session_task(pid)==visible?visible:0;
    return (int)count;
}
int session_config_path_for(u32 id,const char *leaf,char out[64])
{
    session_t *s=session_get(id);if(!s || s->closing || s->kind==SESSION_LOCKED)return -1;
    if(!leaf || *leaf=='/' || *leaf=='\\')return -1;
    const char *component=leaf;
    for(const char *p=leaf;;p++){
        if(!*p || *p=='/'){u32 length=(u32)(p-component);
            if((length==1 && component[0]=='.') || (length==2 && component[0]=='.' && component[1]=='.'))return -1;
            component=p+1;if(!*p)break;
        }else if(*p=='\\' || *p==':' || (u8)*p<32)return -1;
    }
    /* mio沿用已有SYS配置以保留原盘外观；其它身份各自的HOME/桌面配置。
     * 同UID多次登录各自持有内存快照，显式保存才更新共同的账户默认值。 */
    const char *base=s->kind==SESSION_SYSTEM?"SYS/ADMIN":s->uid==AUTH_DEFAULT?"SYS":s->home;
    u32 n=0;while(base[n]){if(n>=62)return -1;out[n]=base[n];n++;}
    if(s->kind==SESSION_NORMAL && s->uid!=AUTH_DEFAULT){const char *suffix="/.DESKTOP";
        while(*suffix){if(n>=62)return -1;out[n++]=*suffix++;}}
    if(n>=62)return -1;out[n++]='/';
    while(*leaf){if(n>=63)return -1;out[n++]=*leaf++;}out[n]=0;return (int)n;
}
int session_config_path(const char *leaf,char out[64])
{return session_config_path_for(session_current(),leaf,out);}
int session_config_prepare(int pid)
{
    session_t *s=session_get(session_task(pid));if(!s || s->closing || s->kind==SESSION_LOCKED)return -5;
    if(s->kind==SESSION_SYSTEM && !auth_can_mod(pid))return -5;
    if(s->uid==AUTH_DEFAULT)return 0;
    char path[64];u32 info[2];if(session_config_path_for(s->id,"",path)<0)return -1;
    if(!fs_stat(path,info))return info[0]==2 && fs_access(pid,path,FS_ACCESS_WRITE)?0:-5;
    /* 首次显式保存才落盘；目录从创建时就是本人rw，不能先公共可读再改权。
     * 失败保留全部现有快照，普通服务不能借SYSTEM名称初始化管理配置。 */
    int result=fs_create_ex(pid,path,2,3);return result==0?0:result;
}
