/* =====================================================================
 * mio：M8配置化用户空间与终端附着作业。
 *
 * 【身份与进程为什么分开】本版本只有可修改的用户名占位，不引入登录
 * 或磁盘账户权限。输出授权仍依据真实任务代数/父子关系/稳定窗口句柄，
 * 不能把可编辑的username当安全身份，也不能让pid复用继承旧终端权限。
 *
 * 【不破坏旧应用】旧FSREAD/EXEC的路径仍从根解释；只有新RESOLVE与
 * CLIRUN使用任务cwd。旧程序的SYS/SRC、HOME路径不用改就能继续工作。
 * 配置解析先生成候选，保存成功后才提交；坏值不改变当前有效快照。
 * ===================================================================== */
#include "userspace.h"
#include "auth.h"
#include "task.h"
#include "process.h"
#include "fs.h"
#include "gfx.h"
#include "wm.h"
#include "memory.h"
#include "session.h"
#include "objpool.h"
#include "interrupts.h"

/* 63字节根相对HOME再加斜杠/NUL需要65字节。旧查询仍按调用者容量
 * 完整检查并可返回-1，不把最长账户HOME复制失败悄悄变成根目录。 */
typedef struct { char username[32],home[65]; } user_config_t;
typedef struct { char name[32],value[256]; } env_item_t;
typedef struct { env_item_t items[16]; int count; } env_config_t;
typedef struct {env_config_t values;u32 refs;} env_snapshot_t;
typedef struct {
    char cwd[64],executable[64];
    u32 generation,parent_generation,ticket;
    int parent,terminal;
    /* 父任务保留最近一次结果；子任务槽回收不会改写这份记录。
     * child_generation还用于观测暂停，防止查询读到复用后的另一个任务。 */
    u32 job_ticket,child_generation;
    int child,result;
    env_snapshot_t *env;
} user_context_t;
static user_config_t user_candidate;
static env_config_t baseline_environment,env_candidate;
typedef struct {user_config_t user;env_config_t environment;} session_profile_t;
typedef struct {session_profile_t *profile;u32 pages;} profile_slot_t;
static objpool_t profiles;
static session_profile_t *profile_for(u32 id);
static const env_config_t *default_environment(int pid)
{session_profile_t *p=profile_for(session_task(pid));return p?&p->environment:&baseline_environment;}
static user_context_t empty_contexts;
static user_context_t *contexts_at(int pid)
{void *p=task_data(pid,TASK_DATA_USERSPACE);return p?p:&empty_contexts;}
#define USER_CONTEXT(pid) (*contexts_at(pid))
u32 userspace_task_bytes(void){return sizeof(user_context_t);}
static char config_text[4096];
static u32 next_ticket;

