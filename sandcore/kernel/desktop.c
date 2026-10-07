/* mio：配置全量解析到候选表，成功才替换。用户把文件写了一半、字段
 * 超长或包含未知格式时，已经可用的菜单不能消失；返回错误给设置。
 * 图标资源在任务0合成阶段懒加载为原尺寸ARGB，不再缩成16×16。
 * 重载只标记缓存失效，大图片IO不留在关中断的主题系统调用里。
 * 字体仍由独立系统字库提供，绝不将图标点阵作为替代汉字字体。 */
#include "desktop.h"
#include "fs.h"
#include "gfx.h"
#include "palette.h"
#include "image.h"
#include "theme.h"
#include "memory.h"
#include "display.h"
#include "image_service.h"
#include "session.h"
#include "objpool.h"
#include "auth.h"
#include "task.h"
#include "timer.h"
#include "wm.h"
static desk_item_t candidate;
static char text[8192];
typedef struct {char path[64];u32 offset,length,touch;image_surface_t surface;image_reader_t reader;int used;} icon_cache_t;
typedef struct {
    desk_item_t menu_items[32],icon_items[24];
    int menu_count,icon_count;
    icon_cache_t images[32];
    image_surface_t background,scaled_background;
    image_reader_t background_reader;
    u32 columns[1920],clock,pages,ticket,io_cursor;
    int map_width,map_height,needs_flush,attempted,error,loaded;
    char path[64];
} desktop_state_t;
typedef struct {desktop_state_t *state;u32 pages;} desktop_slot_t;
static objpool_t desktops;
static desktop_state_t emergency;
static desktop_state_t *selected_state;
static u32 selected_id;
static u32 context_override;
/* 只记录公开成本，不保存文件正文。候选页计入所属会话的pages，
 * 全局累计读取与批次最大PIT跨度用来验证冷加载服务预算。 */
u32 desktop_io_bytes,desktop_io_batches,desktop_io_ticks_max,desktop_io_pending,desktop_io_session;
static u32 desktop_context(void){return context_override?context_override:session_current();}
static desktop_state_t *desktop_current(void);
#define menus (desktop_current()->menu_items)
#define icons (desktop_current()->icon_items)
#define nmenus (desktop_current()->menu_count)
#define nicons (desktop_current()->icon_count)
#define cache (desktop_current()->images)
#define wall (desktop_current()->background)
#define wall_render (desktop_current()->scaled_background)
#define wall_columns (desktop_current()->columns)
#define wall_map_width (desktop_current()->map_width)
#define wall_map_height (desktop_current()->map_height)
#define wall_path (desktop_current()->path)
#define cache_clock (desktop_current()->clock)
/* 诊断统计：常驻资源与用户窗口有不同生命周期。切主题会合法改变
 * 这里的页数，退出验收必须扣除这一变化再判断应用是否泄漏。 */
#define desktop_pages (desktop_current()->pages)
#define cache_dirty (desktop_current()->needs_flush)
#define wall_attempted (desktop_current()->attempted)
#define wall_ticket (desktop_current()->ticket)
#define wall_error (desktop_current()->error)
int desktop_session_prepare(u32 id)
{
    if(!desktops.stride && objpool_init(&desktops,sizeof(desktop_slot_t),0))return -4;
    if(objpool_get(&desktops,(int)id))return 0;
    if(!session_get(id) || objpool_claim(&desktops,(int)id))return -4;
    desktop_slot_t *slot=objpool_get(&desktops,(int)id);slot->pages=(sizeof(desktop_state_t)+4095)/4096;
    slot->state=(desktop_state_t *)pframe_alloc_run(slot->pages);
    if(!slot->state){objpool_release(&desktops,(int)id);return -4;}
    u32 *p=(u32 *)slot->state;for(u32 i=0;i<slot->pages*1024;i++)p[i]=0;
    slot->state->needs_flush=1;return 0;
}
static desktop_state_t *desktop_current(void)
{
    u32 id=desktop_context();if(selected_state && selected_id==id)return selected_state;
    if(desktop_session_prepare(id))return &emergency;
    selected_id=id;selected_state=((desktop_slot_t *)objpool_get(&desktops,(int)id))->state;return selected_state;
}
void desktop_session_destroy(u32 id)
{
    desktop_slot_t *slot=desktops.stride?objpool_get(&desktops,(int)id):0;if(!slot)return;
    if(selected_id==id){selected_state=0;selected_id=0;}
    desktop_state_t *state=slot->state;
    for(int i=0;i<32;i++){image_reader_cancel(&state->images[i].reader);image_release(&state->images[i].surface);}
    image_reader_cancel(&state->background_reader);
    if(state->ticket)image_service_cancel(0,state->ticket);
    image_release(&state->background);image_release(&state->scaled_background);
    pframe_free_run((u32)state,slot->pages);objpool_release(&desktops,(int)id);
}
/* 合成期间持有任务0并开IRQ；票据表的短操作仍必须原子。保存IF
 * 再恢复，不能假定调用者总开中断，更不能在这里嵌套render_hold。 */
