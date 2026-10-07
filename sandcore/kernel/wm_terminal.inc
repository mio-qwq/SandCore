/* =====================================================================
 * mio：原生流式终端。属于WM私有实现，不是第二套用户字库。
 *
 * 保存UTF-8逻辑历史，显示时依据当前宽度/缩放取凤凰16px字形；改变
 * 主题/尺寸会重新排版，而非拷贝旧字体像素。两遍布局先算总行数再只
 * 绘最后可见行，长输出不需要反复搬动整个ARGB画布。输入删除按完整
 * 标量回退，即使字符因窗口变窄折行，也能干净恢复前一个提示符位置。
 * ===================================================================== */
static int terminal_scale(void){return GFX_W==320?100:display_scale();}
static int terminal_unit(int value,int scale){return value*scale/100;}
static void terminal_layout(win_t *w,terminal_t *t,int first,int draw)
{
    int scale=terminal_scale(),margin=terminal_unit(12,scale);
    if(GFX_W==320)margin=4;
    int step=terminal_unit(20,scale),glyph_h=terminal_unit(16,scale);
    int x=margin,row=0;const char *p=t->text;u32 scalar;u16 bits[16];
    while(*p) {
        gfx_utf8_next(&p,&scalar);
        if(scalar=='\r'){x=margin;continue;}
        if(scalar=='\n'){x=margin;row++;continue;}
        int width=gfx_layout_glyph16(scalar,bits);if(!width)width=16;
        width=terminal_unit(scalar=='\t'?32:width,scale);
        int right=w->cw-margin-(GFX_W==320?0:terminal_unit(16,scale));
        if(x+width>right && x>margin){x=margin;row++;}
        int y=margin+(row-first)*step;
        if(draw && scalar!='\t' && y>=margin && y+glyph_h<=w->ch-margin) {
            u32 color=0xFF000000u|theme_color(TH_TEXT);
            for(int r=0;r<glyph_h;r++)for(int c=0;c<width;c++)
                if(x+c<w->cw && (bits[r*100/scale]&(0x8000u>>(c*100/scale))))
                    ((u32 *)w->canvas)[(y+r)*w->cw+x+c]=color;
        }
        x+=width;
    }
    w->tx=x;w->ty=margin+(row-first)*step;
    if(draw && w->tx<w->cw-margin && w->ty>=margin && w->ty+glyph_h+2<w->ch-margin) {
        int width=terminal_unit(8,scale);
        for(int c=0;c<width && w->tx+c<w->cw-margin;c++)
            ((u32 *)w->canvas)[(w->ty+glyph_h+1)*w->cw+w->tx+c]=0xFF000000u|theme_color(TH_ACCENT);
    }
}
static void terminal_rect(win_t *w,int x,int y,int width,int height,int role)
{
    if(x<0){width+=x;x=0;}if(y<0){height+=y;y=0;}
    if(width>w->cw-x)width=w->cw-x;if(height>w->ch-y)height=w->ch-y;
    if(width<=0||height<=0)return;
    u32 color=0xFF000000u|theme_color(role);
    for(int row=0;row<height;row++)for(int col=0;col<width;col++)((u32 *)w->canvas)[(y+row)*w->cw+x+col]=color;
}
static void terminal_track(win_t *w,terminal_t *t,int *out)
{
    int scale=terminal_scale(),button=terminal_unit(18,scale);
    out[0]=w->cw-terminal_unit(18,scale);out[1]=terminal_unit(12,scale)+button;
    out[2]=terminal_unit(12,scale);out[3]=w->ch-terminal_unit(24,scale)-2*button;
    if(out[3]<1)out[3]=1;
    int rows=t->rows>0?t->rows:1;
    out[4]=out[3]*t->visible/rows;if(out[4]<terminal_unit(12,scale))out[4]=terminal_unit(12,scale);
    if(out[4]>out[3])out[4]=out[3];
    int max=t->rows-t->visible;if(max<0)max=0;
    out[5]=out[1]+(max?t->first*(out[3]-out[4])/max:0);
}
static void terminal_redraw(win_t *w)
{
    terminal_t *t=terminal_of(w->handle);if(!t)return;
    if(!window_visible(w)){t->width=0;return;}
    int scale=terminal_scale(),margin=GFX_W==320?4:terminal_unit(12,scale);
    int step=terminal_unit(20,scale),height=terminal_unit(16,scale);
    int visible=(w->ch-2*margin-height)/step+1;if(visible<1)visible=1;
    u32 background=0xFF000000u|theme_color(TH_PAPER);
    u32 *out=(u32 *)w->canvas,count=(u32)w->cw*w->ch;
    __asm__ __volatile__("cld; rep stosl":"+D"(out),"+c"(count):"a"(background):"memory");
    extra_of(w->handle)->opaque=1;
    terminal_layout(w,t,0,0);
    int rows=(w->ty-margin)/step+1;
    t->rows=rows;t->visible=visible;
    int max=rows>visible?rows-visible:0;
    if(t->follow)t->first=max;
    if(t->first>max)t->first=max;if(t->first<0)t->first=0;
    terminal_layout(w,t,t->first,1);
    if(GFX_W!=320){
        int b[6];terminal_track(w,t,b);
        terminal_rect(w,b[0],margin,b[2],w->ch-2*margin,TH_FACE_ALT);
        terminal_rect(w,b[0],b[5],b[2],b[4],t->grab?TH_ACCENT:TH_LINE);
        for(int r=0;r<4;r++){
            int inset=terminal_unit(5-r,scale),width=terminal_unit(2*r+1,scale);
            terminal_rect(w,b[0]+inset,margin+terminal_unit(5+r,scale),width,terminal_unit(1,scale),TH_MUTED);
            terminal_rect(w,b[0]+inset,w->ch-margin-terminal_unit(6+r,scale),width,terminal_unit(1,scale),TH_MUTED);
        }
    }
    u32 info[32];theme_info(info);t->theme=info[2];
    t->width=w->cw;t->height=w->ch;t->scale=scale;wm_request_compose();
}
int wm_terminal_owned(int pid,int handle)
{
    return owned_window(pid,handle)!=0 && terminal_of(handle)!=0;
}
int wm_terminal_size(int pid,int handle,int *columns,int *rows)
{
    win_t *w=owned_window(pid,handle);if(!w || !terminal_of(handle))return -1;
    int scale=terminal_scale(),margin=GFX_W==320?4:terminal_unit(12,scale),scroll=GFX_W==320?0:terminal_unit(16,scale);
    *columns=(w->cw-2*margin-scroll)/terminal_unit(8,scale);*rows=(w->ch-2*margin-terminal_unit(16,scale))/terminal_unit(20,scale)+1;
    if(*columns<1)*columns=1;if(*rows<1)*rows=1;return 0;
}
int wm_terminal_control(int pid,int handle,int command)
{
    win_t *w=owned_window(pid,handle);
    if(!w || command<0 || command>2 || extra_of(handle)->format!=2)return -1;
    terminal_t *t=terminal_of(handle);
    if(!t) {
        if(command)return -1;
        t=&window_record(handle)->terminal;
        char *history=(char *)pframe_alloc_run(4);if(!history)return -4;
        *t=(terminal_t){0};t->handle=handle;t->text=history;history[0]=0;t->follow=1;
    }else if(command==1){t->used=0;t->text[0]=0;t->first=0;t->follow=1;}
    t->width=0;window_changed(w);return 0;
}
int wm_terminal_puts(int pid,int handle,const char *text)
{
    win_t *w=owned_window(pid,handle);terminal_t *t=terminal_of(handle);
    if(!w || !t)return -1;
    if(!gfx_utf8_valid(text))return -2;
    for(int i=0;text[i];i++)if((u8)text[i]<32 && text[i]!='\n' && text[i]!='\r' && text[i]!='\t' && text[i]!='\b')return -2;
    const char *p=text;
    while(*p) {
        if(*p=='\b') {
            p++;
            if(t->used && t->text[t->used-1]!='\n') {
                t->used--;
                while(t->used && ((u8)t->text[t->used]&0xC0)==0x80)t->used--;
            }
            t->text[t->used]=0;continue;
        }
        const char *begin=p;u32 scalar;gfx_utf8_next(&p,&scalar);u32 bytes=(u32)(p-begin);
        while(t->used+bytes>=16384) {
            /* 优先删最早整条逻辑行，保证UTF-8和下一条提示符完整；若
             * 超长单行没有LF，仅删一个完整标量。不会将连续字节截半。 */
            u32 drop=0;while(drop<t->used && t->text[drop]!='\n')drop++;
            if(drop<t->used)drop++;
            else {const char *old=t->text;gfx_utf8_next(&old,&scalar);drop=(u32)(old-t->text);}
            for(u32 i=drop;i<t->used;i++)t->text[i-drop]=t->text[i];
            t->used-=drop;
        }
        while(begin<p)t->text[t->used++]=*begin++;
        t->text[t->used]=0;
    }
    /* 只改逻辑历史，在合成时一次重排。连续键入/CLI连续多行输出
     * 不再每个PUTS都在IF=0清整个大画布，减少抢占延迟和重复绘制。 */
    t->width=0;window_changed(w);return 0;
}
static void terminal_refresh_all(void)
{
    u32 info[32];theme_info(info);
    for(int i=0;i<nwins;i++) {
        win_t *w=&WINDOW(i);terminal_t *t=terminal_of(w->handle);
        if(t && (t->theme!=info[2] || t->width!=w->cw || t->height!=w->ch || t->scale!=terminal_scale()))terminal_redraw(w);
    }
}
static int terminal_mouse(int x,int y,u8 buttons,u8 previous)
{
    /* 只拦截终端自己的滚动条，文本点击/窗口标题/原键盘仍由原路径
     * 处理。拖动到条外继续按同一稳定handle更新，释放立即清grab。
     * 位移不改变first时不制造整帧dirty，静止鼠标不能烧满客体CPU。 */
    if(GFX_W==320)return 0;
    for(int i=nwins-1;i>=0;i--){
        win_t *w=&WINDOW(i);terminal_t *t=terminal_of(w->handle);
        if(!t || extra_of(w->handle)->hidden)continue;
        int cx=x-w->x-1,cy=y-w->y-TITLE_H,b[6];terminal_track(w,t,b);
        int hit=cx>=b[0] && cx<b[0]+b[2] && cy>=0 && cy<w->ch;
        if(!t->grab && (!hit || !(buttons&1) || (previous&1)))continue;
        if(!t->grab && hit_window(x,y)!=i)continue;
        int max=t->rows-t->visible;if(max<0)max=0;
        int first=t->first,grab=t->grab,raise=0;
        if(!(buttons&1)){t->grab=0;}
        else if(t->grab){
            int travel=b[3]-b[4],position=cy-b[1]-t->grab_offset;
            if(position<0)position=0;if(position>travel)position=travel;
            t->first=travel?position*max/travel:0;
        }else{
            raise=1;
            if(cy<b[1])t->first--;
            else if(cy>=b[1]+b[3])t->first++;
            else if(cy>=b[5] && cy<b[5]+b[4]){t->grab=1;t->grab_offset=cy-b[5];}
            else t->first+=cy<b[5]?-(t->visible-1):(t->visible-1);
            if(t->first<0)t->first=0;if(t->first>max)t->first=max;
        }
        t->follow=t->first==max;
        if(first!=t->first || grab!=t->grab){t->width=0;wm_damage_rect(w->x,w->y,w->w,w->h);}
        /* 置顶会重排wins，必须在使用w完成损伤标记后执行；terminal
         * 自身按稳定handle保存，不因置顶把另一个窗口的矩形标脏。 */
        if(raise)win_raise(i);
        return 1;
    }
    return 0;
}
