/* =====================================================================
 * mio：Lens 原生图片查看器。窗口、按钮、路径与图片属于普通三环任务。
 * 支持SCB索引位图及BI_RGB的24/32位BMP（正/负高度），拒绝压缩、
 * 调色板BMP、越界尺寸和不完整正文。BMP保留真实RGB，SCB1查原槽位。
 *
 * ARGB界面后台在高端私有堆，960x540真彩色预览约2MB留在私有映像。
 * 大图按行READAT读取，fit缓存只加速概览；100%模式再次读原始像素，
 * 不把小预览拉伸当成原图。拖动按源像素移动，窗口改变重新裁工作区。
 * 图像读取失败清空可用标记，绝不把上次图片冒充本次打开成功。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "IMAGE.inc"
static u32 preview[960*540];
static u8 row_bytes[1920*4];
static u32 palette_rgb[256];
static char filename[64]="HOME/WELCOME.SCB",status[100]="Open SCB or uncompressed BMP";
static int image_w,image_h,cache_w,cache_h,bmp,scb_version,bpp,top_down,loaded,fit=1,pan_x,pan_y,dragging,drag_x,drag_y;
static u32 data_offset,stride;
static inline void make_rgb_table(void)
{

    sc_palette(palette_rgb);
    /* 图片属于作品，不随UI主题覆盖同号槽位；SCB1查原DAC RGB。
     * BMP直接保留24位RGB，取消过去的5bit/槽位近似量化。 */
}
static inline int read_row(int y)
{
    int yy=top_down?y:image_h-1-y;
    return sc_read_at(filename,row_bytes,stride,data_offset+(u32)yy*stride)==(int)stride;
}
static inline u32 row_color(int x)
{
    if(scb_version==2)return image_u32(row_bytes+x*4);
    if(!bmp)return 0xFF000000u|palette_rgb[row_bytes[x]];
    u8 *p=row_bytes+x*bpp;
    return 0xFF000000u|((u32)p[2]<<16)|((u32)p[1]<<8)|p[0];
}
static inline int load(const char *path)
{

    loaded=0;
    u32 stat[2];
    u8 header[64];
    int header_bytes=sc_read_at(path,header,64,0);
    if(sc_stat(path,stat)||stat[0]!=1||header_bytes<24){
        copy(status,"Cannot read image header",sizeof(status));
        return 0;

    }
    copy(filename,path,64);
    bmp=0;
    scb_version=1;
    top_down=1;
    data_offset=24;
    bpp=1;
    if(image_scb(header)){
        image_w=(int)image_u32(header+8);
        image_h=(int)image_u32(header+12);
        stride=image_w;

    }
    else if(header_bytes>=32 && image_scb2(header)){
        scb_version=2;bpp=4;data_offset=32;
        image_w=(int)image_u32(header+8);image_h=(int)image_u32(header+12);
        stride=(u32)image_w*4;
    }
    else if(header_bytes>=54&&header[0]=='B'&&header[1]=='M'&&image_u32(header+14)>=40
    &&image_u16(header+26)==1&&(image_u16(header+28)==24||image_u16(header+28)==32)&&!image_u32(header+30)){

        bmp=1;
        image_w=(int)image_u32(header+18);
        int h=(int)image_u32(header+22);
        if(h==(int)0x80000000u){
            copy(status,"Invalid BMP height",sizeof(status));
            return 0;

        }
        image_h=h<0?-h:h;
        top_down=h<0;
        bpp=image_u16(header+28)/8;
        data_offset=image_u32(header+10);
        /* DIB长度先减法比较，再做14+DIB，避免0xffffffff一加绕回去。
         * 维度先限界，再计算行跨度；坏外来头不能绕过正文边界。 */
        u32 dib=image_u32(header+14);
        if(dib>stat[1]-14 || data_offset<14+dib || data_offset>stat[1]
           || image_w<1 || image_w>1920){
            copy(status,"Invalid BMP data offset",sizeof(status));
            return 0;

        }
        stride=((u32)image_w*bpp+3)&~3u;

    }
    else{
        copy(status,"SCB or BI_RGB 24/32-bit BMP only",sizeof(status));
        return 0;

    }
    if(image_w<1||image_h<1||image_w>1920||image_h>1080||stride>sizeof(row_bytes)
    ||data_offset>stat[1]||stride*(u32)image_h>stat[1]-data_offset
    ||(!bmp&&stat[1]!=data_offset+stride*(u32)image_h)
    ||(scb_version==2&&!bmp&&image_u32(header+24)!=stride*(u32)image_h)){
        copy(status,"Image dimensions / length invalid",sizeof(status));
        return 0;

    }
    cache_w=image_w;
    cache_h=image_h;
    if(cache_w>960){
        cache_h=cache_h*960/cache_w;
        cache_w=960;

    }
    if(cache_h>540){
        cache_w=cache_w*540/cache_h;
        cache_h=540;

    }
    if(!cache_w)cache_w=1;
    if(!cache_h)cache_h=1;
    int next=0;
    /* SCB完整逐行校验槽位，尚未收录的槽不悄悄显示黑色。缓存可以
     * 缩小，但验证不能只查被采样到的行，避免隐藏损坏正文。 */
    for(int y=0;y<image_h;y++){
        if(!read_row(y)){
            copy(status,"Image read failed",sizeof(status));
            return 0;

        }
        if(!bmp&&scb_version==1)for(int x=0;x<image_w;x++)if(row_bytes[x]>PAL_UI_LINE){
            copy(status,"SCB uses unregistered palette slot",sizeof(status));
            return 0;

        }
        while(next<cache_h&&next*image_h/cache_h==y){
            for(int x=0;x<cache_w;x++)preview[next*cache_w+x]=row_color(x*image_w/cache_w);
            next++;

        }
    }
    loaded=1;
    fit=1;
    pan_x=pan_y=0;
    copy(status,"Image opened / Fit or 100%, drag to pan",sizeof(status));
    return 1;
}
static inline void draw(void)
{

    ui_header("LENS / IMAGE VIEWER",filename);
    ui_control(1,16,70,88,"Open",0);
    ui_control(2,112,70,80,"Fit",fit);
    ui_control(3,200,70,88,"100%",!fit);
    ui_control(4,296,70,96,"Center",0);
    int x=16,y=116,w=UI_W-32,h=UI_H-162;
    ui_panel(x,y,w,h);
    if(loaded&&h>0){
        int px=ui_px(x+1),py=ui_px(y+1),pw=ui_px(w-2),ph=ui_px(h-2);
        /* 工作区按真实物理像素绘图。图片缩放不再乘UI比例，100%就是
         * 一个源像素对应一个屏幕像素，字体/按钮缩放与图片倍率分离。 */
        for(int r=0;r<ph;r++)for(int c=0;c<pw;c++)ui_pixels[(py+r)*ui_width+px+c]=ui_color(((c/16+r/16)&1)?PAL_UI_PANEL:PAL_UI_NIGHT+3);
        if(fit){
            int dw=pw,dh=image_h*dw/image_w;
            if(dh>ph){
                dh=ph;
                dw=image_w*dh/image_h;

            }
            int xx=px+(pw-dw)/2,yy=py+(ph-dh)/2;
            if(dw&&dh)for(int r=0;r<dh;r++)for(int c=0;c<dw;c++){
                int at=(yy+r)*ui_width+xx+c;
                ui_pixels[at]=image_over(preview[(r*cache_h/dh)*cache_w+c*cache_w/dw],ui_pixels[at]);
            }

        }
        else{

            int visible_w=image_w<pw?image_w:pw,visible_h=image_h<ph?image_h:ph;
            pan_x=ui_clamp(pan_x,0,image_w-visible_w);
            pan_y=ui_clamp(pan_y,0,image_h-visible_h);
            int xx=px+(pw-visible_w)/2,yy=py+(ph-visible_h)/2;
            for(int r=0;r<visible_h;r++)if(read_row(pan_y+r))for(int c=0;c<visible_w;c++){
                int at=(yy+r)*ui_width+xx+c;ui_pixels[at]=image_over(row_color(pan_x+c),ui_pixels[at]);
            }

        }
        ui_number(UI_W-208,14,image_w,PAL_UI_TEXT);
        ui_text(UI_W-164,14,"x",PAL_UI_MUTED);
        ui_number(UI_W-140,14,image_h,PAL_UI_TEXT);

    }
    else ui_text(x+24,y+24,"Open a picture to begin.",PAL_UI_MUTED);
    ui_footer(status);
}
int main(void)
{

    if(ui_open("Lens")<0)return 1;
    make_rgb_table();
    char args[64];
    sc_args(args,64);
    if(args[0])copy(filename,args,64);
    load(filename);
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();
        draw();
        ui_present();
        int k=sc_key(),a=ui_action;
        if(k==27)return 0;
        if(a==1||k==1){
            char p[64];
            copy(p,filename,64);
            if(ui_edit_path(p,64,"Open SCB / BMP"))load(p);

        }
        if(a==2||k=='f')fit=1;
        if(a==3||k=='1')fit=0;
        if(a==4){
            pan_x=pan_y=0;

        }
        if((ui_pressed&1)&&ui_hit(16,116,UI_W-32,UI_H-162)){
            dragging=1;
            drag_x=ui_x;
            drag_y=ui_y;

        }
        if(dragging&&(ui_buttons&1)&&!fit){
            pan_x+=(drag_x-ui_x)*ui_scale/100;
            pan_y+=(drag_y-ui_y)*ui_scale/100;
            drag_x=ui_x;
            drag_y=ui_y;

        }
        if(!(ui_buttons&1))dragging=0;
        if(ui_pressed&2){
            fit=!fit;
            copy(status,fit?"Fit image":"Original pixels / drag to pan",sizeof(status));

        }

    }
}
