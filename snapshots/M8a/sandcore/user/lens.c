/* =====================================================================
 * mio：M8 Lens。完整图片和过滤后的Fit分别放私有高端堆，界面后台
 * 仍由NUI拥有。文件加载先建立独立候选，所有行验证完毕才交换；
 * 打开失败不能破坏上次照片、原始像素倍率、平移或文件身份。
 *
 * 原图尺寸最多1920×1080。100%直接读取完整ARGB缓存，一个源像素
 * 对应一个物理像素；界面缩放不改变图片倍率。Fit的双线性缓存只
 * 在尺寸/图像变化时重建，鼠标hover不重复过滤整张图片。所有运算
 * 为整数；PNG/JPG/WebP由独立三环图片服务解码，原位图路径保留。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "IMAGE.inc"
#include "IMAGECLIENT.inc"
typedef struct {
    int width,height,bmp,version,bpp,top_down;
    u32 offset,stride;
} lens_image_t;
static u32 *preview,*fit_pixels,palette_rgb[256];
static u8 row_bytes[1920*4];
static char filename[64]="SYS/PICTURES/WELCOME.PNG",status[100]="Open SCB / BMP / PNG / JPEG / WebP";
static int image_w,image_h,cache_w,cache_h,loaded,image_error,fit=1,pan_x,pan_y;
/* 独立保留已提交格式，调试器/验收能只读识别实际SCB版本；候选
 * 失败不改它。只是应用私有诊断字段，不增加系统调用布局。 */
static volatile int scb_version;
static int dragging,drag_x,drag_y,fit_w,fit_h,fit_attempt_w,fit_attempt_h;
static int small_window,work_x,work_y,work_w,work_h,viewport_x,viewport_y,viewport_w,viewport_h;
/* 私有完成序号仅帮助验收判断调用返回；不是成功码或公开ABI。 */
static volatile int lens_operations;
static ScImageClient compressed;
static char pending_path[64];

