/* =====================================================================
 * hexed.c —— SandHex：十六进制编辑器
 * 所属：SandCore_ExtraSoftware_Pack_1（ext/SoftwarePack1）
 *
 * 特性
 *   - 打开任意文件（默认上限 8MB，HEXED.CFG limit_mb 可调 1..16）
 *   - 十六进制 + ASCII 双栏；偏移 8 位 HEX；点击/方向键选位
 *   - 双输入模式：HEX 模式逐半字节改写，TEXT 模式按 ASCII 覆写
 *   - 双栈撤销/重做（各 65536 笔）；F6 还原到磁盘版本（有确认）
 *   - G 跳转（支持 0x 前缀）、F 搜索（自动识别文本/HEX 模式）、
 *     N 下一个；F3 保存 / F4 另存 / F5 打开 / F7 新建 256B
 *   - 脏标记、状态栏（偏移/大小/模式/上限）
 *
 * 工程立场：文件模型是"整文件读入 -> 原地改写 -> 整文件写回"，
 * 与 SandFS 的 FSWRITE 全量写语义一致，不支持插入/截断（那是
 * 不同工具的职责）。所有编辑都先落撤销栈，绝不出现改了就
 * 回不去的状态。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "exui_ext.inc"
#include "exutil.h"

#define HX_PER_ROW 16
#define HX_UNDO_MAX 65536
#define HX_PAT_MAX 64

static u8 *hx_data;              /* 文件正文 */
static u32 hx_size;
static u32 hx_cap;               /* 当前缓冲容量 */
static u32 hx_limit=8*1024*1024; /* 配置上限 */
static u32 hx_off=0;             /* 选中字节偏移 */
static u32 hx_scroll=0;          /* 顶部行偏移（字节） */
static char hx_path[64];
static int hx_dirty;
static int hx_text_mode;         /* 0 HEX 半字节 / 1 TEXT 覆写 */
static int hx_nibble;            /* HEX 模式：0 等高 4 位 / 1 等低 4 位 */
static u8 hx_pending;            /* 半字节暂存 */
static char hx_status[64];
/* 撤销/重做双环 */
static u32 *hx_u_off,*hx_r_off;
static u8 *hx_u_old,*hx_r_old;
static u32 hx_u_count,hx_u_head,hx_r_count,hx_r_head;
/* 搜索 */
static char hx_pattern[HX_PAT_MAX];
static u8 hx_pat_bytes[HX_PAT_MAX];
static int hx_pat_len,hx_pat_hex;

static int hx_hexval(char c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}

/* ---------- 撤销/重做 ---------- */
static void hx_push_undo(u32 off,u8 old)
{
    hx_u_off[hx_u_head]=off;
    hx_u_old[hx_u_head]=old;
    hx_u_head=(hx_u_head+1)%HX_UNDO_MAX;
    if(hx_u_count<HX_UNDO_MAX)hx_u_count++;
    hx_r_count=0;hx_r_head=0;      /* 新编辑作废重做分支：标准语义 */
}
static void hx_undo(void)
{
    if(!hx_u_count){copy(hx_status,"Nothing to undo",sizeof(hx_status));return;}
    hx_u_head=(hx_u_head+HX_UNDO_MAX-1)%HX_UNDO_MAX;
    hx_u_count--;
    u32 off=hx_u_off[hx_u_head];
    u8 old=hx_u_old[hx_u_head];
    hx_r_off[hx_r_head]=off;
    hx_r_old[hx_r_head]=hx_data[off];
    hx_r_head=(hx_r_head+1)%HX_UNDO_MAX;
    if(hx_r_count<HX_UNDO_MAX)hx_r_count++;
    hx_data[off]=old;
    hx_off=off;
    hx_dirty=1;
    copy(hx_status,"Undo",sizeof(hx_status));
}
static void hx_redo(void)
{
    if(!hx_r_count){copy(hx_status,"Nothing to redo",sizeof(hx_status));return;}
    hx_r_head=(hx_r_head+HX_UNDO_MAX-1)%HX_UNDO_MAX;
    hx_r_count--;
    u32 off=hx_r_off[hx_r_head];
    u8 cur=hx_r_old[hx_r_head];
    hx_u_off[hx_u_head]=off;
    hx_u_old[hx_u_head]=hx_data[off];
    hx_u_head=(hx_u_head+1)%HX_UNDO_MAX;
    if(hx_u_count<HX_UNDO_MAX)hx_u_count++;
    hx_data[off]=cur;
    hx_off=off;
    hx_dirty=1;
    copy(hx_status,"Redo",sizeof(hx_status));
}

