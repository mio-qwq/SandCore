/* =====================================================================
 * mio：M8 原生桌面扩展，属于 wm.c 同一职责的私有实现片段。
 * 像素坐标用于原生窗口；旧窗口保存自己的逻辑画布，显示比例只在
 * 合成/鼠标坐标转换时应用。所有状态用稳定handle关联，不挪走资源。
 * ===================================================================== */
static int hit_window(i32 x,i32 y);
static int menu_open,menu_first,menu_selected;
static int context_open,context_x,context_y,context_window=-1,context_icon=-1;
static int icon_first;
static int unit(int value){
    return GFX_W==320?value:value*display_scale()/100;
}
static u32 native_ui_slot(u8 slot)
{
    if(slot==PAL_UI_TEXT)return theme_color(TH_TEXT);
    if(slot==PAL_UI_MUTED)return theme_color(TH_MUTED);
    if(slot==PAL_UI_LINE)return theme_color(TH_LINE);
    if(slot==PAL_UI_ALERT)return theme_color(TH_ALERT);
    if(slot>=PAL_UI_CYAN && slot<PAL_UI_CYAN+8)return theme_color(TH_ACCENT);
    return display_rgb(slot);
}
static int rounded_inset(int row,int height,int radius)
{
    if(!radius || (row>=radius && row<height-radius))return 0;
    int dy=row<radius?radius-1-row:row-(height-radius),dx=radius;
    while(dx*dx+dy*dy>radius*radius)dx--;
    return radius-dx;
}
static void native_round(int x,int y,int w,int h,int radius,u32 rgb)
{
    if(radius>w/2)radius=w/2;
    if(radius>h/2)radius=h/2;
    for(int row=0;row<h;row++){
        int inset=rounded_inset(row,h,radius);
        gfx_rgb_fill(x+inset,y+row,w-2*inset,1,rgb);
    }
}
static void native_bevel(int x,int y,int w,int h,int pressed)
{
    /* Classic表面与两对方向相反的边对应真实凸/凹状态。无圆角、
     * 无现代透明投影；旧画布尺寸不改，边缘占窗口已有的1px边框。 */
    u32 light=theme_color(pressed?TH_DARK_EDGE:TH_HIGHLIGHT);
    u32 dark=theme_color(pressed?TH_HIGHLIGHT:TH_DARK_EDGE);
    gfx_rgb_fill(x,y,w,h,theme_color(TH_FACE));
    gfx_rgb_fill(x,y,w,1,light);gfx_rgb_fill(x,y,1,h,light);
    gfx_rgb_fill(x,y+h-1,w,1,dark);gfx_rgb_fill(x+w-1,y,1,h,dark);
}
static void native_text_rgb(int x,int y,const char *text,u32 color)
{
    while(*text){
        u32 scalar;u16 rows[16];gfx_utf8_next(&text,&scalar);
        int width=gfx_glyph16(scalar,rows);if(!width)width=16;
        for(int r=0;r<16;r++)for(int c=0;c<width;c++)if(rows[r]&(0x8000u>>c))
            gfx_rgb_fill(x+unit(c),y+unit(r),unit(c+1)-unit(c),unit(r+1)-unit(r),color);
        x+=unit(width);
    }
}
static void native_text(int x,int y,const char *text,u8 color)
{
    native_text_rgb(x,y,text,native_ui_slot(color));
}
static int native_elide(char *out,const char *text,int width)
{
    /* 省略按凤凰字形前进宽计算，复制完整UTF-8标量，不能以“11字节”
     * 截开汉字。只有显示副本变短，真实标签/执行路径永远不受影响。 */
    int n=0,pixels=0;
    while(*text){
        const char *begin=text;u32 scalar;u16 rows[16];gfx_utf8_next(&text,&scalar);
        int advance=gfx_glyph16(scalar,rows);if(!advance)advance=16;
        int reserve=*text?unit(24):0;
        if(pixels+unit(advance)+reserve>width){
            if(width>=unit(24)){out[n++]='.';out[n++]='.';out[n++]='.';pixels+=unit(24);}
            break;
        }
        while(begin<text && n<60)out[n++]=*begin++;
        pixels+=unit(advance);
    }
    out[n]=0;return pixels;
}
static int native_resize(win_t *w,int width,int height)
{

    window_extra_t *e=extra_of(w->handle);
    if(width>GFX_W)width=GFX_W;
    if(height>work_bottom())height=work_bottom();
    if(width<96)width=96;
    if(height<TITLE_H+40)height=TITLE_H+40;
    if(e->native) {

        int cw=width-2,ch=height-TITLE_H-1;
        if(cw==w->cw && ch==w->ch){
            w->w=width;
            w->h=height;
            return 0;

        }
        int bytes=e->format==2?4:1;
        u32 pages=((u32)cw*ch*bytes+4095)/4096;
        u8 *canvas=(u8 *)pframe_alloc_run(pages);
        if(!canvas)return -1;
        for(int y=0;y<ch;y++)for(int x=0;x<cw;x++) {
            u32 color=(y<w->ch && x<w->cw)?canvas_pixel(w,y*w->cw+x)
                       :(e->format==2?0xFF000000u|display_rgb(PAL_CON_BG):PAL_CON_BG);
            if(bytes==4)((u32 *)canvas)[y*cw+x]=color; else canvas[y*cw+x]=(u8)color;
        }
        pframe_free_run((u32)w->canvas,e->pages);
        e->pages=pages;
        w->canvas=canvas;
        w->cw=cw;
        w->ch=ch;
        w->w=width;
        w->h=height;

    }
    else {

        /* 旧程序没有resize事件合同，不能改变它提交的FRAME长度。
         * 拖动尺寸转换为整数显示倍数，原画布/字体/键盘路径原样保留。 */
        int sx=(width-2)/w->cw,sy=(height-TITLE_H-1)/w->ch;
        e->scale=sx<sy?sx:sy;
        if(e->scale<1)e->scale=1;
        if(e->scale>4)e->scale=4;
        w->w=w->cw*e->scale+2;
        w->h=w->ch*e->scale+TITLE_H+1;

    }
    if(w->x+w->w>GFX_W)w->x=GFX_W-w->w;
    if(w->x<0)w->x=0;
    if(w->y+w->h>work_bottom())w->y=work_bottom()-w->h;
    if(w->y<0)w->y=0;
    wm_request_compose();
    return 0;
}
void wm_display_changed(void)
{

    menu_open=context_open=0;
    resizing=-1;
    drag=-1;
    for(int i=0;i<nwins;i++) {

        win_t *w=&wins[i];
        window_extra_t *e=extra_of(w->handle);
        if(!e->native) {

            e->scale=GFX_W==320?1:(display_scale()+99)/100;
            while(e->scale>1 && (w->cw*e->scale+2>GFX_W || w->ch*e->scale+TITLE_H+1>work_bottom()))e->scale--;
            w->w=w->cw*e->scale+2;
            w->h=w->ch*e->scale+TITLE_H+1;

        }
        else native_resize(w,e->maximized?GFX_W:w->w,e->maximized?work_bottom():w->h);
        w->x=(GFX_W-w->w)/2;
        w->y=(work_bottom()-w->h)/2;
        if(w->x<0)w->x=0;
        if(w->y<0)w->y=0;

    }
    wm_request_compose();
}
int wm_open_native_window(int pid,const char *title,int width,int height)
{

    if(width>GFX_W)width=GFX_W;
    if(height>work_bottom())height=work_bottom();
    int result=win_create(pid,title,(GFX_W-width)/2,(work_bottom()-height)/2,width,height,1);
    if(result>=0)wm_request_compose();
    return result;
}
int wm_open_rgb_window(int pid,const char *title,int width,int height)
{
    if(width>GFX_W)width=GFX_W;
    if(height>work_bottom())height=work_bottom();
    int result=win_create(pid,title,(GFX_W-width)/2,(work_bottom()-height)/2,width,height,2);
    if(result>=0)wm_request_compose();
    return result;
}
int wm_user_frame_rgb(int pid,int handle,const u32 *pixels,u32 bytes)
{
    win_t *w=owned_window(pid,handle);
    /* RGB长度以字节计，不能把像素数传来后只覆盖四分之一帧。完整
     * owner/格式/尺寸检查先于任何写入。接近8MB不能全程CLI：PIT
     * 和8042要继续收硬件。当前任务/CR3和窗口生命周期由内部持有
     * 固定，IRQ不能切其它用户任务或主循环，因此目标不会被关闭/
     * resize，源映射也不会回收，合成器不能读取尚在复制的半帧。 */
    if(!w || extra_of(handle)->format!=2 || bytes!=(u32)w->cw*w->ch*4) return -1;
    u32 changed=0,alpha=0xFF000000u,*target=(u32 *)w->canvas;
    u32 first=bytes/4,last=0;
    task_render_hold(1);sti();
    for(u32 i=0;i<bytes/4;i++) {
        u32 value=pixels[i],difference=target[i]^value;
        if(difference){if(first==bytes/4)first=i;last=i;changed|=difference;}
        target[i]=value;
        alpha&=value;
    }
    /* alpha/dirty旁表在短IF=0内与这张完整帧一起发布；所有成功
     * 返回都已解除持有，非法参数从未进入长复制/开IRQ路径。 */
    cli();task_render_hold(0);
    /* 同样的用户帧仍完成合法提交，但不唤醒整屏合成。旧程序反复
     * FRAME不会制造虚假动画，真正改变的任意alpha/RGB字节仍触发更新。 */
    if(changed){
        /* 已经逐字比较，就顺便记录首/末改变行；不额外遍历8MB，
         * 不按每个改变像素做除法。横向保守覆盖整行，原子复制/
         * alpha统计/原字节保持，合成器只需重画这段物理客户区。 */
        int top=(int)(first/(u32)w->cw),bottom=(int)(last/(u32)w->cw)+1;
        wm_damage_rect(w->x+1,w->y+TITLE_H+top,w->cw,bottom-top);
    }
    extra_of(handle)->opaque=(alpha&0xFF000000u)==0xFF000000u;
    return 0;
}
int wm_user_fill_rgb(int pid,int handle,int x,int y,int width,int height,u32 rgb)
{
    win_t *w=owned_window(pid,handle);
    if(!w || extra_of(handle)->format!=2) return -1;
    if(width<=0 || height<=0) return 0;
    signed long long right=(signed long long)x+width,bottom=(signed long long)y+height;
    int x0=x<0?0:x,y0=y<0?0:y;
    int x1=right>w->cw?w->cw:(int)right,y1=bottom>w->ch?w->ch:(int)bottom;
    if(x0>=x1 || y0>=y1)return 0;
    /* 先裁剪一次，再做整数字串填充；小矩形不再扫描整个客户区。
     * alpha始终255，整区覆盖才可证明画布重新完全不透明。 */
    u32 pixel=0xFF000000u|(rgb&0xFFFFFFu);
    for(int row=y0;row<y1;row++){
        u32 *out=((u32 *)w->canvas)+row*w->cw+x0,count=(u32)(x1-x0);
        __asm__ __volatile__("cld; rep stosl":"+D"(out),"+c"(count):"a"(pixel):"memory");
    }
    if(!x0 && !y0 && x1==w->cw && y1==w->ch)extra_of(handle)->opaque=1;
    wm_request_compose();return 0;
}
int wm_user_text_rgb(int pid,int handle,int x,int y,const char *text,u32 rgb)
{
    win_t *w=owned_window(pid,handle);
    if(!w || extra_of(handle)->format!=2 || x < -32768 || x > 32767 || y < -32768 || y > 32767) return -1;
    int left=x,count=0;u32 scalar;u16 rows[16];
    while(*text) {
        gfx_utf8_next(&text,&scalar);
        if(scalar=='\n'){x=left;y+=18;continue;}
        if(scalar=='\r'){x=left;continue;}
        int width=gfx_glyph16(scalar,rows);if(!width)width=16;
        for(int row=0;row<16;row++)for(int col=0;col<width;col++)
            if(x+col>=0 && x+col<w->cw && y+row>=0 && y+row<w->ch && (rows[row]&(0x8000u>>col)))
                ((u32 *)w->canvas)[(y+row)*w->cw+x+col]=0xFF000000u|(rgb&0xFFFFFFu);
        x+=width;count++;
    }
    wm_request_compose();return count;
}
int wm_window_control(int pid,int handle,int command)
{

    win_t *w=owned_window(pid,handle);
    if(!w)return -1;
    window_extra_t *e=extra_of(handle);
    if(command==0){
        e->hidden=1;
        wm_request_compose();
        return 0;

    }
    if(command==1) {

        if(!e->maximized){
            e->restore_x=w->x;
            e->restore_y=w->y;
            e->restore_w=w->w;
            e->restore_h=w->h;

        }
        if(native_resize(w,GFX_W,work_bottom()))return -1;
        w->x=w->y=0;
        e->maximized=1;
        e->hidden=0;

    }
    else if(command==2) {

        e->hidden=0;
        if(e->maximized){
            if(native_resize(w,e->restore_w,e->restore_h))return -1;
            w->x=e->restore_x;
            w->y=e->restore_y;
            e->maximized=0;

        }

    }
    else return -1;
    for(int i=0;i<nwins;i++)if(wins[i].handle==handle){
        win_raise(i);
        break;

    }
    wm_request_compose();
    return 0;
}
static int pointer_snapshot(int pid,int handle,i32 *out,int consume)
{

    win_t *w=owned_window(pid,handle);
    if(!w)return -1;
    window_extra_t *e=extra_of(handle);
    int x=mouse_x()-w->x-1,y=mouse_y()-w->y-TITLE_H;
    if(!e->native){
        x/=e->scale;
        y/=e->scale;

    }
    int focused=wm_focused(pid) && !menu_open && !context_open;
    out[0]=x;
    out[1]=y;
    out[2]=focused?mouse_buttons():0;
    out[3]=focused?(i32)e->pressed:0;
    out[4]=focused?(i32)e->released:0;
    out[5]=focused;
    if(consume)e->pressed=e->released=0;
    return 0;
}
int wm_user_pointer(int pid,int handle,i32 *out){return pointer_snapshot(pid,handle,out,1);}
int wm_user_pointer_peek(int pid,int handle,i32 *out){return pointer_snapshot(pid,handle,out,0);}
u32 wm_legacy_mouse(int pid)
{

    int top=top_visible();
    if(GFX_W==320 || top<0 || wins[top].owner!=pid || extra_of(wins[top].handle)->native)
    return (u32)(mouse_x()|(mouse_y()<<9)|(mouse_buttons()<<18));
    win_t *w=&wins[top];
    window_extra_t *e=extra_of(w->handle);
    int x=(mouse_x()-w->x-1)/e->scale+1,y=(mouse_y()-w->y-TITLE_H)/e->scale+13;
    u32 buttons=mouse_buttons();
    if(x<0||x>511||y<0||y>511)buttons=0;
    if(x<0)x=0;
    if(x>511)x=511;
    if(y<0)y=0;
    if(y>511)y=511;
    return (u32)(x|(y<<9)|(buttons<<18));
}
static void native_draw_window(int index)
{

    win_t *w=&wins[index];
    window_extra_t *e=extra_of(w->handle);
    i32 clip[4];gfx_clip_bounds(clip);int pad=unit(8);
    /* 光照投影也属于窗口占据的区域。只跳过连投影都不相交的
     * 对象，不能仅按客户区裁而漏掉被下层损伤覆盖的阴影/圆角。 */
    if(w->x-pad>=clip[0]+clip[2] || w->y-pad>=clip[1]+clip[3]
       || w->x+w->w+pad<=clip[0] || w->y+w->h+pad<=clip[1])return;
    int focus=index==top_visible();
    int radius=theme_classic()?0:unit(theme_radius());
    if(theme_classic()){
        if(!e->opaque)native_bevel(w->x,w->y,w->w,w->h,0);
        else{
            gfx_rgb_fill(w->x,w->y,w->w,1,theme_color(TH_HIGHLIGHT));
            gfx_rgb_fill(w->x,w->y,1,w->h,theme_color(TH_HIGHLIGHT));
            gfx_rgb_fill(w->x,w->y+w->h-1,w->w,1,theme_color(TH_DARK_EDGE));
            gfx_rgb_fill(w->x+w->w-1,w->y,1,w->h,theme_color(TH_DARK_EDGE));
        }
    }
    else{
        /* 两层低透明度圆角投影保留壁纸细节；不能把整块深色矩形
         * 当作投影。只画阴影覆盖区，主面板稍后遮住中心。 */
        for(int row=0;row<w->h+unit(4);row++){
            if(w->y+unit(3)+row<clip[1] || w->y+unit(3)+row>=clip[1]+clip[3])continue;
            int inset=rounded_inset(row,w->h+unit(4),radius+unit(2));
            int end=w->w+unit(4)-inset,local_y=row+unit(3);
            int inside=local_y<w->h?rounded_inset(local_y,w->h,radius):0;
            int left=local_y<w->h?unit(2)+inside:end;
            int right=local_y<w->h?unit(2)+w->w-inside:end;
            if(left>end)left=end;
            if(right<inset)right=inset;
            /* 中心随后被不透明窗口覆盖，跳过它不改变任何最终像素，
             * 却避免1080p每个窗口先做一整块无效的透明乘除。 */
            for(int col=inset;col<left;col++)
                gfx_argb_pset(w->x-unit(2)+col,w->y+unit(3)+row,
                              0x18000000u|theme_color(TH_SHADOW));
            for(int col=right;col<end;col++)
                gfx_argb_pset(w->x-unit(2)+col,w->y+unit(3)+row,
                              0x18000000u|theme_color(TH_SHADOW));
        }
        if(!e->opaque){
            native_round(w->x,w->y,w->w,w->h,radius,theme_color(TH_LINE));
            native_round(w->x+1,w->y+1,w->w-2,w->h-2,radius>0?radius-1:0,theme_color(TH_FACE));
        }else for(int row=0;row<w->h;row++){
            if(w->y+row<clip[1] || w->y+row>=clip[1]+clip[3])continue;
            /* mio：整画布已证明不透明，客户区将完整覆盖中间。仅画
             * 露出的圆角边框/内表面；仍使用相同两条圆角轮廓，不能
             * 为加速把圆角抹平。透明帧回到完整基底再alpha合成。 */
            int a=rounded_inset(row,w->h,radius),b=w->w-a;
            if(row<TITLE_H || row>=w->h-1)gfx_rgb_fill(w->x+a,w->y+row,b-a,1,theme_color(TH_LINE));
            else{
                int l=a>1?a:1,r=w->w-l;
                gfx_rgb_fill(w->x+a,w->y+row,l-a,1,theme_color(TH_LINE));
                gfx_rgb_fill(w->x+r,w->y+row,b-r,1,theme_color(TH_LINE));
            }
            if(row>0 && row<w->h-1){
                int c=1+rounded_inset(row-1,w->h-2,radius>0?radius-1:0),d=w->w-c;
                if(row<TITLE_H)gfx_rgb_fill(w->x+c,w->y+row,d-c,1,theme_color(TH_FACE));
                else{
                    int l=a>1?a:1,r=w->w-l;
                    if(c<l)gfx_rgb_fill(w->x+c,w->y+row,l-c,1,theme_color(TH_FACE));
                    if(d>r)gfx_rgb_fill(w->x+r,w->y+row,d-r,1,theme_color(TH_FACE));
                }
            }
        }
    }
    u32 title=theme_color(focus?TH_TITLE_ACTIVE:TH_TITLE_INACTIVE);
    for(int row=1;row<TITLE_H;row++){
        int inset=rounded_inset(row,w->h,radius);if(inset<1)inset=1;
        gfx_rgb_fill(w->x+inset,w->y+row,w->w-2*inset,1,title);
    }
    native_text_rgb(w->x+unit(10),w->y+unit(4),w->title,theme_color(focus?TH_TITLE_TEXT:TH_TITLE_MUTED));
    int box=unit(24),cx=w->x+w->w-box;
    for(int b=0;b<3;b++){
        int bx=cx-b*box,hover=mouse_x()>=bx && mouse_x()<bx+box && mouse_y()>=w->y && mouse_y()<w->y+TITLE_H;
        if(theme_classic())native_bevel(bx+unit(3),w->y+unit(3),box-unit(6),TITLE_H-unit(6),hover&&(mouse_buttons()&1));
        else native_round(bx+unit(2),w->y+unit(2),box-unit(4),TITLE_H-unit(4),unit(6),
                          theme_color(hover?(b==0?TH_ALERT:TH_SELECT):TH_FACE_ALT));
        native_text_rgb(bx+unit(7),w->y+unit(3),b==0?"x":b==1?(e->maximized?"o":"+"):"-",theme_color(TH_TEXT));
    }
    for(int row=0;row<w->ch;row++){
        if(w->y+TITLE_H+row<clip[1] || w->y+TITLE_H+row>=clip[1]+clip[3])continue;
        int inset=rounded_inset(TITLE_H+row,w->h,radius);
        if(e->format==2) {
            int start=inset>1?inset-1:0,end=w->w-inset-1;
            if(end>w->cw)end=w->cw;
            if(end>start){
                const u32 *pixels=((u32 *)w->canvas)+row*w->cw+start;
                if(e->opaque)gfx_opaque_blit(w->x+1+start,w->y+TITLE_H+row,end-start,1,pixels,w->cw);
                else gfx_argb_blit(w->x+1+start,w->y+TITLE_H+row,end-start,1,pixels,w->cw);
            }
            continue;
        }
        for(int col=0;col<w->cw;col++) {
        if(col+1<inset || col+1>=w->w-inset)continue;
        u8 color=w->canvas[row*w->cw+col];
        if(e->scale==1)gfx_pset(w->x+1+col,w->y+TITLE_H+row,color);
        else gfx_fill(w->x+1+col*e->scale,w->y+TITLE_H+row*e->scale,e->scale,e->scale,color);
        }
    }
    for(int i=2;i<unit(12);i+=unit(3))gfx_line(w->x+w->w-i,w->y+w->h-2,w->x+w->w-2,w->y+w->h-i,PAL_UI_LINE);
}
static int task_button_width(void)
{
    int available=GFX_W-unit(250),width=nwins?available/nwins:unit(100);
    if(width>unit(140))width=unit(140);
    if(width<unit(40))width=unit(40);
    return width;
}
static int rank_window(int slot)
{
    for(int i=0;i<nwins;i++){
        int rank=0;
        for(int j=0;j<nwins;j++)if(wins[j].handle<wins[i].handle)rank++;
        if(rank==slot)return i;

    }
    return -1;
}
static void native_draw_taskbar(void)
{

    int y=work_bottom(),height=GFX_H-y;
    gfx_rgb_fill(0,y,GFX_W,height,theme_color(TH_FACE));
    gfx_rgb_fill(0,y,GFX_W,1,theme_color(theme_classic()?TH_HIGHLIGHT:TH_LINE));
    if(theme_classic())native_bevel(unit(8),y+unit(4),unit(100),height-unit(8),menu_open);
    else native_round(unit(8),y+unit(4),unit(100),height-unit(8),unit(10),theme_color(menu_open?TH_SELECT:TH_FACE_ALT));
    native_text_rgb(unit(18),y+unit(8),"沙核",theme_color(TH_TEXT));
    native_text_rgb(unit(56),y+unit(8),"START",theme_color(TH_ACCENT));
    int bw=task_button_width();
    for(int slot=0;slot<nwins;slot++) {

        int i=rank_window(slot),x=unit(116)+slot*bw;
        window_extra_t *e=extra_of(wins[i].handle);
        if(theme_classic())native_bevel(x,y+unit(4),bw-unit(4),height-unit(8),i==top_visible());
        else native_round(x,y+unit(4),bw-unit(4),height-unit(8),unit(8),theme_color(i==top_visible()?TH_SELECT:TH_FACE_ALT));
        char label[64];native_elide(label,wins[i].title,bw-unit(12));
        native_text_rgb(x+unit(6),y+unit(8),label,theme_color(e->hidden?TH_MUTED:TH_TEXT));

    }
    char clock[12];
    int value=sc_ticks/100,n=0;
    do{
        clock[n++]=(char)('0'+value%10);
        value/=10;

    }
    while(value);
    for(int i=0;i<n/2;i++){
        char c=clock[i];
        clock[i]=clock[n-i-1];
        clock[n-i-1]=c;

    }
    clock[n]=0;
    native_text(GFX_W-unit(98),y+unit(8),clock,PAL_UI_MUTED);
    native_text(GFX_W-unit(30),y+unit(8),"s",PAL_UI_MUTED);
}
static void icon_position(int index,int *x,int *y)
{
    int rows=(work_bottom()-unit(40))/unit(112);
    if(rows<1)rows=1;
    index-=icon_first;
*x=unit(28)+(index/rows)*unit(128);
*y=unit(28)+(index%rows)*unit(112);
}
static int icon_capacity(void)
{
    int rows=(work_bottom()-unit(40))/unit(112),cols=(GFX_W-unit(28))/unit(128);
    if(rows<1)rows=1;
    if(cols<1)cols=1;
    return rows*cols;
}
static void native_draw_icons(void)
{

    int capacity=icon_capacity();
    if(icon_first>=desktop_count(0))icon_first=0;
    for(int i=icon_first;i<desktop_count(0)&&i<icon_first+capacity;i++) {

        int x,y;
        icon_position(i,&x,&y);
        const desk_item_t *item=desktop_item(0,i);
        desktop_icon(x+unit(22),y,unit(48),item);
        /* 名称在图标下方居中；宽度超出单格先做视觉省略，执行命令
         * 始终保留完整127B，不能把绘制截断用来截断真实文件路径。 */
        char label[64];int width=native_elide(label,item->label,unit(96));
        int label_x=x+(unit(96)-width)/2,label_y=y+unit(56);
        native_text_rgb(label_x+unit(1),label_y+unit(1),label,theme_color(TH_DESKTOP_SHADOW));
        native_text_rgb(label_x,label_y,label,theme_color(TH_DESKTOP_TEXT));

    }
    if(desktop_count(0)>capacity){
        native_text(GFX_W-unit(72),unit(8),"<  >",PAL_UI_TEXT);

    }
}
static int desktop_hit(int x,int y)
{
    for(int i=icon_first;i<desktop_count(0)&&i<icon_first+icon_capacity();i++){
        int ix,iy;
        icon_position(i,&ix,&iy);
        if(x>=ix&&x<ix+unit(96)&&y>=iy&&y<iy+unit(82))return i;

    }
    return -1;
}
static int menu_rows(void)
{
    int rows=(work_bottom()-unit(80))/(GFX_W==320?22:unit(40));
    if(rows>8)rows=8;
    if(rows<2)rows=2;
    return rows;
}
static void menu_geometry(int *x,int *y,int *width,int *height)
{
*width=GFX_W==320?288:unit(360);
*height=unit(60)+menu_rows()*(GFX_W==320?22:unit(40));
*x=unit(8);
*y=work_bottom()-*height-unit(8);
    if(*y<0)*y=0;
}
static void native_draw_overlays(void)
{

    if(menu_open) {

        int x,y,w,h;
        menu_geometry(&x,&y,&w,&h);
        if(theme_classic())native_bevel(x,y,w,h,0);
        else{
            native_round(x,y,w,h,unit(theme_radius()),theme_color(TH_LINE));
            native_round(x+1,y+1,w-2,h-2,unit(theme_radius()),theme_color(TH_FACE));
        }
        native_text(x+unit(16),y+unit(10),"SANDCORE / APPLICATIONS",PAL_UI_CYAN+7);
        int rowheight=GFX_W==320?22:unit(40);
        for(int row=0;row<menu_rows();row++) {

            int index=menu_first+row;
            const desk_item_t *item=desktop_item(1,index);
            if(!item)break;
            int yy=y+unit(38)+row*rowheight;
            if(index==menu_selected)native_round(x+unit(8),yy,w-unit(16),rowheight,
                                                theme_classic()?0:unit(6),theme_color(TH_SELECT));
            desktop_icon(x+unit(15),yy+unit(2),GFX_W==320?16:unit(24),item);
            native_text_rgb(x+unit(52),yy+unit(3),item->label,theme_color(index==menu_selected?TH_SELECT_TEXT:TH_TEXT));

        }
        native_text(x+unit(16),y+h-unit(20),"< Prev",PAL_UI_CYAN+7);
        native_text(x+unit(112),y+h-unit(20),"Next >",PAL_UI_CYAN+7);
        native_text(x+unit(232),y+h-unit(20),"ESC",PAL_UI_MUTED);

    }
    if(context_open) {

        int rowheight=GFX_W==320?22:unit(32),width=GFX_W==320?190:unit(232);
        if(theme_classic())native_bevel(context_x,context_y,width,rowheight*5,0);
        else{
            native_round(context_x,context_y,width,rowheight*5,unit(theme_radius()),theme_color(TH_LINE));
            native_round(context_x+1,context_y+1,width-2,rowheight*5-2,unit(theme_radius()),theme_color(TH_FACE));
        }
        const char *window_labels[]={
            "Restore","Minimize","Maximize","Close","Cancel"
        };
        const char *desk_labels[]={
            context_icon>=0?"Open":"Open Files","Settings","Refresh","Remove shortcut","Cancel"
        };
        for(int i=0;i<5;i++){
            int yy=context_y+i*rowheight,hover=mouse_x()>=context_x && mouse_x()<context_x+width && mouse_y()>=yy && mouse_y()<yy+rowheight;
            if(hover)native_round(context_x+unit(4),yy+unit(2),width-unit(8),rowheight-unit(4),theme_classic()?0:unit(6),theme_color(TH_SELECT));
            native_text_rgb(context_x+unit(12),yy+unit(3),context_window>=0?window_labels[i]:desk_labels[i],theme_color(hover?TH_SELECT_TEXT:TH_TEXT));
        }

    }
    if(resizing>=0) {

        win_t *w=owned_window(wins[resizing].owner,wins[resizing].handle);
        if(w){
            gfx_line(w->x,w->y,w->x+resize_w,w->y,PAL_UI_CYAN+7);
            gfx_line(w->x+resize_w,w->y,w->x+resize_w,w->y+resize_h,PAL_UI_CYAN+7);
            gfx_line(w->x,w->y+resize_h,w->x+resize_w,w->y+resize_h,PAL_UI_CYAN+7);

        }

    }
}
static int native_menu_key(u8 key)
{

    if(!menu_open)return 0;
    if(key==27)menu_open=0;
    else if(key==0x80 && menu_selected>0)menu_selected--;
    else if(key==0x81 && menu_selected+1<desktop_count(1))menu_selected++;
    else if(key==10){
        const desk_item_t *item=desktop_item(1,menu_selected);
        if(item)task_exec_scx(item->command);
        menu_open=0;

    }
    if(menu_selected<menu_first)menu_first=menu_selected;
    if(menu_selected>=menu_first+menu_rows())menu_first=menu_selected-menu_rows()+1;
    wm_request_compose();
    return 1;
}
static int native_mouse(int x,int y,u8 buttons,u8 previous)
{

    int down=(buttons&1)&&!(previous&1),right=(buttons&2)&&!(previous&2);
    int top=top_visible();
    if(top>=0) {
        window_extra_t *e=extra_of(wins[top].handle);
        e->pressed|=(buttons&~previous);
        e->released|=(previous&~buttons);

    }
    if(resizing>=0) {

        if(resizing>=nwins){
            resizing=-1;
            return 1;

        }
        win_t *w=&wins[resizing];
        /* 同一抓取偏移贯穿预览与提交。宽高的增量恰等于鼠标
         * 从按下点移动的增量，不把抓取区内的位置误算成缩量。 */
        resize_w=x-w->x+1+resize_ox;
        resize_h=y-w->y+1+resize_oy;
        if(!(buttons&1)){
            native_resize(w,resize_w,resize_h);
            resizing=-1;

        }
        return 1;

    }
    /* 客户区右键交给应用；只有桌面和窗口标题属于系统菜单。
     * 否则 Files/画板自己的上下文菜单会被合成器永远抢走。 */
    int under=hit_window(x,y);
    if(right && (under<0 || y<wins[under].y+TITLE_H)) {

        menu_open=0;
        context_open=1;
        context_window=under<0?-1:wins[under].handle;
        context_icon=desktop_hit(x,y);
        context_x=x;
        context_y=y;
        int width=GFX_W==320?190:unit(232),height=(GFX_W==320?22:unit(32))*5;
        if(context_x+width>GFX_W)context_x=GFX_W-width;
        if(context_y+height>work_bottom())context_y=work_bottom()-height;
        if(context_y<0)context_y=0;
        return 1;

    }
    if(context_open && down) {

        int rowheight=GFX_W==320?22:unit(32),width=GFX_W==320?190:unit(232);
        int row=(y-context_y)/rowheight;
        if(x>=context_x && x<context_x+width && y>=context_y && row<5) {

            if(context_window>=0) {

                for(int i=0;i<nwins;i++)if(wins[i].handle==context_window){
                    win_t *w=&wins[i];
                    if(row==0)wm_window_control(w->owner,w->handle,2);
                    if(row==1)wm_window_control(w->owner,w->handle,0);
                    if(row==2)wm_window_control(w->owner,w->handle,1);
                    if(row==3)task_stop(w->owner);
                    break;

                }

            }
            else {

                const desk_item_t *item=desktop_item(0,context_icon);
                if(row==0)task_exec_scx(item?item->command:"apps/files.scx");
                if(row==1)task_exec_scx("apps/settings.scx");
                if(row==2)desktop_reload();
                if(row==3 && item){
                    fs_remove(item->link);
                    desktop_reload();

                }

            }

        }
        context_open=0;
        return 1;

    }
    if(down && y>=work_bottom() && x< (GFX_W==320?40:unit(112))) {

        menu_open=!menu_open;
        menu_first=menu_selected=0;
        return 1;

    }
    if(menu_open) {

        int xx,yy,width,height;
        menu_geometry(&xx,&yy,&width,&height);
        int rowheight=GFX_W==320?22:unit(40),row=(y-yy-unit(38))/rowheight;
        if(down&&y>=yy+height-unit(24)&&y<yy+height&&x>=xx&&x<xx+width){

            if(x<xx+unit(100)){
                menu_first-=menu_rows();
                if(menu_first<0)menu_first=0;

            }
            else if(x<xx+unit(216)&&menu_first+menu_rows()<desktop_count(1))menu_first+=menu_rows();
            menu_selected=menu_first;
            return 1;

        }
        if(x>=xx && x<xx+width && y>=yy+unit(38) && row>=0 && row<menu_rows()) {

            menu_selected=menu_first+row;
            if(down){
                const desk_item_t *item=desktop_item(1,menu_selected);
                if(item)task_exec_scx(item->command);
                menu_open=0;

            }

        }
        else if(down)menu_open=0;
        return 1;

    }
    if(GFX_W==320)return 0;
/* 原 VGA 窗口命中/拖动/图标路径原样保留 */
    if(down) {

        if(y>=work_bottom()) {

            int slot=(x-unit(116))/task_button_width(),i=rank_window(slot);
            if(x>=unit(116) && i>=0){
                extra_of(wins[i].handle)->hidden=0;
                win_raise(i);

            }
            return 1;

        }
        int i=hit_window(x,y);
        if(i<0){

            if(y<unit(28)&&x>=GFX_W-unit(80)&&desktop_count(0)>icon_capacity()){

                if(x<GFX_W-unit(42)){
                    icon_first-=icon_capacity();
                    if(icon_first<0)icon_first=0;

                }
                else if(icon_first+icon_capacity()<desktop_count(0))icon_first+=icon_capacity();
                return 1;

            }
            int icon=desktop_hit(x,y);
            const desk_item_t *item=desktop_item(0,icon);
            if(item)task_exec_scx(item->command);
            return 1;

        }
        win_raise(i);
        i=nwins-1;
        win_t *w=&wins[i];
        window_extra_t *e=extra_of(w->handle);
        if(y<w->y+TITLE_H) {

            int box=unit(24),cx=w->x+w->w-box;
            if(x>=cx)task_stop(w->owner);
            else if(x>=cx-box)wm_window_control(w->owner,w->handle,e->maximized?2:1);
            else if(x>=cx-2*box)wm_window_control(w->owner,w->handle,0);
            else {
                drag=i;
                drag_ox=x-w->x;
                drag_oy=y-w->y;

            }

        }
        else if(x>=w->x+w->w-unit(16) && y>=w->y+w->h-unit(16)){
            resizing=i;
            resize_w=w->w;
            resize_h=w->h;
            resize_ox=w->x+w->w-1-x;
            resize_oy=w->y+w->h-1-y;

        }

    }
    if(!(buttons&1))drag=-1;
    if(drag>=0 && (buttons&1)) {

        win_t *w=&wins[drag];
        w->x=x-drag_ox;
        w->y=y-drag_oy;
        if(w->x<0)w->x=0;
        if(w->y<0)w->y=0;
        if(w->x>GFX_W-w->w)w->x=GFX_W-w->w;
        if(w->y>work_bottom()-w->h)w->y=work_bottom()-w->h;

    }
    return 1;
}