static u32 row_color(const lens_image_t *info,int x)
{
    if(info->version==2)return image_u32(row_bytes+x*4);
    if(!info->bmp)return 0xFF000000u|palette_rgb[row_bytes[x]];
    u8 *p=row_bytes+x*info->bpp;
    /* BI_RGB的32位第四字节是保留字节，旧合同不能悄悄变成alpha。 */
    return 0xFF000000u|((u32)p[2]<<16)|((u32)p[1]<<8)|p[0];
}
static int read_row(const char *path,const lens_image_t *info,int y)
{
    int source=info->top_down?y:info->height-1-y;
    return sc_read_at(path,row_bytes,info->stride,info->offset+(u32)source*info->stride)==(int)info->stride;
}
static void release_fit(void)
{
    if(fit_pixels)sc_free(fit_pixels);
    fit_pixels=0;fit_w=fit_h=fit_attempt_w=fit_attempt_h=0;
}
static int load_compressed(const char *path)
{
    int result=image_client_begin_root(&compressed,path);
    if(result){copy(status,result==-2?"Cannot read image path":"Cannot start image service",sizeof(status));return 0;}
    copy(pending_path,path,sizeof(pending_path));
    copy(status,"Decoding image / old picture remains available",sizeof(status));
    return 2; /* 私有结果：已开始，尚未成功，不能增加完成序号。 */
}
static int poll_compressed(void)
{
    if(!compressed.ticket)return 0;
    int result=image_client_step(&compressed);
    if(result==1)return 0;
    /* 每轮非阻塞检查，加载时旧图仍可平移/缩放、窗口仍可改大小。
     * 不读GETKEY、不改鼠标沿；第二次Open会先取消前一次请求。
     * 完成前始终保留旧照片/缓存/文件名，失败不假报新图已打开。 */
    lens_operations++;
    if(result){
        copy(status,result==-5?"Not enough memory for image":result==-6?"Image service failed / timeout"
            :result==-3?"Image service unavailable":"Unsupported or damaged image",sizeof(status));
        image_error=1;return 1;
    }
    if(preview)sc_free(preview);release_fit();preview=compressed.pixels;compressed.pixels=0;
    image_w=cache_w=(int)compressed.info[1];image_h=cache_h=(int)compressed.info[2];
    scb_version=0;copy(filename,pending_path,sizeof(filename));loaded=1;image_error=0;fit=1;pan_x=pan_y=dragging=0;
    copy(status,"Image opened / Fit or 100%, drag to pan",sizeof(status));return 1;
}
static int load_image(const char *path)
{
    u32 stat[2];u8 header[64];lens_image_t candidate;u32 *pixels=0;
    /* 输入框容量高于文件身份容量，完整拒绝第64B；不截断后读另
     * 一个同前缀文件。所有早期失败尚未修改有效图片的任何字段。 */
    if(length(path)>=64){copy(status,"Path exceeds 63 bytes",sizeof(status));return 0;}
    int n=sc_read_at(path,header,sizeof(header),0);
    if(sc_stat(path,stat)||stat[0]!=1||n<24){copy(status,"Cannot read image header",sizeof(status));return 0;}
    /* 按真实内容分流。JPEG扩展名为.JPG或.JPEG都走同一签名；WebP
     * 的RIFF还必须带WEBP类型，不会把音频/AVI送进图片服务。 */
    if((header[0]==255 && header[1]==216)
        ||(header[0]==137 && header[1]=='P' && header[2]=='N' && header[3]=='G')
        ||(header[0]=='R' && header[1]=='I' && header[2]=='F' && header[3]=='F'
            && header[8]=='W' && header[9]=='E' && header[10]=='B' && header[11]=='P'))return load_compressed(path);
    candidate.bmp=0;candidate.version=1;candidate.bpp=1;
    candidate.top_down=1;candidate.offset=24;
    if(image_scb(header)){
        candidate.width=(int)image_u32(header+8);candidate.height=(int)image_u32(header+12);
        candidate.stride=(u32)candidate.width;
    }else if(n>=32&&image_scb2(header)){
        candidate.version=2;candidate.bpp=4;candidate.offset=32;
        candidate.width=(int)image_u32(header+8);candidate.height=(int)image_u32(header+12);
        candidate.stride=(u32)candidate.width*4;
    }else if(n>=54&&header[0]=='B'&&header[1]=='M'&&image_u32(header+14)>=40
        &&image_u16(header+26)==1&&(image_u16(header+28)==24||image_u16(header+28)==32)&&!image_u32(header+30)){
        candidate.bmp=1;candidate.width=(int)image_u32(header+18);
        int height=(int)image_u32(header+22);
        if(height==(int)0x80000000u){copy(status,"Invalid BMP height",sizeof(status));return 0;}
        candidate.height=height<0?-height:height;candidate.top_down=height<0;
        candidate.bpp=image_u16(header+28)/8;candidate.offset=image_u32(header+10);
        /* 先减法验证DIB，再加14，坏长度不能经无符号回绕落回头部。 */
        u32 dib=image_u32(header+14);
        if(dib>stat[1]-14||candidate.offset<14+dib||candidate.offset>stat[1]
            ||candidate.width<1||candidate.width>1920){copy(status,"Invalid BMP data offset",sizeof(status));return 0;}
        candidate.stride=((u32)candidate.width*candidate.bpp+3)&~3u;
    }else{copy(status,"Unsupported image format",sizeof(status));return 0;}
    /* 维度限界后乘法至多8294400，不给负值/大无符号宽高绕过正文。
     * SCB要求精确尾部；BMP保留原合同，允许正文之后的附加数据。 */
    if(candidate.width<1||candidate.height<1||candidate.width>1920||candidate.height>1080
        ||candidate.stride>sizeof(row_bytes)||candidate.offset>stat[1]
        ||candidate.stride*(u32)candidate.height>stat[1]-candidate.offset
        ||(!candidate.bmp&&stat[1]!=candidate.offset+candidate.stride*(u32)candidate.height)
        ||(!candidate.bmp&&candidate.version==2&&image_u32(header+24)!=candidate.stride*(u32)candidate.height)){
        copy(status,"Image dimensions / length invalid",sizeof(status));return 0;
    }
    pixels=sc_alloc((u32)candidate.width*candidate.height*4);
    if(!pixels){copy(status,"Not enough memory for image",sizeof(status));return 0;}
    for(int y=0;y<candidate.height;y++){
        if(!read_row(path,&candidate,y)){copy(status,"Image read failed",sizeof(status));goto failed;}
        /* 检查每个槽，不只检查概览采样点。最后一行坏槽也不能让
         * 前面已经写好的候选被误提交成合法照片。 */
        if(!candidate.bmp&&candidate.version==1)for(int x=0;x<candidate.width;x++)if(row_bytes[x]>PAL_UI_LINE){
            copy(status,"SCB uses unregistered palette slot",sizeof(status));goto failed;
        }
        for(int x=0;x<candidate.width;x++)pixels[y*candidate.width+x]=row_color(&candidate,x);
        if(!(y&7))sc_yield();
    }
    /* 候选完整成立之后才交换。旧帧还在本人内核画布，释放旧私有
     * 图像不影响上屏；下一帧正常提交新照片，不需内核事务接口。 */
    if(preview)sc_free(preview);
    release_fit();preview=pixels;image_w=cache_w=candidate.width;image_h=cache_h=candidate.height;
    scb_version=candidate.version;
    copy(filename,path,sizeof(filename));loaded=1;fit=1;pan_x=pan_y=dragging=0;
    copy(status,"Image opened / Fit or 100%, drag to pan",sizeof(status));return 1;
failed:
    sc_free(pixels);return 0;
}
static int load(const char *path)
{
    image_client_cancel(&compressed);
    int result=load_image(path);image_error=!result;
    if(result!=2)lens_operations++;return result;
}

