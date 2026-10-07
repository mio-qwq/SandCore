/* =====================================================================
 * mio：M8命名主题的验证、持久化和快照。
 *
 * 文件属于用户配置，不能边解析边改当前颜色，否则一条错字段会留
 * 半个新主题。候选放在短生命周期内核栈，文本缓冲固定4KB；所有路径、
 * RGB、重复字段验证结束后才提交。两遍解析解决style在后面的顺序问题。
 * 初始化阶段不触发桌面重载，运行期应用后通知合成器与快捷方式缓存。
 * ===================================================================== */
#include "theme.h"
#include "fs.h"
#include "gfx.h"
#include "wm.h"
#include "desktop.h"
#include "session.h"
#include "objpool.h"
#include "interrupts.h"
#include "task.h"

typedef struct {
    u32 colors[TH_ROLES],generation;
    int classic,radius;
    char name[32],iconroot[64],wallpaper[64];
} theme_t;
static objpool_t themes;
static theme_t emergency;
static theme_t *selected_theme;
static u32 selected_id;
static theme_t *theme_current(void);
#define active (*theme_current())
static char config[4096];
static const char *const keys[TH_ROLES]={
    "face","face_alt","paper","text","muted","line","accent","accent_deep",
    "select","select_text","shadow","alert","gold","green","violet","rose",
    "highlight","dark_edge","title_active","title_inactive","title_text","title_muted",
    "desktop_text","desktop_shadow"
};