static int equal(const char *a,const char *b)
{ while(*a && *a==*b){a++;b++;}return *a==*b; }
static int length(const char *s){int n=0;while(s[n])n++;return n;}
static int copy(char *out,const char *value,int capacity)
{
    int n=length(value);if(n>=capacity)return -1;
    for(int i=0;i<=n;i++)out[i]=value[i];
    return n;
}
static char *trim(char *p)
{
    while(*p==' ' || *p=='\t')p++;
    int n=length(p);while(n && (p[n-1]==' ' || p[n-1]=='\t' || p[n-1]=='\r'))p[--n]=0;
    return p;
}
static int identifier(const char *p)
{
    int n=0;
    while(p[n]) {
        char c=p[n];
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_'
             || (n && c>='0' && c<='9')))return 0;
        if(++n>31)return 0;
    }
    return n!=0;
}
static int env_find(const env_config_t *env,const char *name)
{for(int i=0;i<env->count;i++)if(equal(env->items[i].name,name))return i;return -1;}
static int path_value(const char *value)
{
    /* PATH没有“空目录就是当前目录”的隐式规则。先限定四个绝对目录，
     * 再逐段规范化；目录尚未安装允许保留，运行命令时才检查候选文件。
     * 值不改写，Settings保存用户拼写；查找时再用FS统一大小写语义。 */
    int at=0,count=0;char segment[64],normal[64];
    if(!*value)return -1;
    do {
        int n=0;
        while(value[at] && value[at]!=':') {
            if(n>=63)return -1;
            segment[n++]=value[at++];
        }
        segment[n]=0;
        if(!n || segment[0]!='/' || fs_normalize(segment,normal)<0 || ++count>4)return -1;
        if(!value[at])break;
        at++;if(!value[at])return -1;
    }while(1);
    return 0;
}
static int parse(int kind)
{
    const char *p=config_text;int line=0,seen=0;char buffer[320];
    user_candidate=(user_config_t){0};env_candidate=(env_config_t){0};
    while(*p) {
        int n=0;
        while(*p && *p!='\n'){if(n>=319)return -1;buffer[n++]=*p++;}
        if(*p)p++;
        buffer[n]=0;char *text=trim(buffer);
        if(!line++) {
            if(!equal(text,kind==0?"SUSER1MIO":"SENV1MIO"))return -1;
            continue;
        }
        if(!*text || *text=='#')continue;
        char *value=text;while(*value && *value!='=')value++;
        if(!*value)return -1;
        *value++=0;char *key=trim(text);value=trim(value);
        /* 整体已验证UTF-8，这里额外禁控制字符。配置含换行的值没有
         * 转义语法，不能将另一条字段藏进用户名或环境变量。 */
        for(int i=0;value[i];i++)if((u8)value[i]<32 || value[i]==127)return -1;
        if(kind==0) {
            if(equal(key,"username")) {
                if(seen&1 || !*value || copy(user_candidate.username,value,32)<0)return -1;
                for(int i=0;value[i];i++)if(value[i]=='@' || value[i]=='>' || value[i]=='/' || value[i]=='\\')return -1;
                seen|=1;
            }else if(equal(key,"home")) {
                char normal[64];
                if(seen&2 || *value!='/' || fs_normalize(value,normal)<0)return -1;
                user_candidate.home[0]='/';copy(user_candidate.home+1,normal,63);seen|=2;
                /* 内部路径可占63B，带根斜杠的公开文本上限63B，避免
                 * 输出缓冲需要65B却仍宣称64B ABI。 */
                if(length(normal)>62)return -1;
            }else return -1;
        }else {
            if(!identifier(key) || length(value)>255 || env_candidate.count==16
               || env_find(&env_candidate,key)>=0)return -1;
            if(equal(key,"PATH") && path_value(value))return -1;
            env_item_t *entry=&env_candidate.items[env_candidate.count++];
            copy(entry->name,key,32);copy(entry->value,value,256);
        }
    }
    return kind==0?(seen==3?0:-1):(env_find(&env_candidate,"PATH")>=0?0:-1);
}
static int candidate(int kind,const char *path)
{
    u32 info[2];
    if(fs_stat(path,info) || info[0]!=1 || !info[1] || info[1]>=sizeof(config_text))return -2;
    int n=fs_read(path,config_text,sizeof(config_text)-1);
    if(n<0 || (u32)n!=info[1])return -2;
    for(int i=0;i<n;i++)if(!config_text[i])return -1;
    config_text[n]=0;
    if(!gfx_utf8_valid(config_text))return -1;
    return parse(kind);
}
void userspace_init(void)
{
    baseline_environment=(env_config_t){0};baseline_environment.count=1;
    copy(baseline_environment.items[0].name,"PATH",32);copy(baseline_environment.items[0].value,"/BIN:/APPS",256);
    if(objpool_init(&profiles,sizeof(profile_slot_t),0))panic("PROFILE MEMORY",0);
    (void)profile_for(session_active());
    USER_CONTEXT(0).generation=1;
}
static session_profile_t *profile_for(u32 id)
{
    if(!profiles.stride || !session_get(id))return 0;
    profile_slot_t *slot=objpool_get(&profiles,(int)id);if(slot)return slot->profile;
    if(objpool_claim(&profiles,(int)id))return 0;slot=objpool_get(&profiles,(int)id);
    slot->pages=(sizeof(session_profile_t)+4095)/4096;slot->profile=(session_profile_t *)pframe_alloc_run(slot->pages);
    if(!slot->profile){objpool_release(&profiles,(int)id);return 0;}
    u8 *bytes=(u8 *)slot->profile;for(u32 i=0;i<sizeof(session_profile_t);i++)bytes[i]=0;
    session_t *s=session_get(id);session_profile_t *p=slot->profile;
    copy(p->user.username,s->name,32);p->user.home[0]='/';(void)copy(p->user.home+1,s->home,64);p->environment=baseline_environment;
    char path[64];
    if(session_config_path_for(id,"USER.CFG",path)>=0 && fs_access_identity(path,s->uid,s->gid,FS_ACCESS_READ)
        && !candidate(0,path))p->user=user_candidate;
    if(session_config_path_for(id,"ENV.CFG",path)>=0 && fs_access_identity(path,s->uid,s->gid,FS_ACCESS_READ)
        && !candidate(1,path))p->environment=env_candidate;
    return p;
}
void userspace_session_destroy(u32 id)
{
    profile_slot_t *slot=profiles.stride?objpool_get(&profiles,(int)id):0;if(!slot)return;
    pframe_free_run((u32)slot->profile,slot->pages);objpool_release(&profiles,(int)id);
}
int userspace_config(int kind,const char *path,int action)
{
    if(kind<0 || kind>1 || action<0 || action>2 || (action!=2 && !path))return -1;
    char fixed[64];if(session_config_path(kind==0?"USER.CFG":"ENV.CFG",fixed)<0)return -5;
    session_profile_t *profile=profile_for(session_current());if(!profile)return -4;
    if(!fs_access(task_pid(),action==2?fixed:path,FS_ACCESS_READ))return -5;
    if(action==1 && (!fs_user_mutable(fixed,0) || !fs_access(task_pid(),fixed,FS_ACCESS_WRITE)))return -5;
    int result=candidate(kind,action==2?fixed:path);if(result)return result;
    if(!action)return 0; /* 验证不提交，普通编辑器可在覆盖固定配置之前校验 */
    if(action==1){result=session_config_prepare(task_pid());if(result<0)return result;}
    if(action==1 && fs_write(fixed,(const u8 *)config_text,(u32)length(config_text))!=length(config_text))return -3;
    if(kind==0)profile->user=user_candidate;else profile->environment=env_candidate;
    return 0;
}
void userspace_spawn(int pid,int parent)
{
    u32 generation=task_generation(pid);
    USER_CONTEXT(pid)=(user_context_t){0};USER_CONTEXT(pid).generation=generation;
    USER_CONTEXT(pid).parent=-1;
    if(parent>0 && parent<NTASK && auth_uid(pid)==auth_uid(parent) && session_task(pid)==session_task(parent)){
        copy(USER_CONTEXT(pid).cwd,USER_CONTEXT(parent).cwd,64);
        USER_CONTEXT(pid).env=USER_CONTEXT(parent).env;if(USER_CONTEXT(pid).env)USER_CONTEXT(pid).env->refs++;
    }
    else {
        u32 info[2];
        const char *home=auth_home(pid);
        if(!fs_stat(home,info) && info[0]==2)fs_normalize(home,USER_CONTEXT(pid).cwd);
    }
}
void userspace_executable(int pid,const char *path)
{fs_normalize(path,USER_CONTEXT(pid).executable);}
int userspace_query(int pid,int kind,const char *name,char *out,u32 capacity)
{
    if(pid<1 || !task_owned(pid) || !capacity || capacity>256)return -1;
    const char *value;char rooted[65];
    if(kind==0)value=auth_name(pid);
    else if(kind==1)value=auth_home(pid);
    else if(kind==2 || kind==3) {
        rooted[0]='/';copy(rooted+1,kind==2?USER_CONTEXT(pid).cwd:USER_CONTEXT(pid).executable,64);value=rooted;
    }else if(kind==4 && name) {
        if(equal(name,"USER"))value=auth_name(pid);
        else if(equal(name,"HOME")){rooted[0]='/';copy(rooted+1,auth_home(pid),64);value=rooted;}
        else if(equal(name,"PWD")){rooted[0]='/';copy(rooted+1,USER_CONTEXT(pid).cwd,64);value=rooted;}
        else {const env_config_t *env=USER_CONTEXT(pid).env?&USER_CONTEXT(pid).env->values:default_environment(pid);
            int found=env_find(env,name);if(found<0)return -2;value=env->items[found].value;}
    }else return -1;
    return copy(out,value,(int)capacity);
}
int userspace_resolve(int pid,const char *path,char *out,u32 capacity)
{
    if(pid<=0 || !task_owned(pid) || !path || !capacity || capacity>64)return -1;
    /* 中间连接串最多127B；输入不截断。先拼当前目录再解析..，因此
     * HOME/../BIN有明确含义，越过根则沿用SandFS的拒绝规则。返回
     * 不带根斜杠，直接可传旧FS接口，根目录以空字符串表示。 */
    char combined[128],normal[64];int at=0,n=length(path);
    if(path[0]!='/') {
        at=length(USER_CONTEXT(pid).cwd);copy(combined,USER_CONTEXT(pid).cwd,128);
        if(at && n)combined[at++]='/';
    }
    if(n+at>=128)return -1;
    copy(combined+at,path,128-at);
    if(fs_normalize(combined,normal)<0)return -1;
    return copy(out,normal,(int)capacity);
}
int userspace_chdir(int pid,const char *path)
{
    char normal[64];u32 info[2];
    if(userspace_resolve(pid,path,normal,64)<0)return -1;
    if(fs_stat(normal,info) || info[0]!=2)return -2;
    if(!fs_access(pid,normal,FS_ACCESS_READ))return -5;
    copy(USER_CONTEXT(pid).cwd,normal,64);return 0;
}
static int suffix(const char *path)
{
    int n=length(path);if(n<4 || path[n-4]!='.')return 0;
    for(int i=0;i<3;i++) {
        char c=path[n-3+i];if(c>='a' && c<='z')c=c-'a'+'A';
        if(c!="SCX"[i])return 0;
    }
    return 1;
}
static int executable(int parent,const char *name,char *out)
{
    char with_suffix[68];int n=length(name);
    if(!n || n>63)return -1;
    copy(with_suffix,name,68);
    if(!suffix(name)) {if(n+4>63)return -1;copy(with_suffix+n,".SCX",68-n);}
    int explicit=0;for(int i=0;i<n;i++)if(name[i]=='/')explicit=1;
    u32 info[2];
    if(explicit) {
        if(userspace_resolve(parent,with_suffix,out,64)<0)return -1;
        return fs_stat(out,info) || info[0]!=1?-2:0;
    }
    const env_config_t *env=USER_CONTEXT(parent).env?&USER_CONTEXT(parent).env->values:default_environment(parent);
    int index=env_find(env,"PATH");
    if(index<0)return -2;
    const char *path=env->items[index].value;
    while(*path) {
        char combined[128];int used=0;
        while(*path && *path!=':')combined[used++]=*path++;
        if(*path)path++;
        if(used && combined[used-1]!='/')combined[used++]='/';
        copy(combined+used,with_suffix,128-used);
        if(fs_normalize(combined,out)>=0 && !fs_stat(out,info) && info[0]==1)return 0;
    }
    return -2;
}
int userspace_cli_run(int parent,const char *command,int terminal)
{
    user_context_t *p=&USER_CONTEXT(parent);
    if(!wm_terminal_owned(parent,terminal))return -1;
    if(p->job_ticket && p->child>0)return -6;
    char name[64],path[64],launch[192];int n=0;
    while(*command==' ')command++;
    while(*command && *command!=' ') {if(n==63)return -1;name[n++]=*command++;}
    name[n]=0;while(*command==' ')command++;
    if(length(command)>127)return -1;
    int checked=executable(parent,name,path);if(checked)return checked;
    int at=copy(launch,path,192);
    if(*command){launch[at++]=' ';copy(launch+at,command,192-at);}
    int child=task_exec_scx(launch);if(child<1)return child;
    u32 ticket=(++next_ticket)&0x3FFFFFFFu;if(!ticket)ticket=(++next_ticket)&0x3FFFFFFFu;
    p->job_ticket=ticket;p->child=child;p->child_generation=USER_CONTEXT(child).generation;p->result=0;
    user_context_t *c=&USER_CONTEXT(child);
    c->parent=parent;c->parent_generation=p->generation;c->terminal=terminal;c->ticket=ticket;
    return (int)ticket;
}
int userspace_job_status(int parent,u32 ticket)
{
    user_context_t *p=&USER_CONTEXT(parent);
    if(!ticket || ticket!=p->job_ticket)return -1;
    if(!p->child)return p->result;
    int child=p->child;
    if(USER_CONTEXT(child).generation!=p->child_generation)return -1; /* 正常停止必须先交结果 */
    return TASK(child).state==3?0x40000001:0x40000000;
}
int userspace_puts(int pid,const char *text)
{
    user_context_t *c=&USER_CONTEXT(pid);int parent=c->parent;
    if(parent<1 || parent>=NTASK || TASK(parent).state!=1)return -1;
    user_context_t *p=&USER_CONTEXT(parent);
    if(c->parent_generation!=p->generation || c->ticket!=p->job_ticket
       || p->child!=pid || p->child_generation!=c->generation)return -1;
    return wm_terminal_puts(parent,c->terminal,text);
}
void userspace_stop(int pid,int code)
{
    user_context_t *c=&USER_CONTEXT(pid);int parent=c->parent;
    if(c->env){env_snapshot_t *env=c->env;c->env=0;if(!--env->refs)pframe_free_run((u32)env,(sizeof(*env)+4095)/4096);}
    if(parent>0 && parent<NTASK) {
        user_context_t *p=&USER_CONTEXT(parent);
        if(c->parent_generation==p->generation && c->ticket==p->job_ticket
           && p->child==pid && p->child_generation==c->generation) {
            p->result=code;p->child=0;
            task_notify_generation(parent,c->parent_generation,TASK_EVENT_JOB);
        }
    }
    c->parent=-1;c->ticket=0;
    /* 父任务停止只取消经CLIRUN附着的作业，原EXEC的GUI子进程不受
     * 影响。先撤授权再停止子进程，递归task_stop不会重复进入本父槽。 */
    int child=c->child;c->child=0;
    if(child>0 && USER_CONTEXT(child).generation==c->child_generation
       && USER_CONTEXT(child).parent==pid) {
        USER_CONTEXT(child).parent=-1;process_exit(child,130);task_stop(child);
    }
}
void userspace_terminal_closed(int owner,int handle)
{
    /* CLIRUN合同始终只有一个附着孩子，直接核对父子代数与句柄即可。
     * 多作业SPAWN2的终端能力另由streams撤销；不能在每关一窗扫全任务。 */
    if(!task_owned(owner))return;user_context_t *p=&USER_CONTEXT(owner);int child=p->child;
    if(child>0 && task_owned(child) && task_generation(child)==p->child_generation){
        user_context_t *c=&USER_CONTEXT(child);
        if(c->parent==owner && c->parent_generation==p->generation && c->terminal==handle){
            process_exit(child,130);task_stop(child);
        }
    }
}
int userspace_env_set(int pid,const char *name,const char *value,int remove)
{
    if(pid<1 || !task_owned(pid) || !identifier(name) || (remove!=0 && remove!=1)
        || (!remove && (!value || length(value)>255)))return -1;
    if(equal(name,"USER") || equal(name,"HOME") || equal(name,"PWD"))return -5;
    if(!remove && equal(name,"PATH") && path_value(value))return -1;
    user_context_t *c=&USER_CONTEXT(pid);
    const env_config_t *prior=c->env?&c->env->values:default_environment(pid);
    int index=env_find(prior,name);if(remove && index<0)return 0;
    if(!remove && index<0 && prior->count==16)return -4;
    if(!c->env || c->env->refs>1){
        env_snapshot_t *next=(env_snapshot_t *)pframe_alloc_run((sizeof(*next)+4095)/4096);if(!next)return -4;
        next->values=*prior;next->refs=1;if(c->env)c->env->refs--;c->env=next;
    }
    env_config_t *env=&c->env->values;
    if(remove){for(int i=index;i<env->count-1;i++)env->items[i]=env->items[i+1];env->count--;return 0;}
    if(index<0)index=env->count++;
    copy(env->items[index].name,name,32);copy(env->items[index].value,value,256);return 0;
}
int userspace_env_list(int pid,char *out,u32 capacity)
{
    const env_config_t *env=USER_CONTEXT(pid).env?&USER_CONTEXT(pid).env->values:default_environment(pid);u32 used=0;
    for(int i=0;i<env->count+3;i++){
        const char *name=i<env->count?env->items[i].name:i==env->count?"USER":i==env->count+1?"HOME":"PWD";
        if(i<env->count && (equal(name,"USER") || equal(name,"HOME") || equal(name,"PWD")))continue;
        char value[256];if(userspace_query(pid,4,name,value,sizeof(value))<0)continue;
        u32 a=(u32)length(name),b=(u32)length(value);if(a+b+2>=capacity-used)return -4;
        for(u32 k=0;k<a;k++)out[used++]=name[k];out[used++]='=';
        for(u32 k=0;k<b;k++)out[used++]=value[k];out[used++]='\n';
    }
    if(!capacity)return -1;out[used]=0;return (int)used;
}
