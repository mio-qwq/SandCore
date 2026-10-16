static void t_damage(ScUiTerminal *m,ScI32 first,ScI32 last)
{
    if(first<0)first=0;
    if(last>=(ScI32)m->screen->rows)last=(ScI32)m->screen->rows-1;
    if(!m->has_damage){
        m->dirty_first=first;
        m->dirty_last=last;
        m->has_damage=1;
    }
    else {
        if(first<m->dirty_first)m->dirty_first=first;
        if(last>m->dirty_last)m->dirty_last=last;
    }
}
static void t_flush_damage(ScUiTerminal *m)
{
    if(!m->has_damage)return;
    m->revision++;
    if(!m->revision)m->revision=1;
    if(m->context && m->node){
        ScUiRect r={
            0,m->dirty_first*(ScI32)m->cell_height,m->width,(m->dirty_last-m->dirty_first+1)*(ScI32)m->cell_height
        };
        if(!m->width || !m->cell_height || m->view_back)ScUiInvalidate(m->context,m->node,0);
        else ScUiInvalidate(m->context,m->node,&r);
    }
    m->has_damage=0;
}
static ScStatus t_metrics(ScUiTerminal *m)
{
    ScUiMetrics metrics;
    t_record(&metrics,sizeof(metrics));
    ScStatus status=ScUiMetricsQuery(m->context,m->node,&metrics);
    if(status<0)return status;
    ScU32 stamp=metrics.font_epoch^(metrics.font_pixels<<16)^(m->font_role<<24)^metrics.scale_percent;
    if(!stamp)stamp=1;
    if(m->font_epoch==stamp && m->cell_width && m->cell_height)return 0;
    ScUiText text={
        sizeof(text),1,"M",1
    };
    ScUiTextMetrics size;
    t_record(&size,sizeof(size));
    status=ScUiMeasureText(m->context,m->font_role,&text,INT_MAX,0,&size);
    if(status<0)return status;
    m->cell_width=size.width?size.width:1;
    m->cell_height=size.height+(metrics.unit?metrics.unit:1);
    m->font_epoch=stamp;
    memset(m->widths,0,sizeof(m->widths));
    return 0;
}
static ScUiTerminal *t_find(TermClass *owner,ScUiNode node)
{
    for(ScUiTerminal *m=owner->first;m;m=m->next)if(m->node==node)return m;
    return 0;
}
static void t_unlink(ScUiTerminal *m)
{
    if(!m->owner)return;
    if(m->previous)m->previous->next=m->next;
    else m->owner->first=m->next;
    if(m->next)m->next->previous=m->previous;
    m->previous=m->next=0;
    m->owner=0;
}
static void t_node_drop(void *context,void *unused)
{
    (void)unused;
    ScUiTerminal *m=(ScUiTerminal *)context;
    if(!t_valid(m) || !m->node_owned)return;
    t_unlink(m);
    m->node_owned=0;
    m->node=0;
    m->context=0;
    ScUiTerminalRelease(m);
}
static void t_class_drop(void *context,void *unused)
{
    (void)unused;
    TermClass *owner=(TermClass *)context;
    while(owner->first)t_node_drop(owner->first,0);
    TermClass **link=&term_classes;
    while(*link && *link!=owner)link=&(*link)->next;
    if(*link)*link=owner->next;
    free(owner);
}
static ScStatus t_measure(ScUiContext *context,ScUiNode node,const ScUiSize *constraint,ScUiSize *out,void *data)
{
    (void)context;
    (void)constraint;
    ScUiTerminal *m=t_find((TermClass *)data,node);
    if(!m)return SC_UI_E_STALE;
    ScStatus status=t_metrics(m);
    if(status<0)return status;
    out->width=(ScI32)(m->screen->columns*m->cell_width+16);
    out->height=(ScI32)(m->screen->rows*m->cell_height);
    return 0;
}
static ScStatus t_arrange(ScUiContext *context,ScUiNode node,const ScUiRect *rectangle,void *data)
{
    (void)context;
    ScUiTerminal *m=t_find((TermClass *)data,node);
    if(!m)return SC_UI_E_STALE;
    /* SCUI给隐藏Tab页安排0×0矩形；保留最后的真实PTY网格，不能把
     * 后台nano/输出缩成1×1并裁掉正文。重新可见时才采用新布局。 */
    if(rectangle->width<=0 || rectangle->height<=0)return 0;
    ScStatus status=t_metrics(m);
    if(status<0)return status;
    ScU32 columns=rectangle->width>16?(ScU32)(rectangle->width-16)/m->cell_width:1;
    ScU32 rows=rectangle->height>0?(ScU32)rectangle->height/m->cell_height:1;
    if(!columns)columns=1;
    if(columns>512)columns=512;
    if(!rows)rows=1;
    if(rows>256)rows=256;
    status=ScUiTerminalResize(m,columns,rows);
    if(status<0)return status;
    m->width=rectangle->width;
    m->height=rectangle->height;
    return 0;
}
static ScU32 t_color(ScU32 color,int background)
{
    static const ScU32 palette[16]={
        SC32_COLOR_SHADOW,SC32_COLOR_ALERT,SC32_COLOR_GREEN,SC32_COLOR_GOLD,
        SC32_COLOR_ACCENT_DEEP,SC32_COLOR_VIOLET,SC32_COLOR_ACCENT,SC32_COLOR_TEXT,
        SC32_COLOR_MUTED,SC32_COLOR_ROSE,SC32_COLOR_GREEN,SC32_COLOR_GOLD,
        SC32_COLOR_ACCENT,SC32_COLOR_VIOLET,SC32_COLOR_SELECT_TEXT,SC32_COLOR_HIGHLIGHT
    };
    return color<16?palette[color]:background?SC32_COLOR_PAPER:SC32_COLOR_TEXT;
}
static void t_scrollbar(ScUiTerminal *m,ScUiRect *track,ScUiRect *thumb)
{
    track->x=m->width-14;
    track->y=0;
    track->width=14;
    track->height=m->height;
    *thumb=*track;
    ScU32 total=t_total(m),visible=m->screen->rows;
    ScI32 height=total?(ScI32)((ScU32)(m->height>0?m->height:0)*visible/total):m->height;
    if(height<12)height=12;
    if(height>m->height)height=m->height;
    thumb->height=height;
    ScU32 limit=(m->flags&SC_UI_TERM_ALTERNATE)?0:m->history_count;
    thumb->y=limit?(ScI32)((ScU32)(m->height-height)*(limit-m->view_back)/limit):0;
}
static ScStatus t_paint(ScUiContext *context,ScUiNode node,ScUiPainter *painter,void *data)
{
    (void)context;
    ScUiTerminal *m=t_find((TermClass *)data,node);
    if(!m)return SC_UI_E_STALE;
    ScUiRect full={
        0,0,m->width,m->height
    };
    ScUiPainterFill(painter,&full,SC32_COLOR_PAPER);
    ScU32 base=(m->flags&SC_UI_TERM_ALTERNATE)?0:m->history_count-m->view_back;
    ScU32 first=0,a=0,last=0,b=0;
    int selection=t_selection_range(m,&first,&a,&last,&b);
    for(ScU32 y=0;y<m->screen->rows;y++){
        TermRow *r=t_row_at(m,base+y);
        if(!r)break;
        for(ScU32 x=0;x<r->columns && x<m->screen->columns;x++){
            TermCell *c=r->cells+x;
            if(c->flags&SC_UI_TERM_CONTINUATION)continue;
            ScU32 width=(c->flags&SC_UI_TERM_WIDE)?2:1;
            int selected=selection && base+y>=first && base+y<=last && (base+y!=first || x+width>a) && (base+y!=last || x<b);
            ScU32 foreground=t_color((c->flags&SC_UI_TERM_BOLD) && c->foreground<8?c->foreground+8:c->foreground,0),background=t_color(c->background,1);
            if(c->flags&SC_UI_TERM_INVERSE){
                ScU32 swap=foreground;
                foreground=background;
                background=swap;
            }
            if(selected){
                foreground=SC32_COLOR_SELECT_TEXT;
                background=SC32_COLOR_SELECT;
            }
            ScUiRect rectangle={
                (ScI32)(x*m->cell_width),(ScI32)(y*m->cell_height),(ScI32)(width*m->cell_width),(ScI32)m->cell_height
            };
            if(background!=SC32_COLOR_PAPER)ScUiPainterFill(painter,&rectangle,background);
            if(c->scalar!=' ' || c->marks){
                char encoded[4];
                ScU32 bytes;
                ScUiText text;
                ScView view;
                t_record(&text,sizeof(text));
                if(c->marks){
                    t_record(&view,sizeof(view));
                    ScStringView(c->marks,&view);
                    text.data=(const char *)view.data;
                    text.bytes=view.bytes;
                }
                else {
                    ScUtf8Encode(c->scalar,encoded,4,&bytes);
                    text.data=encoded;
                    text.bytes=bytes;
                }
                ScU32 flags=(c->flags&SC_UI_TERM_UNDERLINE)?SC_UI_TEXT_UNDERLINE:0;
                ScStatus status=ScUiPainterTextColor(painter,&rectangle,&text,m->font_role,flags,foreground);
                if(status<0)return status;
            }
        }
    }
    ScUiNodeInfo info;
    t_record(&info,sizeof(info));
    if(!m->view_back && (m->flags&SC_UI_TERM_CURSOR) && !ScUiNodeQuery(m->context,m->node,&info) && (info.state&SC_UI_FOCUSED)){
        ScUiRect cursor={
            (ScI32)(m->screen->column*m->cell_width),(ScI32)((m->screen->row+1)*m->cell_height)-2,(ScI32)m->cell_width,2
        };
        ScUiPainterFill(painter,&cursor,SC32_COLOR_ACCENT);
    }
    ScUiRect track,thumb;
    t_scrollbar(m,&track,&thumb);
    ScUiPainterFill(painter,&track,SC32_COLOR_FACE_ALT);
    ScUiPainterFill(painter,&thumb,m->scroll_grab?SC32_COLOR_ACCENT:SC32_COLOR_LINE);
    return 0;
}
static ScStatus t_key(ScUiTerminal *m,const ScUiEvent *event)
{
    ScU32 key=event->key,mods=event->modifiers;
    if((mods&(SC32_MOD_CONTROL|SC32_MOD_SHIFT))==(SC32_MOD_CONTROL|SC32_MOD_SHIFT)){
        if(key=='c' || key=='C'){
            ScStatus status=ScUiTerminalCopy(m);
            return status==SC_UI_E_NOT_FOUND?SC_UI_HANDLED:status<0?status:SC_UI_HANDLED;
        }
        if(key=='v' || key=='V'){
            ScStatus status=ScUiTerminalPaste(m);
            return status<0?status:SC_UI_HANDLED;
        }
        if(key==SC32_KEY_HOME || key==SC32_KEY_END){
            ScUiTerminalScroll(m,key==SC32_KEY_HOME?INT_MAX:INT_MIN);
            return SC_UI_HANDLED;
        }
        /* 未由控件占用的桌面快捷键继续交公共命令表，不能变成孩子的Ctrl-N/W。 */
        return 0;
    }
    if((mods&SC32_MOD_SHIFT) && (key==SC32_KEY_PAGEUP || key==SC32_KEY_PAGEDOWN)){
        ScUiTerminalScroll(m,key==SC32_KEY_PAGEUP?(ScI32)m->screen->rows:-(ScI32)m->screen->rows);
        return SC_UI_HANDLED;
    }
    const char *sequence=0;
    unsigned char text[8];
    ScU32 bytes=0;
    if((mods&SC32_MOD_CONTROL) && key<128){
        ScU32 c=key;
        if(c>='a' && c<='z')c-=32;
        if(c>='@' && c<='_'){
            text[bytes++]=(unsigned char)(c&31);
        }
    }
    else if((mods&SC32_MOD_ALT) && key>=32 && key<127){
        text[bytes++]=27;
        text[bytes++]=(unsigned char)key;
    }
    else if(key==8 || key==127)text[bytes++]=127;
    else if(key==13 || key==10)text[bytes++]=10;
    else if(key==9 || key==27)text[bytes++]=(unsigned char)key;
    else if(key>=SC32_KEY_UP && key<=SC32_KEY_RIGHT){
        const char *arrows="ABDC";
        text[0]=27;
        text[1]=(m->flags&SC_UI_TERM_APPLICATION_CURSOR)?'O':'[';
        text[2]=(unsigned char)arrows[key-SC32_KEY_UP];
        bytes=3;
    }
    else if(key==SC32_KEY_HOME)sequence="\033[H";
    else if(key==SC32_KEY_END)sequence="\033[F";
    else if(key==SC32_KEY_INSERT)sequence="\033[2~";
    else if(key==SC32_KEY_DELETE)sequence="\033[3~";
    else if(key==SC32_KEY_PAGEUP)sequence="\033[5~";
    else if(key==SC32_KEY_PAGEDOWN)sequence="\033[6~";
    else if(key>=SC32_KEY_F1 && key<SC32_KEY_F1+12){
        static const char *functions[]={
            "\033[11~","\033[12~","\033[13~","\033[14~","\033[15~","\033[17~","\033[18~","\033[19~","\033[20~","\033[21~","\033[23~","\033[24~"
        };
        sequence=functions[key-SC32_KEY_F1];
    }
    if(sequence){
        ScStatus status=t_queue(m,sequence,(ScU32)strlen(sequence));
        return status<0?status:SC_UI_HANDLED;
    }
    if(bytes){
        ScStatus status=ScUiTerminalSend(m,text,bytes,0);
        return status<0?status:SC_UI_HANDLED;
    }
    return 0;
}
static void t_pointer_position(ScUiTerminal *m,ScI32 x,ScI32 y,ScU32 *row,ScU32 *column)
{
    if(x<0)x=0;
    if(y<0)y=0;
    ScU32 r=(ScU32)y/m->cell_height,c=(ScU32)x/m->cell_width;
    if(r>=m->screen->rows)r=m->screen->rows-1;
    if(c>m->screen->columns)c=m->screen->columns;
    ScU32 base=(m->flags&SC_UI_TERM_ALTERNATE)?0:m->history_count-m->view_back;
    TermRow *line=t_row_at(m,base+r);
    if(c>line->columns)c=line->columns;
    if(c<line->columns && (line->cells[c].flags&SC_UI_TERM_CONTINUATION) && c)c--;
    *row=line->id;
    *column=c;
}
static ScStatus t_event(ScUiContext *context,const ScUiEvent *event,void *data)
{
    ScUiTerminal *m=t_find((TermClass *)data,event->node);
    if(!m)return 0;
    if(event->type==SC_UI_EVENT_KEY_DOWN)return t_key(m,event);
    if(event->type==SC_UI_EVENT_TEXT){
        if(event->modifiers&(SC32_MOD_CONTROL|SC32_MOD_ALT))return SC_UI_HANDLED;
        if(event->scalar<32 || event->scalar==127)return SC_UI_HANDLED;
        char bytes[4];
        ScU32 written;
        ScStatus status=ScUtf8Encode(event->scalar,bytes,4,&written);
        if(!status)status=ScUiTerminalSend(m,bytes,written,0);
        return status<0?status:SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_FOCUS_IN || event->type==SC_UI_EVENT_FOCUS_OUT){
        t_damage(m,(ScI32)m->screen->row,(ScI32)m->screen->row);
        t_flush_damage(m);
        return 0;
    }
    if(event->type==SC_UI_EVENT_WHEEL){
        ScI32 delta=event->message?(ScI32)event->message->data[2]:0;
        if(delta>1000)delta=1000;
        if(delta< -1000)delta=-1000;
        ScUiTerminalScroll(m,delta*3);
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_DOWN && (event->buttons&1)){
        ScUiFocus(context,m->node);
        ScStatus status=ScUiCapture(context,m->node,1);
        if(status<0)return status;
        if(event->x>=m->width-14)m->scroll_grab=1;
        else {
            ScUiTerminalSelection selection;
            t_record(&selection,sizeof(selection));
            t_pointer_position(m,event->x,event->y,&selection.anchor_row,&selection.anchor_column);
            selection.active_row=selection.anchor_row;
            selection.active_column=selection.anchor_column;
            ScUiTerminalSelect(m,&selection);
            m->selecting=1;
        }
    }
    if((event->type==SC_UI_EVENT_POINTER_DOWN || event->type==SC_UI_EVENT_POINTER_MOVE) && m->scroll_grab){
        ScUiRect track,thumb;
        t_scrollbar(m,&track,&thumb);
        ScI32 y=event->y-thumb.height/2,space=track.height-thumb.height;
        if(y<0)y=0;
        if(y>space)y=space;
        ScU32 limit=(m->flags&SC_UI_TERM_ALTERNATE)?0:m->history_count,next=space>0?limit-(ScU32)y*limit/(ScU32)space:0;
        m->view_back=next;
        t_damage(m,0,(ScI32)m->screen->rows-1);
        t_flush_damage(m);
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_MOVE && m->selecting){
        ScUiTerminalSelection selection=m->selection;
        t_pointer_position(m,event->x,event->y,&selection.active_row,&selection.active_column);
        ScUiTerminalSelect(m,&selection);
        m->selecting=1;
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_UP || event->type==SC_UI_EVENT_CAPTURE_LOST || event->type==SC_UI_EVENT_INPUT_SYNC){
        m->selecting=m->scroll_grab=0;
        if(event->type==SC_UI_EVENT_POINTER_UP)ScUiCapture(context,m->node,0);
        return 0;
    }
    return 0;
}
ScStatus ScUiTerminalSetFont(ScUiTerminal *m,ScU32 role)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(role>SC_UI_FONT_CODE)return SC_UI_E_INVALID;
    m->font_role=role;
    m->font_epoch=0;
    if(!m->context || !m->node)return SC_UI_E_STALE;
    return ScUiNodeSetFont(m->context,m->node,role);
}
ScStatus ScUiTerminalCreate(ScUiContext *context,const ScUiTerminalDesc *desc,ScUiTerminal **out,ScUiNode *node)
{
    if(!context || !desc || !out || !node || desc->size!=sizeof(*desc) || desc->version!=1 || desc->reserved[0] || desc->reserved[1] || desc->reserved[2])return SC_UI_E_INVALID;
    if(!desc->columns || desc->columns>512 || !desc->rows || desc->rows>256 || desc->history_rows>32768
    || desc->input_budget_bytes<16 || desc->input_budget_bytes>1048576 || desc->font_role>SC_UI_FONT_CODE || desc->flags&~3u)return SC_UI_E_RANGE;
    TermClass *owner=term_classes;
    while(owner && owner->context!=context)owner=owner->next;
    if(!owner){
        owner=(TermClass *)calloc(1,sizeof(*owner));
        if(!owner)return SC_UI_E_MEMORY;
        owner->context=context;
        ScUiClassDesc klass;
        t_record(&klass,sizeof(klass));
        klass.name="SCTERM.Terminal.1";
        klass.measure=t_measure;
        klass.arrange=t_arrange;
        klass.paint=t_paint;
        klass.event=t_event;
        klass.context=owner;
        klass.context_drop=t_class_drop;
        ScStatus status=ScUiClassRegister(context,&klass,&owner->kind);
        if(status<0){
            free(owner);
            return status;
        }
        owner->next=term_classes;
        term_classes=owner;
    }
    ScUiTerminal *m=(ScUiTerminal *)calloc(1,sizeof(*m));
    if(!m)return SC_UI_E_MEMORY;
    m->magic=TERM_MAGIC;
    m->refs=1;
    m->revision=1;
    m->foreground=m->background=16;
    m->flags=desc->flags;
    m->font_role=desc->font_role;
    m->input_budget=desc->input_budget_bytes;
    m->history_limit=desc->history_rows;
    m->screen=&m->primary;
    m->context=context;
    m->cell_width=8;
    m->cell_height=20;
    t_selection_clear(m);
    ScStatus status=t_screen_make(m,&m->primary,desc->columns,desc->rows,0);
    if(!status && m->history_limit){
        m->history=(TermRow **)calloc(m->history_limit,sizeof(TermRow *));
        if(!m->history)status=SC_UI_E_MEMORY;
    }
    if(status<0){
        ScUiTerminalRelease(m);
        return status;
    }
    m->owner=owner;
    m->next=owner->first;
    if(m->next)m->next->previous=m;
    owner->first=m;
    m->node_owned=1;
    m->refs++;
    ScUiWidgetDesc widget;
    t_record(&widget,sizeof(widget));
    widget.parent=desc->parent;
    widget.kind=owner->kind;
    widget.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    t_record(&widget.text,sizeof(widget.text));
    widget.text.data="";
    widget.context=m;
    widget.context_drop=t_node_drop;
    status=ScUiWidgetCreate(context,&widget,&m->node);
    if(status<0){
        if(m->node_owned)t_node_drop(m,0);
        ScUiTerminalRelease(m);
        return status;
    }
    status=ScUiNodeSetFont(context,m->node,m->font_role);
    if(status<0){
        ScUiNodeDestroy(context,m->node);
        ScUiTerminalRelease(m);
        return status;
    }
    *out=m;
    *node=m->node;
    return 0;
}