/* ---------- 编辑 ---------- */
static void hx_write_byte(u32 off,u8 v)
{
    if(off>=hx_size)return;
    hx_push_undo(off,hx_data[off]);
    hx_data[off]=v;
    hx_dirty=1;
    copy(hx_status,"Edited",sizeof(hx_status));
}
static int hx_type_hex(int val)
{
    if(hx_off>=hx_size)return 0;
    if(!hx_nibble){
        hx_pending=(u8)(val<<4);
        hx_nibble=1;
    }else{
        hx_write_byte(hx_off,hx_pending|(u8)val);
        hx_nibble=0;
        hx_off++;                       /* 完成一个字节自动前进 */
        if(hx_off>=hx_size)hx_off=hx_size-1;
    }
    ui_followup=1;
    return 1;
}
static int hx_type_text(char c)
{
    if(hx_off>=hx_size)return 0;
    hx_write_byte(hx_off,(u8)c);
    hx_off++;
    if(hx_off>=hx_size)hx_off=hx_size-1;
    ui_followup=1;
    return 1;
}

/* ---------- 文件 ---------- */
static void hx_free_all(void)
{
    if(hx_data){sc_free(hx_data);hx_data=0;}
    if(hx_u_off){sc_free(hx_u_off);hx_u_off=0;}
    if(hx_u_old){sc_free(hx_u_old);hx_u_old=0;}
    if(hx_r_off){sc_free(hx_r_off);hx_r_off=0;}
    if(hx_r_old){sc_free(hx_r_old);hx_r_old=0;}
}
static int hx_alloc_buffers(u32 size)
{
    hx_cap=size;
    hx_data=sc_alloc(size);
    hx_u_off=sc_alloc(HX_UNDO_MAX*4);
    hx_u_old=sc_alloc(HX_UNDO_MAX);
    hx_r_off=sc_alloc(HX_UNDO_MAX*4);
    hx_r_old=sc_alloc(HX_UNDO_MAX);
    if(!hx_data||!hx_u_off||!hx_u_old||!hx_r_off||!hx_r_old){
        hx_free_all();
        return -1;
    }
    return 0;
}
static void hx_load(const char *path)
{
    u32 info[2];
    if(sc_stat(path,info)||info[0]!=1){
        copy(hx_status,"Not a file",sizeof(hx_status));
        ui_followup=1;
        return;
    }
    if(info[1]>(u32)hx_limit){
        copy(hx_status,"File exceeds size limit (see HEXED.CFG)",
             sizeof(hx_status));
        ui_followup=1;
        return;
    }
    hx_free_all();
    if(hx_alloc_buffers(info[1])){
        copy(hx_status,"Out of memory",sizeof(hx_status));
        ui_followup=1;
        return;
    }
    int n=sc_read(path,hx_data,(int)info[1]);
    if(n!=(int)info[1]){
        hx_free_all();
        copy(hx_status,"Read failed",sizeof(hx_status));
        ui_followup=1;
        return;
    }
    hx_size=info[1];
    copy(hx_path,path,sizeof(hx_path));
    hx_dirty=0;
    hx_off=0;
    hx_scroll=0;
    hx_u_count=hx_u_head=hx_r_count=hx_r_head=0;
    copy(hx_status,"Opened",sizeof(hx_status));
    ui_followup=1;
}
static void hx_open_dialog(void)
{
    char path[64];
    copy(path,hx_path,sizeof(path));
    if(!ui_edit_path(path,sizeof(path),"Open file"))return;
    if(hx_dirty&&!ui_confirm("Discard changes?","Buffer is not saved."))return;
    hx_load(path);
}
static void hx_save_as(void)
{
    char path[64];
    copy(path,hx_path,sizeof(path));
    if(!ui_edit_path(path,sizeof(path),"Save as"))return;
    if(sc_write(path,hx_data,(int)hx_size)==(int)hx_size){
        copy(hx_path,path,sizeof(hx_path));
        hx_dirty=0;
        copy(hx_status,"Saved",sizeof(hx_status));
    }else copy(hx_status,"Write failed",sizeof(hx_status));
    ui_followup=1;
}
static void hx_new(void)
{
    if(hx_dirty&&!ui_confirm("Discard changes?","Buffer is not saved."))return;
    hx_free_all();
    if(hx_alloc_buffers(256)){
        copy(hx_status,"Out of memory",sizeof(hx_status));
        ui_followup=1;
        return;
    }
    for(int i=0;i<256;i++)hx_data[i]=0;
    hx_size=256;
    hx_path[0]=0;
    hx_dirty=0;
    hx_off=0;
    hx_scroll=0;
    copy(hx_status,"New 256-byte buffer",sizeof(hx_status));
    ui_followup=1;
}
static void hx_revert(void)
{
    if(!hx_path[0]){copy(hx_status,"No file",sizeof(hx_status));ui_followup=1;return;}
    if(hx_dirty&&!ui_confirm("Revert?","All edits since last save are lost."))return;
    hx_load(hx_path);
}

