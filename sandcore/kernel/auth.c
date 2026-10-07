#include "auth.h"
#include "task.h"
#include "fs.h"
#include "crypto.h"
#include "random.h"
#include "timer.h"
#include "process.h"
#include "streams.h"
#include "session.h"

#define PASSWORD_ITERATIONS 100000u
#define USER_ENABLED 1u
#define USER_PASSWORD 2u
#define AUTH_DB "SYS/AUTH/USERS.DAT"
typedef struct {char name[32],home[64];i32 uid,gid;u8 salt[16],hash[32];u32 flags,reserved;} user_t;
typedef struct {i32 uid,gid,subject_uid,subject_gid;u32 realm,session,generation;} credential_t;
typedef struct {
    u32 ticket,generation,created,state,action,database_generation;
    i32 target;pbkdf256_t derive;u8 salt[16];
    int queue_prev,queue_next,queued;
} login_t;
static user_t users[AUTH_USER_MAX];
static u32 user_count,database_generation;
static u8 database[32+AUTH_USER_MAX*sizeof(user_t)],boot_token[32];
static credential_t empty_credentials;
static credential_t *credentials_at(int pid)
{void *p=task_data(pid,TASK_DATA_AUTH);return p?p:&empty_credentials;}
#define CREDENTIAL(pid) (*credentials_at(pid))
u32 auth_task_bytes(void){return sizeof(credential_t);}
static credential_t launch;
static login_t empty_logins;
static login_t *logins_at(int pid)
{void *p=task_data(pid,TASK_DATA_LOGIN);return p?p:&empty_logins;}
#define LOGIN(pid) (*logins_at(pid))
u32 auth_login_bytes(void){return sizeof(login_t);}
static u32 launch_set,launch_desktop,serial_session,serial_counter,poll_tick;
static int login_head=-1,login_tail=-1;
static i32 active_uid=AUTH_DEFAULT,active_gid=AUTH_DEFAULT;
static u32 next_attempt[AUTH_USER_MAX+1];

static void login_unqueue(int pid)
{
    login_t *j=task_data(pid,TASK_DATA_LOGIN);if(!j || !j->queued)return;
    if(j->queue_prev>=0)LOGIN(j->queue_prev).queue_next=j->queue_next;else login_head=j->queue_next;
    if(j->queue_next>=0)LOGIN(j->queue_next).queue_prev=j->queue_prev;else login_tail=j->queue_prev;
    j->queued=0;j->queue_prev=j->queue_next=-1;
}
static void login_clear(int pid)
{login_unqueue(pid);crypto_zero(&LOGIN(pid),sizeof(login_t));}
static void login_enqueue(int pid)
{
    login_t *j=&LOGIN(pid);j->queue_prev=login_tail;j->queue_next=-1;j->queued=1;
    if(login_tail>=0)LOGIN(login_tail).queue_next=pid;else login_head=pid;login_tail=pid;
}