static int same(const char *a,const char *b)
{ while(*a && *a==*b){a++;b++;}return *a==*b; }
static int copy_text(char *out,const char *text,int capacity)
{
    int n=0;
    while(text[n]){if(n+1>=capacity)return -1;out[n]=text[n];n++;}
    out[n]=0;return n;
}
static void defaults(theme_t *t,int classic)
{
    /* RGB来自GFX.md已登记的Aurora/经典控件角色。此数组是角色默认值
     * 的唯一入口，绘制代码只取角色；用户配置覆盖不影响旧槽位。 */
    static const u32 aurora[TH_ROLES]={
        0xFFFFFF,0xE6F3F9,0xFBFCFE,0x173247,0x5F788C,0xD7E6ED,
        0x147F9F,0x125E78,0xCAE6F0,0x173247,0x17384D,0xDC798A,
        0xEAC470,0x4EA580,0x817DD3,0xDC798A,0xFFFFFF,0x5F788C,
        0xFFFFFF,0xE6F3F9,0x173247,0x5F788C,0xFFFFFF,0x17384D
    };
    static const u32 retro[TH_ROLES]={
        0xC0C0C0,0xD9DED3,0xFFFFFF,0x000000,0x808080,0x808080,
        0x000080,0x1084D0,0x000080,0xFFFFFF,0x000000,0xC67461,
        0xFFD17F,0x008080,0xAE5595,0xC67461,0xFFFFFF,0x808080,
        0x000080,0x808080,0xFFFFFF,0xD9DED3,0xFFFFFF,0x000000
    };
    *t=(theme_t){0};t->classic=classic;t->radius=classic?0:10;
    for(int i=0;i<TH_ROLES;i++)t->colors[i]=(classic?retro:aurora)[i];
    copy_text(t->name,classic?"Classic":"Aurora",32);
    copy_text(t->iconroot,classic?"SYS/ICONS/CLASSIC":"SYS/ICONS/AURORA",64);
}
static char *trim(char *text)
{
    while(*text==' ' || *text=='\t')text++;
    int n=0;while(text[n])n++;
    while(n && (text[n-1]==' ' || text[n-1]=='\t' || text[n-1]=='\r'))text[--n]=0;
    return text;
}
static int hex_rgb(const char *value,u32 *out)
{
    if(*value=='#')value++;
    u32 rgb=0;
    for(int i=0;i<6;i++){
        int c=(u8)value[i],digit;
        if(c>='0' && c<='9')digit=c-'0';
        else if(c>='a' && c<='f')digit=c-'a'+10;
        else if(c>='A' && c<='F')digit=c-'A'+10;
        else return -1;
        rgb=(rgb<<4)|(u32)digit;
    }
    if(value[6])return -1;
    *out=rgb;return 0;
}
static int normalized(char *out,const char *value)
{
    if(!*value){out[0]=0;return 0;}
    /* fs_normalize成功返回正文长度，这里统一成0成功；不能把合法
     * 非空路径的正长度误当错误码。失败仍在提交快照前整体拒绝。 */
    return fs_normalize(value,out)<0?-1:0;
}
static int parse(theme_t *out,int bytes)
{
    if(bytes<10 || !gfx_utf8_valid(config))return -1;
    /* 在本地行副本里分割，不破坏config：持久化写回的是原始完整文本，
     * 而不是首个等号/换行被改成NUL之后的残片。每行至多127B。 */
    int classic=0;
    for(int pass=0;pass<2;pass++){
        const char *p=config;int first=1;u32 seen=0;
        if(pass)defaults(out,classic);
        while(*p){
            char row[128];int n=0;
            while(*p && *p!='\n'){if(n==127)return -1;row[n++]=*p++;}
            if(*p)p++;
            row[n]=0;char *key=trim(row);
            if(first){first=0;if(!same(key,"STHEME1MIO"))return -1;continue;}
            if(!*key || *key=='#')continue;
            char *v=key;while(*v && *v!='=')v++;
            if(!*v)return -1;
            *v++=0;key=trim(key);v=trim(v);
            for(const char *c=v;*c;c++)if((u8)*c<32)return -1;
            int id=0;while(id<TH_ROLES && !same(key,keys[id]))id++;
            if(id==TH_ROLES){
                if(same(key,"style"))id=24;
                else if(same(key,"name"))id=25;
                else if(same(key,"iconroot"))id=26;
                else if(same(key,"wallpaper"))id=27;
                else if(same(key,"radius"))id=28;
                else return -1;
            }
            if(seen&(1u<<id))return -1;
            seen|=1u<<id;
            if(id==24){
                if(same(v,"classic"))classic=1;
                else if(same(v,"aurora"))classic=0;
                else return -1;
            }
            if(!pass)continue;
            if(id<TH_ROLES){if(hex_rgb(v,&out->colors[id]))return -1;}
            else if(id==25){if(!*v || copy_text(out->name,v,32)<0)return -1;}
            else if(id==26){if(normalized(out->iconroot,v))return -1;}
            else if(id==27){if(normalized(out->wallpaper,v))return -1;}
            else if(id==28){
                int radius=0;
                if(!*v)return -1;
                while(*v){if(*v<'0'||*v>'9')return -1;radius=radius*10+*v++-'0';if(radius>20)return -1;}
                out->radius=radius;
            }
        }
    }
    return 0;
}
static int read_candidate(const char *path,theme_t *out)
{
    session_t *s=session_get(session_current());
    if(!s || !fs_access_identity(path,s->uid,s->gid,FS_ACCESS_READ))return -2;
    u32 info[2];
    if(fs_stat(path,info) || info[0]!=1 || !info[1] || info[1]>=sizeof(config))return -2;
    int n=fs_read(path,config,sizeof(config)-1);
    if(n<0 || (u32)n!=info[1])return -2;
    for(int i=0;i<n;i++)if(!config[i])return -1; /* 内嵌NUL不能隐藏尾部坏字段 */
    config[n]=0;
    return parse(out,n);
}
void theme_init(void)
{
    defaults(&emergency,0);emergency.generation=1;
    if(objpool_init(&themes,sizeof(theme_t),0) || theme_session_prepare(session_active()))panic("THEME MEMORY",0);
}
int theme_session_prepare(u32 id)
{
    if(objpool_get(&themes,(int)id))return 0;
    if(!session_get(id) || objpool_claim(&themes,(int)id))return -4;
    theme_t *theme=objpool_get(&themes,(int)id),candidate;defaults(theme,0);
    char path[64];
    session_t *s=session_get(id);
    /* 候选初始化可能为隐藏会话准备；权限按该会话身份，而非任务0。 */
    if(session_config_path_for(id,"THEME.CFG",path)>=0 && fs_access_identity(path,s->uid,s->gid,FS_ACCESS_READ)){
        u32 info[2];int n=fs_stat(path,info)?-1:fs_read(path,config,sizeof(config)-1);
        if(n>0 && (u32)n==info[1] && info[1]<sizeof(config)){
            int valid=1;for(int i=0;i<n;i++)if(!config[i])valid=0;
            config[n]=0;if(valid && !parse(&candidate,n))*theme=candidate;
        }
    }
    theme->generation=1;return 0;
}
static theme_t *theme_current(void)
{
    u32 id=session_current();if(selected_theme && selected_id==id)return selected_theme;
    if(!themes.stride || theme_session_prepare(id))return &emergency;
    selected_id=id;selected_theme=objpool_get(&themes,(int)id);return selected_theme;
}
void theme_session_destroy(u32 id){if(selected_id==id){selected_theme=0;selected_id=0;}if(themes.stride)objpool_release(&themes,(int)id);}
int theme_check(const char *path)
{theme_t candidate;return read_candidate(path,&candidate);}
int theme_load(const char *path,int action)
{
    if(action<0 || action>2 || (action!=2 && !path))return -1;
    char saved[64];if(session_config_path("THEME.CFG",saved)<0)return -5;
    if(theme_session_prepare(session_current()))return -4;
    theme_t candidate;
    int result=read_candidate(action==2?saved:path,&candidate);
    if(result)return result;
    if(action==1){
        int prepared=session_config_prepare(task_pid());if(prepared<0)return prepared;
        if(!fs_user_mutable(saved,0) || !fs_access(task_pid(),saved,FS_ACCESS_WRITE))return -5;
        u32 n=0;while(config[n])n++;
        if(fs_write(saved,(const u8 *)config,n)!=(int)n)return -3;
    }
    candidate.generation=active.generation+1;
    if(!candidate.generation)candidate.generation=1;
    active=candidate;
    desktop_reload();wm_session_repaint(session_current());
    return 0;
}
void theme_info(u32 *out)
{
    out[0]=1;out[1]=(u32)active.classic;out[2]=active.generation;
    out[3]=24;out[4]=32;out[5]=(u32)theme_radius();out[6]=1;out[7]=0;
    for(int i=0;i<TH_ROLES;i++)out[8+i]=active.colors[i];
}
int theme_path(int kind,char *out,u32 capacity)
{
    if(kind<0 || kind>2 || !capacity || capacity>64)return -1;
    const char *value=kind==0?active.wallpaper:kind==1?active.iconroot:active.name;
    u32 n=0;while(value[n])n++;
    if(n>=capacity)return -1;
    for(u32 i=0;i<=n;i++)out[i]=value[i];
    return (int)n;
}
u32 theme_color(int role){return role>=0 && role<TH_ROLES?active.colors[role]:0;}
int theme_classic(void){return active.classic;}
int theme_radius(void){return active.classic?0:active.radius;}