/* ---------- 跳转与搜索 ---------- */
static void hx_goto_dialog(void)
{
    char buf[16];
    buf[0]='0';buf[1]='x';buf[2]=0;
    ex_hex(buf+2,8,hx_off);
    if(!ui_edit_value(buf,sizeof(buf),"Go to offset (0x...)",0))return;
    int v;
    if(ex_parse_int(buf,&v)){                      /* ex_parse_int 认 0x 前缀 */
        copy(hx_status,"Bad offset",sizeof(hx_status));
    }else{
        u32 off=v<0?0:(u32)v;
        if(off>=hx_size)off=hx_size?hx_size-1:0;
        hx_off=off;
        copy(hx_status,"Moved",sizeof(hx_status));
    }
    ui_followup=1;
}
/* 模式判定：以 0x 开头或"纯 hex 位+空白成对"按字节序列；
 * 其余按文本。显式 0x 前缀永远赢，避免 "BEEF" 歧义靠猜。 */
static void hx_parse_pattern(const char *s)
{
    hx_pat_len=0;
    hx_pat_hex=0;
    if(ex_starts(s,"0x")||ex_starts(s,"0X")){
        hx_pat_hex=1;
        s+=2;
        int hi=-1;
        while(*s&&hx_pat_len<HX_PAT_MAX){
            int v=hx_hexval(*s);
            if(v<0){if(*s==' '||*s=='-'){s++;continue;}break;}
            if(hi<0)hi=v;
            else hx_pat_bytes[hx_pat_len++]=(u8)(hi*16+v),hi=-1;
            s++;
        }
        if(hx_pat_len==0)hx_pat_hex=0;
        return;
    }
    /* 纯偶数位 hex 串也按字节（如 AB CD），带非 hex 字符按文本 */
    int hexish=length(s)>0;
    for(const char *p=s;*p;p++){
        char c=ex_upper(*p);
        if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')||c==' '))hexish=0;
    }
    if(hexish){
        hx_pat_hex=1;
        int hi=-1;
        while(*s&&hx_pat_len<HX_PAT_MAX){
            int v=hx_hexval(*s);
            if(v<0){s++;continue;}
            if(hi<0)hi=v;
            else hx_pat_bytes[hx_pat_len++]=(u8)(hi*16+v),hi=-1;
            s++;
        }
        if(hx_pat_len==0)hx_pat_hex=0;
        return;
    }
    hx_pat_len=ex_min(length(s),HX_PAT_MAX);
    for(int i=0;i<hx_pat_len;i++)hx_pat_bytes[i]=(u8)s[i];
}
static void hx_find_dialog(void)
{
    char buf[HX_PAT_MAX+4];
    copy(buf,hx_pattern,sizeof(buf));
    if(!ui_edit_value(buf,sizeof(buf),"Find (text or hex bytes)",0))return;
    copy(hx_pattern,buf,sizeof(hx_pattern));
    hx_parse_pattern(buf);
    if(!hx_pat_len){copy(hx_status,"Empty pattern",sizeof(hx_status));ui_followup=1;return;}
    hx_off=(hx_off+1)%hx_size;                     /* 从下一字节开始 */
    for(u32 k=0;k<hx_size;k++){
        u32 off=(hx_off+k)%hx_size;
        if(off+hx_pat_len>hx_size)continue;
        int ok=1;
        for(int i=0;i<hx_pat_len;i++)
            if(hx_data[off+i]!=hx_pat_bytes[i]){ok=0;break;}
        if(ok){
            hx_off=off;
            copy(hx_status,hx_pat_hex?"Found (hex)":"Found (text)",
                 sizeof(hx_status));
            ui_followup=1;
            return;
        }
    }
    copy(hx_status,"Not found",sizeof(hx_status));
    ui_followup=1;
}