static char fold(char c){return c>='a' && c<='z'?c-32:c;}
static int same(const char *a,const char *b)
{while(*a && fold(*a)==fold(*b)){a++;b++;}return fold(*a)==fold(*b);}
static u32 length(const char *s,u32 limit)
{u32 n=0;while(n<limit && s[n])n++;return n;}
static void copy(char *out,const char *in,u32 cap)
{u32 i=0;while(i+1<cap && in[i]){out[i]=in[i];i++;}out[i]=0;}
static int user_index(int uid)
{for(u32 i=0;i<user_count;i++)if(users[i].uid==uid)return (int)i;return -1;}
static int named(const char *name)
{for(u32 i=0;i<user_count;i++)if(same(users[i].name,name))return (int)i;return -1;}
static int valid_name(const char *name)
{
    u32 n=length(name,32);if(!n || n==32 || same(name,"SYSTEM"))return 0;
    for(u32 i=0;i<n;i++)if(!((name[i]>='a' && name[i]<='z') || (name[i]>='A' && name[i]<='Z')
        || (name[i]>='0' && name[i]<='9') || name[i]=='_' || name[i]=='-'))return 0;
    return 1;
}
static int save(void)
{
    crypto_zero(database,sizeof(database));copy((char *)database,"SCUSR1",8);
    *(u32 *)(database+8)=1;*(u32 *)(database+12)=user_count;
    *(u32 *)(database+16)=database_generation;*(u32 *)(database+20)=PASSWORD_ITERATIONS;
    *(u32 *)(database+24)=0x004F494Du;
    for(u32 i=0;i<user_count;i++)for(u32 j=0;j<sizeof(user_t);j++)
        database[32+i*sizeof(user_t)+j]=((u8 *)&users[i])[j];
    u32 bytes=32+user_count*sizeof(user_t);
    int result=fs_write(AUTH_DB,database,bytes)==(int)bytes?0:-1;
    crypto_zero(database,sizeof(database));return result;
}
static void defaults(void)
{
    crypto_zero(users,sizeof(users));user_count=2;database_generation=1;
    copy(users[0].name,"root",32);copy(users[0].home,"HOME/ROOT",64);
    users[0].uid=users[0].gid=0;users[0].flags=USER_ENABLED;
    copy(users[1].name,"mio",32);copy(users[1].home,"HOME",64);
    users[1].uid=users[1].gid=AUTH_DEFAULT;users[1].flags=USER_ENABLED;
    /* 默认账户没有可登录密码。GUI按普通mio身份启动，root首次密码
     * 由外部串口SYSTEM设置；不安装公开默认root口令或SYSTEM密码。 */
}
void auth_init(void)
{
    defaults();crypto_zero(&CREDENTIAL(0),sizeof(credential_t));crypto_zero(&LOGIN(0),sizeof(login_t));
    serial_session=serial_counter=launch_set=0;poll_tick=0xFFFFFFFF;login_head=login_tail=-1;
    crypto_zero(next_attempt,sizeof(next_attempt));
    random_init();crypto_zero(boot_token,sizeof(boot_token));
    if(random_ready())(void)random_bytes(boot_token,sizeof(boot_token));
    u32 metadata[2];int database_exists=fs_stat(AUTH_DB,metadata)==0,loaded=0;
    int bytes=fs_read(AUTH_DB,database,sizeof(database));
    if(bytes>=32 && crypto_equal(database,(const u8 *)"SCUSR1\0",8)
        && *(u32 *)(database+8)==1 && *(u32 *)(database+24)==0x004F494Du
        && *(u32 *)(database+20)==PASSWORD_ITERATIONS){
        u32 count=*(u32 *)(database+12);int has_default=0,valid=count>=2 && count<=AUTH_USER_MAX
            && (u32)bytes==32+count*sizeof(user_t);
        user_t *rows=(user_t *)(database+32);
        for(u32 i=0;i<count && valid;i++){
            if(rows[i].uid==AUTH_DEFAULT)has_default=1;
            if(!rows[i].name[0] || rows[i].name[31] || rows[i].home[63]
                || rows[i].uid<0 || rows[i].gid<0 || !valid_name(rows[i].name)
                || (rows[i].flags&~3u))valid=0;
            char normalized[64];if(fs_normalize(rows[i].home,normalized)<=0)valid=0;
            for(u32 j=0;j<i;j++)if(rows[i].uid==rows[j].uid || same(rows[i].name,rows[j].name))valid=0;
        }
        if(valid && has_default && rows[0].uid==0 && rows[0].gid==0 && same(rows[0].name,"root")){
            for(u32 i=0;i<count;i++)users[i]=rows[i];user_count=count;
            database_generation=*(u32 *)(database+16);if(!database_generation)database_generation=1;
            loaded=1;
        }
        /* 坏账户库不恢复公开默认密码；默认root仍锁住，由硬件管理恢复。 */
    }
    /* 已存在的坏库不能偷偷恢复默认mio的组权限。无账户库是首次安装，
     * 才使用默认身份；坏库/默认用户被禁用则桌面任务取无权限哨兵，
     * 普通文件入口拒绝它，恢复仅由外部SYSTEM重新保存账户库后重启。 */
    int default_row=user_index(AUTH_DEFAULT);
    if(database_exists && !loaded){active_uid=active_gid=-2;}
    else if(default_row<0 || !(users[default_row].flags&USER_ENABLED)){active_uid=active_gid=-2;}
    else {active_uid=users[default_row].uid;active_gid=users[default_row].gid;}
    crypto_zero(database,sizeof(database));
    CREDENTIAL(0).uid=CREDENTIAL(0).gid=AUTH_SYSTEM;
    CREDENTIAL(0).subject_uid=CREDENTIAL(0).subject_gid=AUTH_SYSTEM;
    CREDENTIAL(0).realm=AUTH_KERNEL;CREDENTIAL(0).generation=task_generation(0);
    session_init(active_uid,active_gid,default_row>=0?users[default_row].name:"Locked",default_row>=0?users[default_row].home:"SYS");
}
static credential_t *credential(int pid)
{
    if(pid<0 || pid>=NTASK || !TASK(pid).state || TASK(pid).state==2
        || CREDENTIAL(pid).generation!=task_generation(pid))return 0;
    return &CREDENTIAL(pid);
}
void auth_spawn(int pid,int parent)
{
    login_clear(pid);
    if(launch_set)CREDENTIAL(pid)=launch;
    else if(parent>0 && credential(parent))CREDENTIAL(pid)=CREDENTIAL(parent);
    else {
        crypto_zero(&CREDENTIAL(pid),sizeof(CREDENTIAL(pid)));
        session_t *s=session_get(session_active());
        CREDENTIAL(pid).uid=CREDENTIAL(pid).subject_uid=s?s->uid:-2;
        CREDENTIAL(pid).gid=CREDENTIAL(pid).subject_gid=s?s->gid:-2;
    }
    CREDENTIAL(pid).generation=task_generation(pid);
    (void)session_bind(pid,launch_set?launch_desktop:parent>0?session_task(parent):session_active());
}
void auth_stop(int pid)
{if(pid>0 && task_owned(pid)){crypto_zero(&CREDENTIAL(pid),sizeof(CREDENTIAL(pid)));login_clear(pid);session_unbind(pid);}}
int auth_uid(int pid){credential_t *c=credential(pid);return c?c->uid:-2;}
int auth_gid(int pid){credential_t *c=credential(pid);return c?c->gid:-2;}
int auth_subject_uid(int pid){credential_t *c=credential(pid);return c?c->subject_uid:-2;}
int auth_subject_gid(int pid){credential_t *c=credential(pid);return c?c->subject_gid:-2;}
const char *auth_name(int pid)
{int uid=auth_uid(pid),i=user_index(uid);return uid==AUTH_SYSTEM?"SYSTEM":i>=0?users[i].name:"unknown";}
const char *auth_home(int pid)
{int i=user_index(auth_uid(pid));return i>=0?users[i].home:"SYS";}
int auth_can_mod(int pid)
{
    credential_t *c=credential(pid);
    return c && c->uid==AUTH_SYSTEM && c->realm==AUTH_SERIAL && serial_session
        && c->session==serial_session && random_ready();
}
int auth_can_manage(int pid){return auth_uid(pid)==AUTH_ROOT || auth_can_mod(pid);}
int auth_info(int pid,u32 out[8])
{
    credential_t *c=credential(pid);if(!c)return -1;
    for(u32 i=0;i<8;i++)out[i]=0;out[0]=1;out[1]=(u32)c->uid;out[2]=(u32)c->gid;
    out[3]=c->realm;out[4]=auth_can_mod(pid);out[5]=c->generation;return 0;
}
static u32 ticket(void)
{
    for(u32 tries=0;tries<16;tries++){
        u32 result;if(random_bytes(&result,4))return 0;result&=0x7FFFFFFF;if(!result)continue;
        int duplicate=0;for(int i=task_next(0);i>=0;i=task_next(i))if(LOGIN(i).ticket==result)duplicate=1;
        if(!duplicate)return result;
    }
    return 0;
}
static login_t *job(int pid,u32 token)
{
    if(!credential(pid) || !token || LOGIN(pid).ticket!=token
        || LOGIN(pid).generation!=task_generation(pid))return 0;
    if(sc_ticks-LOGIN(pid).created>60000u || LOGIN(pid).database_generation!=database_generation){
        login_clear(pid);return 0;
    }
    return &LOGIN(pid);
}
static int begin(int pid,int target,const char *password,u32 action)
{
    if(!credential(pid) || !random_ready() || !password || length(password,129)>128)return -1;
    login_clear(pid);login_t *j=&LOGIN(pid);
    j->ticket=ticket();if(!j->ticket)return -1;
    j->target=target;j->action=action;j->created=sc_ticks;j->generation=task_generation(pid);
    j->database_generation=database_generation;j->state=1;
    if(action){if(random_bytes(j->salt,16)){login_clear(pid);return -1;}}
    else for(u32 i=0;i<16;i++)j->salt[i]=target>=0?users[target].salt[i]:boot_token[i];
    if(!action && auth_can_manage(pid)){j->state=target>=0?2:3;return (int)j->ticket;}
    pbkdf256_init(&j->derive,password,length(password,129),j->salt,PASSWORD_ITERATIONS);
    login_enqueue(pid);
    return (int)j->ticket;
}
int auth_login(int pid,const char *name,const char *password)
{
    if(!valid_name(name) || !credential(pid))return -5;
    int target=named(name),slot=user_index(auth_uid(pid));if(slot<0)slot=AUTH_USER_MAX;
    if((i32)(sc_ticks-next_attempt[slot])<0)return -6;
    next_attempt[slot]=sc_ticks+200;
    return begin(pid,target,password,0);
}
int auth_status(int pid,u32 token,u32 out[8])
{
    login_t *j=job(pid,token);if(!j)return -1;
    for(u32 i=0;i<8;i++)out[i]=0;out[0]=1;out[1]=j->state;out[2]=j->derive.done;
    out[3]=j->derive.total;out[4]=j->target>=0?(u32)users[j->target].uid:0xFFFFFFFEu;
    out[5]=j->action;return j->state==1?AUTH_PENDING:j->state==2?0:-5;
}
int auth_cancel(int pid,u32 token)
{if(!job(pid,token))return -1;login_clear(pid);return 0;}
static int execute(const char *command,int uid,int gid,u32 realm,u32 session,int subject_uid,int subject_gid,u32 desktop)
{
    if(launch_set)return -1;
    crypto_zero(&launch,sizeof(launch));launch.uid=uid;launch.gid=gid;launch.realm=realm;
    launch.session=session;launch.subject_uid=subject_uid;launch.subject_gid=subject_gid;
    launch_desktop=desktop;launch_set=1;int result=task_exec_scx(command);launch_set=launch_desktop=0;crypto_zero(&launch,sizeof(launch));return result;
}
int auth_exec_ticket(int pid,u32 token,const char *command,int activate)
{
    login_t *j=job(pid,token);
    if(!j || j->state!=2 || j->action || j->target<0 || !(users[j->target].flags&USER_ENABLED))return -5;
    user_t *u=&users[j->target];
    u32 desktop=session_create(u->uid,u->gid,SESSION_NORMAL,u->name,u->home);if(!desktop)return -4;
    if(activate){int result=session_switch_trusted(desktop);if(result)session_discard(desktop);return result;}
    credential_t *parent=credential(pid);u32 session=parent?parent->session:0;
    int child=execute(command,u->uid,u->gid,AUTH_NORMAL,session,u->uid,u->gid,desktop);
    if(child>0)streams_login_terminal(pid,child);else session_discard(desktop);return child;
}
u32 auth_serial_begin(void)
{
    if(!random_ready())return 0;
    serial_counter++;if(!serial_counter)serial_counter=1;serial_session=serial_counter;return serial_session;
}
void auth_serial_end(void)
{
    u32 previous=serial_session;serial_session=0;
    if(!previous)return;
    /* 断开先撤授权再停止管理链进程；常驻模块仍按用户合同留到重启。 */
    for(int pid=task_next(0);pid>=0;pid=task_next(pid))if(CREDENTIAL(pid).session==previous && TASK(pid).state){
        process_exit(pid,130);task_stop(pid);
    }
}
int auth_serial_exec(u32 session,const char *command)
{
    if(!session || session!=serial_session || !random_ready())return -5;
    return execute(command,AUTH_SYSTEM,AUTH_SYSTEM,AUTH_SERIAL,session,AUTH_SYSTEM,AUTH_SYSTEM,session_system());
}
int auth_service_exec(const char *command,int delegate)
{
    /* 服务只能取核心目录的受保护程序。即使其进程UID是SYSTEM，文件
     * 访问也按请求者subject判断，不能把图片服务变成读取私密文件的代理。 */
    char normalized[64],path[64];u32 n=0;
    while(command[n] && command[n]!=' ' && n<63){path[n]=command[n];n++;}path[n]=0;
    if(fs_normalize(path,normalized)<=0)return -1;
    const char *prefix="SYS/CORE/";for(n=0;prefix[n];n++)if(fold(normalized[n])!=prefix[n])return -5;
    int uid=delegate>0?auth_subject_uid(delegate):AUTH_SYSTEM;
    int gid=delegate>0?auth_subject_gid(delegate):AUTH_SYSTEM;if(uid<-1)return -5;
    return execute(command,AUTH_SYSTEM,AUTH_SYSTEM,AUTH_SERVICE,0,uid,gid,session_system());
}
int auth_login_screen_exec(u32 desktop)
{
    session_t *s=session_get(desktop);if(!s || s->kind!=SESSION_LOCKED)return -1;
    /* 仅此固定登录程序显示在锁屏会话。服务UID不带串口授权；文件
     * 主体为无权限哨兵，不能借登录窗读写其它用户文档或执行任意程序。 */
    return execute("SYS/CORE/LOGIN.SCX",AUTH_SYSTEM,AUTH_SYSTEM,AUTH_SERVICE,0,-2,-2,desktop);
}
int auth_launch_access(const char *path,u32 access)
{
    int uid,gid;
    if(launch_set){uid=launch.uid;gid=launch.gid;}
    else if(task_pid()>0){uid=auth_subject_uid(task_pid());gid=auth_subject_gid(task_pid());}
    else {session_t *s=session_get(session_active());uid=s?s->uid:-2;gid=s?s->gid:-2;}
    return fs_access_identity(path,uid,gid,access);
}
int auth_network_exec(int pid,const char *command,const i32 fds[3])
{
    credential_t *parent=credential(pid);
    if(!parent || launch_set || !streams_pipe_descriptors(pid,fds))return -13;
    /* nc -e新进程有独立网络启动合同，旧SPAWN2继承语义不改。
     * 普通账户/root取本人文件主体；外部SYSTEM显式启动时落为root。
     * 服务只能取其受委托的普通主体，不能以SYSTEM服务伪造root。
     * 无UART端点/外部会话票据，也不能在remote子孙重新得到AUTH_SERIAL。
     * task_exec与三个标准描述符接线全在IF=0内，孩子不会抢先运行。 */
    int uid=parent->subject_uid,gid=parent->subject_gid;
    if(uid==AUTH_SYSTEM){if(!auth_can_mod(pid))return -13;uid=gid=AUTH_ROOT;}
    if(uid<0 || gid<0)return -13;
    crypto_zero(&launch,sizeof(launch));launch.uid=launch.subject_uid=uid;launch.gid=launch.subject_gid=gid;
    launch.realm=AUTH_NORMAL;launch_desktop=session_task(pid);launch_set=1;
    int result=streams_exec(pid,command,fds,0);
    launch_set=launch_desktop=0;crypto_zero(&launch,sizeof(launch));return result;
}
int auth_user_add(int pid,const char *name,const char *home,int uid,int gid)
{
    if(!auth_can_manage(pid))return -5;
    char normalized[64];if(!valid_name(name) || named(name)>=0 || uid<1 || gid<0
        || user_index(uid)>=0 || user_count==AUTH_USER_MAX || fs_normalize(home,normalized)<=0)return -1;
    user_t *u=&users[user_count];crypto_zero(u,sizeof(*u));
    copy(u->name,name,32);copy(u->home,normalized,64);u->uid=uid;u->gid=gid;u->flags=USER_ENABLED;
    user_count++;database_generation++;
    if(save()){user_count--;database_generation--;crypto_zero(u,sizeof(*u));return -2;}
    return 0;
}
int auth_user_remove(int pid,const char *name)
{
    if(!auth_can_manage(pid))return -5;
    int i=named(name);if(i<0 || users[i].uid==AUTH_ROOT || users[i].uid==AUTH_DEFAULT)return -1;
    /* 禁用保留UID和名称，旧文件所有者不被新的同名/同UID用户接管。 */
    u32 old=users[i].flags;users[i].flags=0;database_generation++;
    if(save()){users[i].flags=old;database_generation--;return -2;}return 0;
}
int auth_password(int pid,const char *name,const char *password,u32 proof)
{
    int i=named(name);if(i<0 || !password || !length(password,129) || length(password,129)>128)return -1;
    if(!auth_can_manage(pid)){
        login_t *j=job(pid,proof);
        if(!j || j->state!=2 || j->action || j->target!=i || users[i].uid!=auth_uid(pid))return -5;
    }
    return begin(pid,i,password,1);
}
void auth_poll(void)
{
    if(poll_tick==sc_ticks)return;poll_tick=sc_ticks;
    /* 只轮转真正进行派生的登录请求；空闲桌面有几千个任务时也不
     * 扫几千份凭据。每tick总计256次HMAC，失效项清理另限8个。 */
    for(int discarded=0;login_head>=0 && discarded<8;discarded++){
        int pid=login_head;login_t *j=&LOGIN(pid);
        if(j->state!=1 || !job(pid,j->ticket)){login_clear(pid);continue;}
        login_unqueue(pid);
        if(!pbkdf256_step(&j->derive,256)){login_enqueue(pid);return;}
        if(j->action){
            user_t old=users[j->target];
            for(u32 n=0;n<16;n++)users[j->target].salt[n]=j->salt[n];
            for(u32 n=0;n<32;n++)users[j->target].hash[n]=j->derive.value[n];
            users[j->target].flags|=USER_PASSWORD;database_generation++;
            if(save()){users[j->target]=old;database_generation--;j->state=3;}
            else {j->database_generation=database_generation;j->state=2;}
            crypto_zero(&old,sizeof(old));
        }else j->state=j->target>=0 && (users[j->target].flags&3)==3
            && crypto_equal(j->derive.value,users[j->target].hash,32)?2:3;
        crypto_zero(&j->derive,sizeof(j->derive));crypto_zero(j->salt,sizeof(j->salt));task_notify_generation(pid,j->generation,TASK_EVENT_AUTH);return;
    }
}
int auth_users(int pid,char *out,u32 capacity)
{
    if(!auth_can_manage(pid))return -5;
    u32 used=0;if(!capacity)return -1;
    for(u32 i=0;i<user_count;i++){
        u32 n=length(users[i].name,32),m=length(users[i].home,64);char number[12];u32 v=(u32)users[i].uid,k=0;
        do{number[k++]=(char)('0'+v%10);v/=10;}while(v);
        if(used+n+m+k+4>=capacity)break;
        for(u32 j=0;j<n;j++)out[used++]=users[i].name[j];out[used++]=' ';
        while(k)out[used++]=number[--k];out[used++]=' ';
        for(u32 j=0;j<m;j++)out[used++]=users[i].home[j];out[used++]='\n';
    }
    out[used]=0;return (int)used;
}
