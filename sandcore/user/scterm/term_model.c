static void t_cell_clear(TermCell *c,ScU32 fg,ScU32 bg,ScU32 attributes)
{
    ScStringRelease(c->marks);
    memset(c,0,sizeof(*c));
    c->scalar=' ';
    c->foreground=fg;
    c->background=bg;
    c->flags=attributes;
}
static void t_row_clear(ScUiTerminal *m,TermRow *r)
{
    for(ScU32 i=0;i<r->columns;i++)t_cell_clear(r->cells+i,m->foreground,m->background,m->attributes);
    r->used=r->wrapped=0;
}
static void t_row_free(TermRow *r)
{
    if(!r)return;
    for(ScU32 i=0;i<r->columns;i++)ScStringRelease(r->cells[i].marks);
    free(r->cells);
    free(r);
}
static void t_row_normalize(ScUiTerminal *m,TermRow *r)
{
    for(ScU32 x=0;x<r->columns;x++){
        TermCell *c=r->cells+x;
        if((c->flags&SC_UI_TERM_WIDE) && (x+1==r->columns || !(r->cells[x+1].flags&SC_UI_TERM_CONTINUATION)))
        t_cell_clear(c,m->foreground,m->background,m->attributes);
        if((c->flags&SC_UI_TERM_CONTINUATION) && (!x || !(r->cells[x-1].flags&SC_UI_TERM_WIDE)))
        t_cell_clear(c,m->foreground,m->background,m->attributes);
    }
}
static TermRow *t_row_new(ScUiTerminal *m,ScU32 columns)
{
    if(m->next_row==UINT_MAX)return 0;
    TermRow *r=(TermRow *)calloc(1,sizeof(*r));
    if(!r)return 0;
    r->cells=(TermCell *)calloc(columns,sizeof(TermCell));
    if(!r->cells){
        free(r);
        return 0;
    }
    r->columns=columns;
    r->id=++m->next_row;
    t_row_clear(m,r);
    return r;
}
static void t_screen_free(TermScreen *s)
{
    for(ScU32 i=0;i<s->rows;i++)t_row_free(s->lines[i]);
    free(s->lines);
    memset(s,0,sizeof(*s));
}
static ScStatus t_screen_make(ScUiTerminal *m,TermScreen *out,ScU32 columns,ScU32 rows,const TermScreen *old)
{
    TermScreen s;
    memset(&s,0,sizeof(s));
    s.columns=columns;
    s.rows=rows;
    s.bottom=rows-1;
    ScU32 offset=old && old->rows>rows && old->row>=rows?old->row-rows+1:0;
    s.lines=(TermRow **)calloc(rows,sizeof(TermRow *));
    if(!s.lines)return SC_UI_E_MEMORY;
    for(ScU32 y=0;y<rows;y++){
        TermRow *r=t_row_new(m,columns);
        if(!r){
            t_screen_free(&s);
            return SC_UI_E_MEMORY;
        }
        s.lines[y]=r;
        if(old && old->lines && y+offset<old->rows){
            TermRow *source=old->lines[y+offset];
            ScU32 n=source->columns<columns?source->columns:columns;
            r->id=source->id;
            r->wrapped=source->wrapped;
            r->used=source->used<n?source->used:n;
            for(ScU32 x=0;x<n;x++){
                r->cells[x]=source->cells[x];
                if(r->cells[x].marks)ScStringRetain(r->cells[x].marks);
            }
            if(n && (r->cells[n-1].flags&SC_UI_TERM_WIDE))t_cell_clear(r->cells+n-1,m->foreground,m->background,m->attributes);
        }
    }
    if(old){
        s.column=old->column<columns?old->column:columns-1;
        s.row=old->row-offset<rows?old->row-offset:rows-1;
        s.saved_column=old->saved_column<columns?old->saved_column:columns-1;
        s.saved_row=old->saved_row>offset?old->saved_row-offset:0;
        if(s.saved_row>=rows)s.saved_row=rows-1;
        s.origin=old->origin;
        s.insert=old->insert;
    }
    *out=s;
    return 0;
}
static void t_history_clear(ScUiTerminal *m)
{
    for(ScU32 i=0;i<m->history_count;i++)t_row_free(m->history[(m->history_head+i)%m->history_limit]);
    m->history_count=m->history_head=m->view_back=0;
}
static ScU32 t_total(const ScUiTerminal *m)
{
    return m->screen->rows+((m->flags&SC_UI_TERM_ALTERNATE)?0:m->history_count);
}
static TermRow *t_row_at(const ScUiTerminal *m,ScU32 index)
{
    ScU32 count=(m->flags&SC_UI_TERM_ALTERNATE)?0:m->history_count;
    if(index<count)return m->history[(m->history_head+index)%m->history_limit];
    return index-count<m->screen->rows?m->screen->lines[index-count]:0;
}
static int t_row_index(const ScUiTerminal *m,ScU32 id,ScU32 *out)
{
    for(ScU32 i=0;i<t_total(m);i++)if(t_row_at(m,i)->id==id){
        *out=i;
        return 1;
    }
    return 0;
}
static void t_selection_clear(ScUiTerminal *m)
{
    t_record(&m->selection,sizeof(m->selection));
    m->selecting=0;
}
static ScStatus t_scroll_screen(ScUiTerminal *m,ScU32 top,ScU32 bottom,int direction)
{
    TermScreen *s=m->screen;
    if(top>bottom || bottom>=s->rows)return SC_UI_E_INVALID;
    if(m->next_row>UINT_MAX-3)return SC_UI_E_RANGE;
    TermRow *reuse=s->lines[direction>0?top:bottom],*fresh=reuse;
    int history=direction>0 && !top && bottom==s->rows-1 && !(m->flags&SC_UI_TERM_ALTERNATE) && m->history_limit;
    if(history){
        if(m->history_count==m->history_limit){
            fresh=m->history[m->history_head];
            if(fresh->columns!=s->columns){
                TermRow *candidate=t_row_new(m,s->columns);
                if(!candidate)return SC_UI_E_MEMORY;
                t_row_free(fresh);
                fresh=candidate;
            }
            m->history[m->history_head]=reuse;
            m->history_head=(m->history_head+1)%m->history_limit;
        }
        else{
            fresh=t_row_new(m,s->columns);
            if(!fresh)return SC_UI_E_MEMORY;
            m->history[(m->history_head+m->history_count)%m->history_limit]=reuse;
            m->history_count++;
        }
        if(m->view_back && m->view_back<m->history_count)m->view_back++;
    }
    if(fresh==reuse || history){
        if(m->next_row==UINT_MAX)return SC_UI_E_RANGE;
        t_row_clear(m,fresh);
        fresh->id=++m->next_row;
    }
    if(direction>0){
        for(ScU32 y=top;y<bottom;y++)s->lines[y]=s->lines[y+1];
        s->lines[bottom]=fresh;
    }
    else {
        for(ScU32 y=bottom;y>top;y--)s->lines[y]=s->lines[y-1];
        s->lines[top]=fresh;
    }
    if(m->selection.anchor_row){
        ScU32 ignored;
        if(!t_row_index(m,m->selection.anchor_row,&ignored) || !t_row_index(m,m->selection.active_row,&ignored))t_selection_clear(m);
    }
    t_damage(m,(ScI32)top,(ScI32)bottom);
    return 0;
}
static ScStatus t_linefeed(ScUiTerminal *m,int carriage)
{
    TermScreen *s=m->screen;
    ScStatus status=0;
    if(s->row==s->bottom)status=t_scroll_screen(m,s->top,s->bottom,1);
    else if(s->row+1<s->rows)s->row++;
    if(!status){
        s->pending=0;
        if(carriage)s->column=0;
    }
    return status;
}
static void t_erase_cells(ScUiTerminal *m,TermRow *r,ScU32 first,ScU32 last)
{
    if(first>=r->columns)return;
    if(last>=r->columns)last=r->columns-1;
    if(r->cells[first].flags&SC_UI_TERM_CONTINUATION){
        if(first)first--;
    }
    if(r->cells[last].flags&SC_UI_TERM_WIDE){
        if(last+1<r->columns)last++;
    }
    for(ScU32 x=first;x<=last;x++)t_cell_clear(r->cells+x,m->foreground,m->background,m->attributes);
    if(last+1>=r->used)r->used=first<r->used?first:r->used;
}
static ScStatus t_scalar_width(ScUiTerminal *m,ScU32 scalar,ScU32 *width)
{
    ScU32 slot=(scalar*2654435761u)>>24;
    if(m->widths[slot].epoch==m->font_epoch && m->widths[slot].scalar==scalar){
        *width=m->widths[slot].width;
        return 0;
    }
    char bytes[4];
    ScU32 count;
    ScStatus status=ScUtf8Encode(scalar,bytes,4,&count);
    if(status<0)return status;
    ScUiText text={
        sizeof(text),1,bytes,count
    };
    ScUiTextMetrics size;
    t_record(&size,sizeof(size));
    if(!m->context)return SC_UI_E_STALE;
    status=ScUiMeasureText(m->context,m->font_role,&text,INT_MAX,0,&size);
    if(status<0)return status;
    ScU32 cells=size.width?(size.width+m->cell_width-1)/m->cell_width:0;
    if(cells>2)cells=2;
    m->widths[slot].scalar=scalar;
    m->widths[slot].epoch=m->font_epoch;
    m->widths[slot].width=cells;
    *width=cells;
    return 0;
}
static ScStatus t_put(ScUiTerminal *m,ScU32 scalar)
{
    TermScreen *s=m->screen;
    ScU32 width;
    ScStatus status=t_scalar_width(m,scalar,&width);
    if(status<0)return status;
    if(!width){
        ScU32 x=s->pending?s->column+1:s->column,y=s->row;
        if(!x){
            if(!y)return 0;
            y--;
            x=s->columns;
        }
        x--;
        TermCell *cell=s->lines[y]->cells+x;
        if((cell->flags&SC_UI_TERM_CONTINUATION) && x)cell--;
        char encoded[4];
        ScU32 bytes;
        ScUtf8Encode(scalar,encoded,4,&bytes);
        if(!cell->marks){
            char cluster[8];
            ScU32 base;
            ScUtf8Encode(cell->scalar?cell->scalar:' ',cluster,4,&base);
            memcpy(cluster+base,encoded,bytes);
            status=ScStringCreate(cluster,base+bytes,&cell->marks);
        }
        else status=ScStringAppend(cell->marks,encoded,bytes);
        if(!status)t_damage(m,(ScI32)y,(ScI32)y);
        return status;
    }
    if(width>s->columns)width=1;
    if(width==2 && s->column+1==s->columns && !(m->flags&SC_UI_TERM_WRAP)){
        /* 关自动换行时只剩一格，不能把第二格写到数组之外。 */
        scalar=0xfffd;
        width=1;
    }
    if(s->pending || (width==2 && s->column+1==s->columns)){
        if(m->flags&SC_UI_TERM_WRAP){
            TermRow *previous=s->lines[s->row];
            status=t_linefeed(m,1);
            if(status<0)return status;
            if(previous!=s->lines[s->row])previous->wrapped=1;
        }
        else s->pending=0;
    }
    TermRow *r=s->lines[s->row];
    ScU32 x=s->column;
    if(s->insert && width<s->columns-x){
        for(ScU32 k=s->columns-width;k<s->columns;k++)ScStringRelease(r->cells[k].marks);
        memmove(r->cells+x+width,r->cells+x,(s->columns-x-width)*sizeof(TermCell));
        memset(r->cells+x,0,width*sizeof(TermCell));
        t_row_normalize(m,r);
    }
    t_erase_cells(m,r,x,x+width-1);
    TermCell *cell=r->cells+x;
    cell->scalar=scalar;
    cell->foreground=m->foreground;
    cell->background=m->background;
    cell->flags=m->attributes|(width==2?SC_UI_TERM_WIDE:0);
    if(width==2){
        r->cells[x+1].scalar=0;
        r->cells[x+1].flags=m->attributes|SC_UI_TERM_CONTINUATION;
        r->cells[x+1].foreground=m->foreground;
        r->cells[x+1].background=m->background;
    }
    if(x+width>r->used)r->used=x+width;
    if(x+width==s->columns){
        s->column=s->columns-1;
        s->pending=1;
    }
    else s->column+=width;
    t_damage(m,(ScI32)s->row,(ScI32)s->row);
    return 0;
}
static ScStatus t_input_reserve(ScUiTerminal *m,ScU32 add)
{
    if(add>m->input_budget-m->input_used)return SC_UI_E_RANGE;
    if(m->input_used+add<=m->input_capacity)return 0;
    ScU32 capacity=m->input_capacity?m->input_capacity:256;
    while(capacity<m->input_used+add){
        if(capacity>m->input_budget/2){
            capacity=m->input_budget;
            break;
        }
        capacity*=2;
    }
    if(capacity>m->input_budget)capacity=m->input_budget;
    unsigned char *body=(unsigned char *)malloc(capacity);
    if(!body)return SC_UI_E_MEMORY;
    for(ScU32 i=0;i<m->input_used;i++)body[i]=m->input[(m->input_head+i)%m->input_capacity];
    free(m->input);
    m->input=body;
    m->input_capacity=capacity;
    m->input_head=0;
    return 0;
}
static void t_input_append(ScUiTerminal *m,const void *data,ScU32 bytes)
{
    for(ScU32 i=0;i<bytes;i++)m->input[(m->input_head+m->input_used+i)%m->input_capacity]=((const unsigned char *)data)[i];
    m->input_used+=bytes;
}
static ScStatus t_queue(ScUiTerminal *m,const void *data,ScU32 bytes)
{
    ScStatus status=t_input_reserve(m,bytes);
    if(!status)t_input_append(m,data,bytes);
    return status;
}
ScStatus ScUiTerminalRetain(ScUiTerminal *m)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(m->refs==UINT_MAX)return SC_UI_E_RANGE;
    m->refs++;
    return 0;
}
void ScUiTerminalRelease(ScUiTerminal *m)
{
    if(!t_valid(m) || --m->refs)return;
    t_history_clear(m);
    t_screen_free(&m->primary);
    t_screen_free(&m->alternate);
    free(m->history);
    free(m->input);
    ScStringRelease(m->title);
    m->magic=0;
    free(m);
}
ScStatus ScUiTerminalResize(ScUiTerminal *m,ScU32 columns,ScU32 rows)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(!columns || columns>512 || !rows || rows>256)return SC_UI_E_RANGE;
    if(m->primary.columns==columns && m->primary.rows==rows)return 0;
    TermScreen primary,alternate;
    memset(&primary,0,sizeof(primary));
    memset(&alternate,0,sizeof(alternate));
    ScStatus status=t_screen_make(m,&primary,columns,rows,&m->primary);
    if(!status && m->alternate.lines)status=t_screen_make(m,&alternate,columns,rows,&m->alternate);
    if(status<0){
        t_screen_free(&primary);
        t_screen_free(&alternate);
        return status;
    }
    /* 两屏候选都齐全才替换，OOM保留画面/历史/光标与输入队列。 */
    ScU32 offset=m->primary.rows>rows && m->primary.row>=rows?m->primary.row-rows+1:0;
    for(ScU32 i=0;i<offset && m->history_limit;i++){
        TermRow *r=m->primary.lines[i];
        m->primary.lines[i]=0;
        if(m->history_count==m->history_limit){
            t_row_free(m->history[m->history_head]);
            m->history[m->history_head]=r;
            m->history_head=(m->history_head+1)%m->history_limit;
        }
        else m->history[(m->history_head+m->history_count++)%m->history_limit]=r;
        if(m->view_back && m->view_back<m->history_count)m->view_back++;
    }
    t_screen_free(&m->primary);
    t_screen_free(&m->alternate);
    m->primary=primary;
    m->alternate=alternate;
    m->screen=(m->flags&SC_UI_TERM_ALTERNATE)?&m->alternate:&m->primary;
    t_selection_clear(m);
    t_damage(m,0,(ScI32)rows-1);
    t_flush_damage(m);
    return 0;
}
ScStatus ScUiTerminalQuery(const ScUiTerminal *m,ScUiTerminalInfo *out)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    ScStatus status=t_output(out,sizeof(*out));
    if(status<0)return status;
    ScUiTerminalInfo value;
    t_record(&value,sizeof(value));
    value.node=m->node;
    value.columns=m->screen->columns;
    value.rows=m->screen->rows;
    value.cursor_column=m->screen->column;
    value.cursor_row=m->screen->row;
    value.flags=m->flags;
    value.history_rows=m->history_count;
    value.history_limit=m->history_limit;
    value.view_back=m->view_back;
    value.input_bytes=m->input_used;
    value.revision=m->revision;
    value.bells=m->bells;
    value.invalid_utf8=m->invalid_utf8;
    value.first_row=t_row_at(m,0)->id;
    value.last_row=t_row_at(m,t_total(m)-1)->id;
    value.font_role=m->font_role;
    *out=value;
    return 0;
}
ScStatus ScUiTerminalCellQuery(const ScUiTerminal *m,ScU32 row,ScU32 column,ScUiTerminalCell *out)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    ScStatus status=t_output(out,sizeof(*out));
    if(status<0)return status;
    TermRow *r=t_row_at(m,row);
    if(!r || column>=r->columns)return SC_UI_E_RANGE;
    TermCell *c=r->cells+column;
    ScUiTerminalCell value;
    t_record(&value,sizeof(value));
    value.scalar=c->scalar;
    value.foreground=c->foreground;
    value.background=c->background;
    value.flags=c->flags;
    value.row_id=r->id;
    *out=value;
    return 0;
}
ScStatus ScUiTerminalScroll(ScUiTerminal *m,ScI32 rows)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    ScU32 limit=(m->flags&SC_UI_TERM_ALTERNATE)?0:m->history_count;
    ScU32 next=m->view_back;
    if(rows<0){
        ScU32 n=0u-(ScU32)rows;
        next=n>next?0:next-n;
    }
    else next=(ScU32)rows>limit-next?limit:next+(ScU32)rows;
    if(next!=m->view_back){
        m->view_back=next;
        t_damage(m,0,(ScI32)m->screen->rows-1);
        t_flush_damage(m);
    }
    return 0;
}
ScStatus ScUiTerminalClear(ScUiTerminal *m,ScU32 action)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(action<1 || action>3)return SC_UI_E_INVALID;
    if(action>=2)t_history_clear(m);
    if(action==1 || action==3){
        if(action==3){
            m->foreground=m->background=16;
            m->attributes=0;
            m->flags=SC_UI_TERM_CURSOR|SC_UI_TERM_WRAP;
            m->screen=&m->primary;
            m->parser=TERM_GROUND;
            m->utf8_needed=m->utf8_seen=0;
            t_screen_free(&m->alternate);
        }
        for(ScU32 y=0;y<m->screen->rows;y++)t_row_clear(m,m->screen->lines[y]);
        m->screen->column=m->screen->row=m->screen->pending=0;
        m->screen->top=0;
        m->screen->bottom=m->screen->rows-1;
    }
    t_selection_clear(m);
    t_damage(m,0,(ScI32)m->screen->rows-1);
    t_flush_damage(m);
    return 0;
}
ScStatus ScUiTerminalSend(ScUiTerminal *m,const void *data,ScU32 bytes,ScU32 paste)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if((bytes && !data) || paste>1)return SC_UI_E_INVALID;
    if(paste && (memchr(data,0,bytes) || ScUtf8Validate(data,bytes,0)<0))return SC_UI_E_UTF8;
    ScU32 extra=paste && (m->flags&SC_UI_TERM_BRACKETED_PASTE)?12:0;
    if(bytes>UINT_MAX-extra)return SC_UI_E_RANGE;
    ScStatus status=t_input_reserve(m,bytes+extra);
    if(status<0)return status;
    if(extra)t_input_append(m,"\033[200~",6);
    t_input_append(m,data,bytes);
    if(extra)t_input_append(m,"\033[201~",6);
    if(m->view_back){
        m->view_back=0;
        t_damage(m,0,(ScI32)m->screen->rows-1);
        t_flush_damage(m);
    }
    return 0;
}
ScStatus ScUiTerminalTakeInput(ScUiTerminal *m,void *data,ScU32 capacity,ScU32 *taken)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(!taken || (capacity && !data))return SC_UI_E_INVALID;
    ScU32 n=m->input_used<capacity?m->input_used:capacity;
    for(ScU32 i=0;i<n;i++)((unsigned char *)data)[i]=m->input[(m->input_head+i)%m->input_capacity];
    if(n){
        m->input_head=(m->input_head+n)%m->input_capacity;
        m->input_used-=n;
    }
    *taken=n;
    return 0;
}
ScStatus ScUiTerminalTitle(const ScUiTerminal *m,ScString **out)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(!out)return SC_UI_E_INVALID;
    if(!m->title)return ScStringCreate("",0,out);
    ScStatus status=ScStringRetain(m->title);
    if(!status)*out=m->title;
    return status;
}