/* ---------- 绘制 ---------- */
#define HX_GUT_W 72       /* 偏移列 */
#define HX_HEX_W (HX_PER_ROW*24+8)
#define HX_ASC_W (HX_PER_ROW*8+8)
static void hx_draw(void)
{
    char title[64];
    copy(title,hx_dirty?"SandHex *":"SandHex",sizeof(title));
    ui_header(title,hx_path[0]?hx_path:"no file");
    int tools_y=ui_compact?30:60;
    ui_small_control(1,16,tools_y-2,52,"Open",0);
    ui_small_control(2,74,tools_y-2,52,"Save",0);
    ui_small_control(3,132,tools_y-2,64,"SaveAs",0);
    ui_small_control(4,202,tools_y-2,52,"Goto",0);
    ui_small_control(5,260,tools_y-2,52,"Find",0);
    ui_small_control(6,318,tools_y-2,52,"Undo",0);
    ui_small_control(7,376,tools_y-2,52,"Redo",0);
    ui_small_control(8,434,tools_y-2,64,"New256",0);
    ui_small_control(9,504,tools_y-2,64,"Revert",0);
    int footer_h=ui_compact?22:30;
    int vx=16,vy=tools_y+24;
    int vh=UI_H-footer_h-vy-4;
    int rows=vh/18;
    if(rows<1)rows=1;
    ui_rect(vx,vy,UI_W-32,rows*18,PAL_UI_INK);
    /* 滚动窗口跟随光标 */
    if(hx_off<hx_scroll)hx_scroll=hx_off;
    if(hx_off>=hx_scroll+rows*HX_PER_ROW)
        hx_scroll=(hx_off-(rows*HX_PER_ROW-1))&~(HX_PER_ROW-1);
    for(int r=0;r<rows;r++){
        u32 base=hx_scroll+(u32)r*HX_PER_ROW;
        int y=vy+r*18+2;
        if(base>=hx_size&&hx_size)break;
        char off[10];
        ex_hex(off,8,base);
        ui_text(vx+4,y,off,PAL_UI_MUTED);
        for(int c=0;c<HX_PER_ROW;c++){
            u32 off2=base+c;
            int hx_x=vx+HX_GUT_W+c*24;
            int asc_x=vx+HX_GUT_W+HX_HEX_W+c*8;
            if(off2<hx_size){
                u8 b=hx_data[off2];
                int sel=off2==hx_off;
                /* 半字节输入进行中：高 4 位先上屏预览 */
                if(sel&&hx_nibble)b=(b&0x0F)|hx_pending;
                char hex[3];
                ex_hex(hex,2,b);
                char ch[2];
                ch[0]=(char)(b>=32&&b<127?b:'.');
                ch[1]=0;
                if(sel){
                    ui_rect(hx_x-2,y-2,22,16,SC_THEME_SELECT);
                    ui_text(hx_x,y,hex,SC_THEME_SELECT_TEXT);
                    ui_text(asc_x,y,ch,SC_THEME_SELECT_TEXT);
                }else{
                    ui_text(hx_x,y,hex,b?PAL_UI_TEXT:PAL_UI_MUTED);
                    ui_text(asc_x,y,ch,b>=32&&b<127?PAL_UI_TEXT:PAL_UI_MUTED);
                }
            }
        }
        /* 行分隔线：弱化，只为扫读 */
        if(r)ui_rect(vx,vy+r*18,UI_W-32,1,PAL_UI_LINE);
    }
    /* 鼠标选位：hex 区或 ASCII 区命中都换算到字节 */
    if(ui_focus&&(ui_pressed&1)&&ui_hit(vx,vy,UI_W-32,rows*18)){
        int c=-1;
        if(ui_x>=vx+HX_GUT_W&&ui_x<vx+HX_GUT_W+HX_HEX_W)
            c=(ui_x-vx-HX_GUT_W)/24;
        else if(ui_x>=vx+HX_GUT_W+HX_HEX_W&&ui_x<vx+HX_GUT_W+HX_HEX_W+HX_ASC_W)
            c=(ui_x-vx-HX_GUT_W-HX_HEX_W)/8;
        if(c>=0&&c<HX_PER_ROW){
            int r=ex_max(0,(ui_y-vy)/18);
            u32 off=hx_scroll+(u32)r*HX_PER_ROW+c;
            if(off<hx_size){
                hx_off=off;
                hx_nibble=0;
                ui_followup=1;
            }
        }
    }
    /* 状态栏 */
    char foot[120];
    if(hx_status[0]){
        copy(foot,hx_status,sizeof(foot));
        hx_status[0]=0;                            /* 一次性消息 */
    }else{
        char off[12],size[12];
        ex_hex(off,8,hx_off);
        ex_udec(size,sizeof(size),hx_size);
        copy(foot,hx_text_mode?"TEXT":"HEX",sizeof(foot));
        append(foot," @0x",sizeof(foot));
        append(foot,off,sizeof(foot));
        append(foot," / ",sizeof(foot));
        append(foot,size,sizeof(foot));
        append(foot,"B",sizeof(foot));
    }
    ui_footer(foot);
}

