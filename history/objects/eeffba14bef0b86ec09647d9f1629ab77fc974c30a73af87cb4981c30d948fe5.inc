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
        int width=gfx_glyph16(scalar,bits);if(!width)width=16;
        width=terminal_unit(scalar=='\t'?32:width,scale);
        if(x+width>w->cw-margin && x>margin){x=margin;row++;}
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
static void terminal_redraw(win_t *w)
{
    terminal_t *t=terminal_of(w->handle);if(!t)return;
    int scale=terminal_scale(),margin=GFX_W==320?4:terminal_unit(12,scale);
    int step=terminal_unit(20,scale),height=terminal_unit(16,scale);
    int visible=(w->ch-2*margin-height)/step+1;if(visible<1)visible=1;
    u32 background=0xFF000000u|theme_color(TH_PAPER);
    u32 *out=(u32 *)w->canvas,count=(u32)w->cw*w->ch;
    __asm__ __volatile__("cld; rep stosl":"+D"(out),"+c"(count):"a"(background):"memory");
    extra_of(w->handle)->opaque=1;
    terminal_layout(w,t,0,0);
    int rows=(w->ty-margin)/step+1;
    terminal_layout(w,t,rows>visible?rows-visible:0,1);
    u32 info[32];theme_info(info);t->theme=info[2];
    t->width=w->cw;t->height=w->ch;t->scale=scale;dirty=1;
}
int wm_terminal_owned(int pid,int handle)
{
    return owned_window(pid,handle)!=0 && terminal_of(handle)!=0;
}
int wm_terminal_control(int pid,int handle,int command)
{
    win_t *w=owned_window(pid,handle);
    if(!w || command<0 || command>2 || extra_of(handle)->format!=2)return -1;
    terminal_t *t=terminal_of(handle);
    if(!t) {
        if(command)return -1;
        for(int i=0;i<WMAX;i++)if(!terminals[i].handle){t=&terminals[i];break;}
        char *history=(char *)pframe_alloc_run(4);if(!history)return -4;
        *t=(terminal_t){0};t->handle=handle;t->text=history;history[0]=0;
    }else if(command==1){t->used=0;t->text[0]=0;}
    t->width=0;dirty=1;return 0;
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
    t->width=0;dirty=1;return 0;
}
static void terminal_refresh_all(void)
{
    u32 info[32];theme_info(info);
    for(int i=0;i<nwins;i++) {
        win_t *w=&wins[i];terminal_t *t=terminal_of(w->handle);
        if(t && (t->theme!=info[2] || t->width!=w->cw || t->height!=w->ch || t->scale!=terminal_scale()))terminal_redraw(w);
    }
}