static u32 filtered_pixel(int fx,int fy)
{
    /* fx/fy是已经夹在原图首末像素中心之间的非负Q8坐标。
     * 高位是左上邻居，低8位是向右/向下的覆盖比例；末行末列
     * 重复边缘像素，不能让x+1/y+1越过完整原图缓存。这里先在
     * 直通ARGB上建立alpha加权颜色，不能直接插值不可见RGB。
     * 否则全透明蓝像素与不透明白像素之间会出现一圈蓝边。 */
    int x=fx>>8,y=fy>>8,x1=x+1<image_w?x+1:x,y1=y+1<image_h?y+1:y;
    u32 wx=(u32)fx&255,wy=(u32)fy&255;
    const u32 weights[]={(256-wx)*(256-wy),wx*(256-wy),(256-wx)*wy,wx*wy};
    const u32 samples[]={preview[y*image_w+x],preview[y*image_w+x1],preview[y1*image_w+x],preview[y1*image_w+x1]};
    u32 alpha=0,red=0,green=0,blue=0;
    /* 权重和65536；alpha*通道*权重总和最大4261478400，连
     * 半分母四舍五入也小于u32上限。先做alpha加权，透明色不
     * 渗进邻居；输出仍为SCB2规定的直通ARGB，不改作品的字节。 */
    for(int i=0;i<4;i++){
        u32 weight=weights[i]*(samples[i]>>24);
        alpha+=weight;red+=weight*((samples[i]>>16)&255);
        green+=weight*((samples[i]>>8)&255);blue+=weight*(samples[i]&255);
    }
    if(!alpha)return 0;
    return ((alpha+32768)>>16)<<24|((red+alpha/2)/alpha)<<16|((green+alpha/2)/alpha)<<8|(blue+alpha/2)/alpha;
}
static void prepare_fit(int width,int height)
{
    if(fit_pixels&&fit_w==width&&fit_h==height)return;
    /* 分配失败同尺寸不每次hover重试，以免反复扫描页表制造迟滞。
     * 用户仍看到完整原图的最近邻Fit，100%不依赖过滤缓存。 */
    if(fit_attempt_w==width&&fit_attempt_h==height)return;
    if(fit_pixels)sc_free(fit_pixels);
    fit_pixels=0;fit_w=fit_h=0;fit_attempt_w=width;fit_attempt_h=height;
    u32 *pixels=sc_alloc((u32)width*height*4);
    if(!pixels)return;
    for(int y=0;y<height;y++){
        /* 把目标像素中心(y+1/2)映射到原图，再减去原图中心
         * 的1/2；这比x*原宽/目标宽对齐左边缘更对称。坐标量化
         * 为1/256像素；允许的1920边界保证乘法仍小于有符号
         * 32位上限，既不使用浮点也不依赖SCCC的64位扩展。 */
        int fy=ui_clamp((2*y+1)*image_h*128/height-128,0,(image_h-1)*256);
        for(int x=0;x<width;x++){
            int fx=ui_clamp((2*x+1)*image_w*128/width-128,0,(image_w-1)*256);
            pixels[y*width+x]=filtered_pixel(fx,fy);
        }
        if(!(y&7))sc_yield();
    }
    fit_pixels=pixels;fit_w=width;fit_h=height;
}
static void draw(void)
{
    small_window=UI_W<276||UI_H<148;
    viewport_w=viewport_h=0;
    if(small_window){
        dragging=0;ui_background(SC_THEME_FACE_ALT);
        int height=UI_H<22?UI_H:22;
        ui_button_box(4,(UI_H-height)/2,UI_W-8,height,UI_W>=104?"Enlarge":"+",0);
        if((ui_pressed&1)&&ui_hit(4,(UI_H-height)/2,UI_W-8,height))ui_action=9;
        return;
    }
    ui_header("LENS / IMAGE VIEWER",filename);
    const int ids[]={1,2,3,4};const char *labels[]={"Open","Fit","100%","Center"};
    int body=ui_toolbar(ids,labels,4,ui_compact?38:70);
    /* 工具条负责换行与命中，此处沿同一几何只补当前倍率的选择
     * 表面。不能让两个倍率都像未选中，逼用户从照片猜当前模式。 */
    int button_x=16,button_y=ui_compact?38:70;
    for(int i=0;i<4;i++){
        int width=length(labels[i])*8+20;
        if(button_x+width>UI_W-16){button_x=16;button_y+=ui_compact?26:36;}
        if((i==1&&fit)||(i==2&&!fit))ui_button_box(button_x,button_y,width,ui_compact?22:30,labels[i],1);
        button_x+=width+8;
    }
    work_x=16;work_y=body;work_w=UI_W-32;work_h=UI_H-(ui_compact?22:30)-12-work_y;
    ui_panel(work_x,work_y,work_w,work_h);
    /* 分别映射两个端点，150%余数不能被宽高单独取整吃掉；直接
     * 像素循环最后仍限制到真实客户区，画纸不能写进私有帧末页。 */
    viewport_x=ui_clamp(ui_px(work_x+1),0,ui_width);
    viewport_y=ui_clamp(ui_px(work_y+1),0,ui_height);
    viewport_w=ui_clamp(ui_px(work_x+work_w-1),viewport_x,ui_width)-viewport_x;
    viewport_h=ui_clamp(ui_px(work_y+work_h-1),viewport_y,ui_height)-viewport_y;
    if(loaded&&viewport_w&&viewport_h){
        u32 light=0xFF000000u|ui_role(SC_THEME_FACE),dark=0xFF000000u|ui_role(SC_THEME_FACE_ALT);
        for(int y=0;y<viewport_h;y++){
            u32 *out=ui_pixels+(viewport_y+y)*ui_width+viewport_x;
            for(int x=0;x<viewport_w;x++)*out++=((x/16+y/16)&1)?light:dark;
        }
        if(fit){
            int width=viewport_w,height=image_h*width/image_w;
            if(height>viewport_h){height=viewport_h;width=image_w*height/image_h;}
            /* 合法1×1080或1920×1细图也至少显示一条物理像素，
             * 不能因整数比例取零而把已经加载的图片画成空棋盘。 */
            if(width<1)width=1;
            if(height<1)height=1;
            if(width&&height){
                prepare_fit(width,height);
                int xx=viewport_x+(viewport_w-width)/2,yy=viewport_y+(viewport_h-height)/2;
                for(int y=0;y<height;y++)for(int x=0;x<width;x++){
                    int at=(yy+y)*ui_width+xx+x;
                    u32 color=fit_pixels?fit_pixels[y*width+x]:preview[(y*image_h/height)*image_w+x*image_w/width];
                    ui_pixels[at]=image_over(color,ui_pixels[at]);
                }
            }
        }else{
            int width=image_w<viewport_w?image_w:viewport_w,height=image_h<viewport_h?image_h:viewport_h;
            pan_x=ui_clamp(pan_x,0,image_w-width);pan_y=ui_clamp(pan_y,0,image_h-height);
            int xx=viewport_x+(viewport_w-width)/2,yy=viewport_y+(viewport_h-height)/2;
            for(int y=0;y<height;y++)for(int x=0;x<width;x++){
                int at=(yy+y)*ui_width+xx+x;
                ui_pixels[at]=image_over(preview[(pan_y+y)*image_w+pan_x+x],ui_pixels[at]);
            }
        }
        if(!ui_compact&&UI_W>=650){
            ui_number(UI_W-208,14,image_w,PAL_UI_TEXT);ui_text(UI_W-164,14,"x",PAL_UI_MUTED);
            ui_number(UI_W-140,14,image_h,PAL_UI_TEXT);
        }
    }else ui_text(work_x+8,work_y+8,"Open a picture",PAL_UI_MUTED);
    /* 缓存不足有可见说明；作品加载错误仍优先显示其真实原因。
     * 此处只在合法Fit已尝试却无缓存时给模式提示，不假报加载失败。 */
    ui_footer(!image_error&&loaded&&fit&&fit_attempt_w&&!fit_pixels?"Fit: nearest (filter memory unavailable)":status);
}
static void open_path(void)
{
    char candidate[128];copy(candidate,filename,sizeof(candidate));
    dragging=0;
    if(ui_edit_path(candidate,sizeof(candidate),"Open image / SCB BMP PNG JPG WebP"))load(candidate);
    else{copy(status,"Cancelled",sizeof(status));image_error=1;lens_operations++;}
}
int main(void)
{
    if(ui_open("Lens")<0)return 1;
    sc_palette(palette_rgb);
    /* 默认照片也是实际PNG解码路径，启动即展示用户选定的高清
     * 壁纸样张；显式文件参数仍优先，失败保留真实服务错误。 */
    char args[128];sc_args(args,sizeof(args));load(args[0]?args:filename);
    ui_pointer();draw();ui_present();
    for(;;){
        int completed=poll_compressed();
        int pointer[6];
        int raw_move=dragging&&!sc_pointer_peek(ui_win,pointer)&&pointer[5]
            &&(pointer[0]!=drag_x||pointer[1]!=drag_y);
        /* NUI按钮只需逻辑坐标变化，图片平移却要求物理1px也立即
         * 响应。只为真实位移越过逻辑取整门槛，静止拖按仍走节流；
         * 先更新平移再绘本帧，不能把最后1px延迟到下一秒刷新。 */
        int due=ui_frame_due();
        if(!raw_move&&!due&&!completed)continue;
        ui_pointer();
        /* due也观察真实排队按键/释放沿，保留模式按钮后的补帧。
         * 若其中一次让出后鼠标又动了，重新读物理点而不用旧快照。 */
        if(sc_pointer_peek(ui_win,pointer))dragging=raw_move=0;
        if(raw_move&&ui_focus&&(ui_buttons&1)&&!fit){
            pan_x+=drag_x-pointer[0];pan_y+=drag_y-pointer[1];
            drag_x=pointer[0];drag_y=pointer[1];
        }
        draw();ui_present();
        int action=ui_action,key=action?-1:sc_key();
        /* 历史PS/2事件1是F1，并非Ctrl-A：驱动目前不把Ctrl
         * 修饰组合转成控制字符。保留F1/小写f/数字1/Esc原路径，
         * 动作帧则让新模态框消费队列，不能提前丢掉文件名首字。 */
        if(key==27){image_client_cancel(&compressed);return 0;}
        if(action==9){sc_window(ui_win,1);continue;}
        if(action==1||key==1){open_path();continue;}
        if(action==2||key=='f'){fit=1;dragging=0;image_error=0;copy(status,"Fit image",sizeof(status));}
        if(action==3||key=='1'){fit=0;dragging=0;image_error=0;copy(status,"Original pixels / drag to pan",sizeof(status));}
        if(action==4||key=='c'){
            pan_x=image_w>viewport_w?(image_w-viewport_w)/2:0;
            pan_y=image_h>viewport_h?(image_h-viewport_h)/2:0;dragging=0;
        }
        if(sc_pointer_peek(ui_win,pointer))continue;
        int inside=ui_focus&&viewport_w&&viewport_h&&pointer[0]>=viewport_x&&pointer[0]<viewport_x+viewport_w
            &&pointer[1]>=viewport_y&&pointer[1]<viewport_y+viewport_h;
        if(loaded&&inside&&(ui_pressed&1)&&!action){dragging=1;drag_x=pointer[0];drag_y=pointer[1];}
        if(!(ui_buttons&1)||!ui_focus)dragging=0;
        if(loaded&&inside&&(ui_pressed&2)){fit=!fit;dragging=0;copy(status,fit?"Fit image":"Original pixels / drag to pan",sizeof(status));}
    }
}