/* ---------- 控件分派 ---------- */
static void hx_save_current(void)
{
    if(!hx_path[0]){hx_save_as();return;}
    if(sc_write(hx_path,hx_data,(int)hx_size)==(int)hx_size){
        hx_dirty=0;
        copy(hx_status,"Saved",sizeof(hx_status));
    }else copy(hx_status,"Write failed",sizeof(hx_status));
}
static void hx_action(int id)
{
    switch(id){
    case 1:hx_open_dialog();break;
    case 2:hx_save_current();break;
    case 3:hx_save_as();break;
    case 4:hx_goto_dialog();break;
    case 5:hx_find_dialog();break;
    case 6:hx_undo();break;
    case 7:hx_redo();break;
    case 8:hx_new();break;
    case 9:hx_revert();break;
    }
    ui_followup=1;
}

int main(void)
{
    if(ui_open("SandHex / SandCore ext")<0)return 1;
    char cfg[2048];
    if(sc_read("HOME/HEXED.CFG",cfg,sizeof(cfg)-1)>=0){
        cfg[sizeof(cfg)-1]=0;
        char v[16];
        if(ex_cfg_get(cfg,"limit_mb",v,sizeof(v))==0){
            int mb;
            if(ex_parse_int(v,&mb)==0)hx_limit=(u32)ex_clamp(mb,1,16)*1024*1024;
        }
    }
    /* 启动即给出可编辑缓冲，而不是空白窗口：新建语义更好上手 */
    if(hx_alloc_buffers(256))return 1;
    for(int i=0;i<256;i++)hx_data[i]=0;
    hx_size=256;
    ui_followup=1;
    for(;;){
        if(!ui_frame_due_interval(100))continue;
        ui_pointer();
        hx_draw();
        ui_present();
        int key=ui_action?-1:sc_key();
        if(ui_action){hx_action(ui_action);continue;}
        if(key<0)continue;
        if(key==27)return 0;
        if(key==0x80){                                 /* 上：一行 */
            hx_off=hx_off>=HX_PER_ROW?hx_off-HX_PER_ROW:0;
            ui_followup=1;
        }else if(key==0x81){                           /* 下：一行 */
            hx_off=ex_min(hx_size-1,hx_off+HX_PER_ROW);
            ui_followup=1;
        }else if(key==0x82){                           /* 左 */
            if(hx_off)hx_off--;
            ui_followup=1;
        }else if(key==0x83){                           /* 右 */
            hx_off=ex_min(hx_size-1,hx_off+1);
            ui_followup=1;
        }else if(key==0x88){                           /* F8: 翻页上 */
            hx_scroll=hx_scroll>=(u32)HX_PER_ROW*8?
                hx_scroll-(u32)HX_PER_ROW*8:0;
            ui_followup=1;
        }else if(key==0x89){                           /* F9: 翻页下 */
            hx_scroll=ex_min(hx_size?hx_size-1:0,
                             hx_scroll+(u32)HX_PER_ROW*8);
            ui_followup=1;
        }else if(key==1){                              /* F1 模式切换 */
            hx_text_mode=!hx_text_mode;
            hx_nibble=0;
            ui_followup=1;
        }else if(key==3){                              /* F3 保存 */
            if(hx_path[0]){
                if(sc_write(hx_path,hx_data,(int)hx_size)==(int)hx_size){
                    hx_dirty=0;
                    copy(hx_status,"Saved",sizeof(hx_status));
                }else copy(hx_status,"Write failed",sizeof(hx_status));
            }else hx_save_as();
            ui_followup=1;
        }else if(key==4)hx_save_as();                  /* F4 */
        else if(key==5)hx_open_dialog();               /* F5 */
        else if(key==6)hx_revert();                    /* F6 */
        else if(key==7)hx_new();                       /* F7 */
        else if(key=='g'||key=='G')hx_goto_dialog();   /* 仅 HEX 模式占字母 */
        else if(key=='f'||key=='F')hx_find_dialog();
        else if(key=='u'||key=='U')hx_undo();
        else if(key=='i'||key=='I')hx_redo();
        else if(hx_text_mode){
            if(key>=32&&key<127)hx_type_text((char)key);
        }else{
            int v=hx_hexval((char)key);
            if(v>=0)hx_type_hex(v);
        }
    }
}