static u32 wall_lock(void){u32 flags;__asm__ __volatile__("pushfl; popl %0":"=r"(flags)::"memory");cli();return flags;}
static void wall_unlock(u32 flags){if(flags&512)sti();}
static int upper(int c){
    return c>='a'&&c<='z'?c-'a'+'A':c;
}
static int prefix(const char *s,const char *p)
{
    while(*p)if(upper(*s++)!=upper(*p++))return 0;
    return 1;
}
static void icon_load(desk_item_t *item,const char *path)
{
    int i=0;do{item->iconspec[i]=path[i];}while(path[i++] && i<64);
}
static int same(const char *a,const char *b)
{while(*a && upper(*a)==upper(*b)){a++;b++;}return upper(*a)==upper(*b);}
static int readable(const char *path)
{session_t *s=session_get(desktop_context());return s && fs_access_identity(path,s->uid,s->gid,FS_ACCESS_READ);}
static void flush_cache(void)
{
    if(!cache_dirty)return;
    for(int i=0;i<32;i++){image_reader_cancel(&cache[i].reader);image_release(&cache[i].surface);cache[i].used=0;}
    image_reader_cancel(&desktop_current()->background_reader);
    if(wall_ticket){u32 flags=wall_lock();image_service_cancel(0,wall_ticket);wall_unlock(flags);wall_ticket=0;}
    /* 保留有效旧壁纸，候选读完才换。坏主题路径/坏JPEG不能把用户
     * 正在看的桌面立即清空。桌面图标缓存仍按原主题代数完整失效。 */
    wall_path[0]=0;wall_attempted=wall_error=0;cache_dirty=0;
    desktop_pages=wall.pages+wall_render.pages;
}
static int resolve_icon(const desk_item_t *item,char *path,u32 *offset,u32 *length)
{
    const char *spec=item->iconspec;u32 stat[2];*offset=*length=0;
    if(!*spec){
        /* EXEC命令的参数不属于文件名，空图标字段只取第一个路径。
         * SCX内置图标是资源身份，不读取主题iconroot，更不改可执行文件。 */
        const char *p=item->command;while(*p==' ')p++;
        int n=0;while(*p && *p!=' '){if(n==63)return -1;path[n++]=*p++;}path[n]=0;
        u8 header[36];return !readable(path) || image_scx_info(path,header,offset,length) || !*length?-1:0;
    }
    if(*spec=='@'){
        char name[32];int n=0;spec++;
        while(*spec){
            int c=upper(*spec++);
            if(n==31 || !((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return -1;
            name[n++]=(char)c;
        }
        name[n]=0;if(!n)return -1;
        int root=theme_path(1,path,64);
        if(root>0 && root+n+5<=63){
            path[root++]='/';for(int i=0;i<n;i++)path[root++]=name[i];
            const char *ext=".SCB";for(int i=0;i<5;i++)path[root+i]=ext[i];
            if(readable(path) && !fs_stat(path,stat) && stat[0]==1){*length=stat[1];return 0;}
        }
        const char *fallback="SYS/ICONS/";int at=0;
        while(*fallback)path[at++]=*fallback++;
        for(int i=0;i<n;i++)path[at++]=name[i];
        const char *ext=".SCB";for(int i=0;i<5;i++)path[at+i]=ext[i];
    }else if(fs_normalize(spec,path)<=0)return -1;
    if(!readable(path) || fs_stat(path,stat) || stat[0]!=1)return -1;
    *length=stat[1];return 0;
}
static image_surface_t *cached_icon(const desk_item_t *item)
{
    char path[64];u32 offset,length;
    if(resolve_icon(item,path,&offset,&length))return 0;
    int selected=0;u32 oldest=0xFFFFFFFFu;
    for(int i=0;i<32;i++){
        if(cache[i].used && cache[i].offset==offset && cache[i].length==length && same(cache[i].path,path)){
            cache[i].touch=++cache_clock;return cache[i].surface.pixels?&cache[i].surface:0;
        }
        if(!cache[i].used){selected=i;oldest=0;}
        else if(oldest && cache[i].touch<oldest){selected=i;oldest=cache[i].touch;}
    }
    icon_cache_t *c=&cache[selected];desktop_pages-=c->surface.pages+c->reader.candidate.pages;
    image_reader_cancel(&c->reader);image_release(&c->surface);
    int i=0;do{c->path[i]=path[i];}while(path[i++]);
    c->offset=offset;c->length=length;c->touch=++cache_clock;c->used=1;
    u8 header[288];
    if(!offset && length==288 && fs_read(path,header,288)==288 && prefix((char *)header,"SCF1MIO")
       && !header[7] && *(u32 *)(header+8)==1){
        c->surface.pixels=(u32 *)pframe_alloc_run(1);
        if(!c->surface.pixels)return 0;
        c->surface.width=c->surface.height=16;c->surface.pages=1;
        for(int y=0;y<16;y++)for(int x=0;x<16;x++)
            c->surface.pixels[y*16+x]=header[16+y*17+x]=='#'?0xFF000000u|theme_color(TH_ACCENT):0;
    }else{
        /* 合成只登记原始资源请求。16KiB服务批次在合成保护之外，
         * 冷图标/壁纸不能串行占住整帧，使管理租约或输入超时。 */
        image_reader_begin(&c->reader,path,offset,length,1);return 0;
    }
    desktop_pages+=c->surface.pages;
    return &c->surface;
}
static int line(char **source,char *out,int capacity)
{

    int n=0;
    while(**source && **source!='\n') {

        char c=*(*source)++;
        if(c=='\r')continue;
        if(n+1>=capacity)return -1;
        out[n++]=c;

    }
    if(**source=='\n')(*source)++;
    out[n]=0;
    return n;
}
static int menu_parse(const char *path,int commit)
{
    if(!readable(path))return -2;
    u32 info[2];
    if(fs_stat(path,info) || info[0]!=1 || info[1]>=sizeof(text))return -2;
    int n=fs_read(path,text,sizeof(text)-1);
    if(n<0 || (u32)n!=info[1])return -2;
    if(n<9)return -1;
    for(int i=0;i<n;i++)if(!text[i])return -1;
    text[n]=0;
    if(!gfx_utf8_valid(text))return -1;
    char *source=text,head[16];
    if(line(&source,head,sizeof(head))!=9 || !prefix(head,"SMENU1MIO"))return -1;
    int count=0;
    /* 两遍解析复用一条候选记录：第一遍只验证，第二遍才替换。
     * 不在低端 BSS 同时放32条重复图标，给字库/调试保留空间。 */
    for(int pass=0;pass<(commit?2:1);pass++) {

        source=text;
        line(&source,head,sizeof(head));
        count=0;
        while(*source) {

            char record[256];
            int bytes=line(&source,record,sizeof(record));
            if(bytes<0)return -1;
            if(!bytes || record[0]=='#')continue;
            if(count==32)return -1;
            desk_item_t *item=&candidate;
            item->link[0]=0;
            char path[64];
            int field=0,at=0;
            for(int i=0;i<=bytes;i++) {

                char c=record[i];
                if(c=='|' || !c) {

                    if(field==0)item->label[at]=0;
                    else if(field==1)item->command[at]=0;
                    else if(field==2)path[at]=0;
                    else return -1;
                    field++;
                    at=0;
                    if(!c)break;
                    continue;

                }
                if((u8)c<32)return -1;
                int max=field==0?31:field==1?127:63;
                if(field>2 || at>=max)return -1;
                if(field==0)item->label[at++]=c;
                else if(field==1)item->command[at++]=c;
                else path[at++]=c;

            }
            if(field!=3 || !item->label[0] || !item->command[0])return -1;
            if(pass){
                icon_load(item,path);
                item->used=1;
                menus[count]=*item;

            }
            count++;

        }

    }
    if(commit)nmenus=count;
    return 0;
}
int desktop_config_check(int kind,const char *path)
{
    if(kind==0)return menu_parse(path,0);
    if(kind!=1)return -1;
    u32 info[2];
    if(fs_stat(path,info) || info[0]!=1 || info[1]>511)return -2;
    int n=fs_read(path,text,511);
    if(n<0 || (u32)n!=info[1])return -2;
    for(int i=0;i<n;i++)if(!text[i] || ((u8)text[i]<32 && text[i]!='\r' && text[i]!='\n'))return -1;
    text[n]=0;if(!gfx_utf8_valid(text))return -1;
    char *source=text,label[32],command[128],icon[64];
    if(line(&source,label,32)<1 || line(&source,command,128)<1 || line(&source,icon,64)<0)return -1;
    while(*source)if(*source++!='\r' && source[-1]!='\n')return -1;
    return 0;
}
static void icons_load(void)
{

    nicons=0;
    for(int i=0;i<fs_count() && nicons<24;i++) {

        const char *name=fs_name(i);
        int len=0;
        while(name[len])len++;
        char directory[64];session_t *s=session_get(desktop_context());
        if(!s || s->kind==SESSION_LOCKED)continue;
        const char *base=s->kind==SESSION_NORMAL && s->uid==AUTH_DEFAULT?"DESK/":0;
        if(base){if(len<9 || !prefix(name,base) || !prefix(name+len-4,".LNK"))continue;}
        else {int n=session_config_path_for(desktop_context(),"DESK/",directory);if(n<0 || len<n+5 || !prefix(name,directory) || !prefix(name+len-4,".LNK"))continue;}
        if(!readable(name))continue;
        int n=fs_read(name,text,511);
        if(n<=0)continue;
        text[n]=0;
        char *source=text,path[64];
        desk_item_t *item=&icons[nicons];
        if(line(&source,item->label,32)<1 || line(&source,item->command,128)<1 || line(&source,path,64)<0)continue;
        int k=0;
        do{
            item->link[k]=name[k];

        }
        while(name[k++]);
        icon_load(item,path);
        item->used=1;
        nicons++;

    }
}
int desktop_reload(void){
    if(desktop_session_prepare(desktop_context()))return -1;
    char path[64];int result=session_config_path_for(desktop_context(),"MENU.CFG",path)<0?-1:menu_parse(path,1);
    /* 新会话还没有个人菜单时复制只读默认布局到本会话内存；此后
     * 每次重载只改变自己的表，保存也不能覆盖别人的SYS配置。 */
    if(result==-2 && !desktop_current()->loaded)result=menu_parse("SYS/MENU.CFG",1);
    /* 新纯检查区分读取/语法，原RELOAD仍保留既有-1失败返回合同。 */
    if(result<0)result=-1;
    icons_load();
    cache_dirty=1;
    desktop_current()->loaded=1;
    return result;
}
void desktop_session_activate(u32 id)
{
    if(desktop_session_prepare(id))return;
    u32 saved=context_override;context_override=id;
    if(!desktop_current()->loaded)desktop_reload();context_override=saved;
}
int desktop_count(int menu){
    return menu?nmenus:nicons;
}
const desk_item_t *desktop_item(int menu,int index)
{
    int n=menu?nmenus:nicons;
    return index>=0 && index<n?&(menu?menus:icons)[index]:0;
}
void desktop_icon(int x,int y,int size,const desk_item_t *item)
{
    if(!item || size<1)return;
    i32 clip[4];gfx_clip_bounds(clip);
    if(x>=clip[0]+clip[2] || y>=clip[1]+clip[3] || x+size<=clip[0] || y+size<=clip[1])return;
    flush_cache();image_surface_t *s=cached_icon(item);
    if(!s){
        /* 可选资源缺失仍有可辨识的文件轮廓；所有颜色从主题角色取，
         * 不把所有程序放进统一彩色正方形底板。 */
        gfx_rgb_fill(x+size/4,y+size/8,size/2,size*3/4,theme_color(TH_LINE));
        gfx_rgb_fill(x+size/4+1,y+size/8+1,size/2-2,size*3/4-2,theme_color(TH_PAPER));
        return;
    }
    int w=size,h=size;
    if(s->width>s->height)h=(int)(s->height*(u32)size/s->width);
    else w=(int)(s->width*(u32)size/s->height);
    if(w<1)w=1;
    if(h<1)h=1;
    x+=(size-w)/2;y+=(size-h)/2;
    int first_x=clip[0]>x?clip[0]-x:0,first_y=clip[1]>y?clip[1]-y:0;
    int last_x=x+w>clip[0]+clip[2]?clip[0]+clip[2]-x:w;
    int last_y=y+h>clip[1]+clip[3]?clip[1]+clip[3]-y:h;
    /* 采样仍用原图完整w/h，不能把裁后宽度作为缩放分母；这样
     * 局部hover/客户帧重画时图标不会缩放或偏移，也省去无关像素。 */
    for(int row=first_y;row<last_y;row++)for(int col=first_x;col<last_x;col++)
        gfx_argb_pset(x+col,y+row,s->pixels[((u32)row*s->height/(u32)h)*s->width+(u32)col*s->width/(u32)w]);
}
static void wall_accept(image_surface_t *candidate,int prepared)
{
    desktop_pages-=wall.pages+wall_render.pages;
    image_release(&wall);image_release(&wall_render);wall=*candidate;*candidate=(image_surface_t){0};
    desktop_pages+=wall.pages;wall_map_width=wall_map_height=0;wall_error=0;
    if(prepared)return;
        /* 壁纸基底是当前主题face，透明度只需在载入时合成一次。
         * 随后此独占缓存保存RGB，不再每次鼠标移动重复alpha计算；
         * 主题代数变化会释放并重新读取，原SCB2文件和图标ARGB不变。 */
        u32 face=theme_color(TH_FACE);
        for(u32 i=0;i<wall.width*wall.height;i++) {
            u32 pixel=wall.pixels[i],alpha=pixel>>24,result=0;
            if(alpha==255)result=pixel&0xFFFFFFu;
            else if(!alpha)result=face;
            else for(u32 shift=0;shift<24;shift+=8)
                result|=(((((pixel>>shift)&255)*alpha+((face>>shift)&255)*(255-alpha)+127)/255)<<shift);
            wall.pixels[i]=result;
        }
}
int desktop_wallpaper(void)
{
    flush_cache();
    if(!wall_attempted){
        wall_attempted=1;u32 stat[2];image_surface_t candidate={0};
        int path=theme_path(0,wall_path,64);
        if(path<=0){wall_accept(&candidate,0);}
        else if(readable(wall_path) && !fs_stat(wall_path,stat) && stat[0]==1){
            image_reader_t *reader=&desktop_current()->background_reader;
            if(image_reader_begin(reader,wall_path,0,stat[1],0))wall_error=-2;
            else{reader->flatten=1;reader->background=theme_color(TH_FACE);}
        }else wall_error=-2;
    }
    if(wall_ticket){
        image_surface_t candidate={0};u32 flags=wall_lock();
        int result=image_service_take(wall_ticket,&candidate);wall_unlock(flags);
        if(result!=1){wall_ticket=0;if(!result)wall_accept(&candidate,0);else wall_error=result;}
    }
    if(!wall.pixels)return 0;
    /* cover模式用整数比较宽高比，中心裁剪。Classic素材已真实降为
     * 320x180，这里最近邻放大保留色块；照片绝不先转索引调色板。 */
    u32 crop_w=wall.width,crop_h=wall.height;
    if(wall.width*(u32)GFX_H>wall.height*(u32)GFX_W)crop_w=wall.height*(u32)GFX_W/(u32)GFX_H;
    else crop_h=wall.width*(u32)GFX_H/(u32)GFX_W;
    if(!crop_w)crop_w=1;
    if(!crop_h)crop_h=1;
    u32 left=(wall.width-crop_w)/2,top=(wall.height-crop_h)/2;
    if(wall_map_width!=GFX_W || wall_map_height!=GFX_H) {
        desktop_pages-=wall_render.pages;image_release(&wall_render);
        for(int x=0;x<GFX_W;x++)wall_columns[x]=left+(u32)x*crop_w/(u32)GFX_W;
        wall_map_width=GFX_W;wall_map_height=GFX_H;
        /* mio：窗口拖动时壁纸本身没有变化，不需要每帧重新采样两
         * 百万像素。足够内存时生成当前模式的最终背景，只复制；
         * 保留8MB余量给窗口/应用，碎片或低内存分配失败仍走原采样
         * 路径，像素不降质。主题/模式变化释放，页计入常驻缓存。
         * VGA在这里一次量化，之后也不每帧重复256色最近色搜索。 */
        u32 pages=((u32)GFX_W*GFX_H*4+4095)/4096;
        if(memory_free_bytes()>pages*4096+8u*1024*1024){
            wall_render.pixels=(u32 *)pframe_alloc_run(pages);
            if(wall_render.pixels){
                wall_render.width=(u32)GFX_W;wall_render.height=(u32)GFX_H;
                wall_render.pages=pages;desktop_pages+=pages;
                for(int y=0;y<GFX_H;y++){
                    const u32 *row=wall.pixels+(top+(u32)y*crop_h/(u32)GFX_H)*wall.width;
                    for(int x=0;x<GFX_W;x++){
                        u32 rgb=row[wall_columns[x]];
                        wall_render.pixels[y*GFX_W+x]=GFX_W==320?display_index(rgb):rgb;
                    }
                }
            }
        }
    }
    if(wall_render.pixels){gfx_background_copy(wall_render.pixels,GFX_W,GFX_H);return 1;}
    i32 clip[4];gfx_clip_bounds(clip);
    for(int y=clip[1];y<clip[1]+clip[3];y++) {
        const u32 *row=wall.pixels+(top+(u32)y*crop_h/(u32)GFX_H)*wall.width;
        gfx_rgb_row_map(y,row,wall_columns,GFX_W);
    }
    return 1;
}

void desktop_poll(void)
{
    if(task_pid()!=0)return;
    desktop_state_t *state=desktop_current();
    desktop_io_session=session_current();desktop_io_pending=0;
    for(u32 i=0;i<32;i++)if(state->images[i].reader.phase)desktop_io_pending++;
    if(state->background_reader.phase)desktop_io_pending++;
    /* 只有当前显示会话进入这里；隐藏候选没有IO、解码或帧生成。
     * 所有读取仍核本会话rw与原对象元数据，批次间删改/撤权不会
     * 拼成混合图片。逐会话游标轮转，壁纸不会饿死小图标。 */
    if(state->needs_flush)return;
    for(u32 checked=0;checked<33;checked++){
        u32 index=state->io_cursor;if(++state->io_cursor==33)state->io_cursor=0;
        image_reader_t *reader=index==32?&state->background_reader:&state->images[index].reader;
        if(!reader->phase)continue;
        if(!readable(reader->path)){
            state->pages-=reader->candidate.pages;image_reader_cancel(reader);
            desktop_io_pending--;if(index==32)state->error=-5;return;
        }
        image_surface_t complete={0};
        image_surface_t *destination=index==32?&complete:&state->images[index].surface;
        u32 pages_before=reader->candidate.pages+destination->pages,position=reader->position,started=sc_ticks;
        task_render_hold(1);sti();
        int result=image_reader_step(reader,16384,destination);
        if(result>=0)desktop_io_bytes+=reader->position-position;
        desktop_io_batches++;
        cli();
        state->pages-=pages_before;state->pages+=reader->candidate.pages;
        if(index!=32)state->pages+=destination->pages;
        if(!result){
            desktop_io_pending--;
            if(index==32){sti();wall_accept(&complete,1);cli();}
            wm_request_compose();
        }else if(result<0 && index==32){
            desktop_io_pending--;
            /* 小格式不合格时仍走已有三环压缩解码器。请求登记是
             * 短临界区；没有在失败后把半幅原始正文交给显示路径。 */
            int ticket=image_service_request(0,state->path);
            if(ticket>0)state->ticket=(u32)ticket;else state->error=ticket;
        }else if(result<0)desktop_io_pending--;
        u32 elapsed=sc_ticks-started;
        if(elapsed>desktop_io_ticks_max)desktop_io_ticks_max=elapsed;
        task_render_hold(0);
        return;
    }
}
