static ScU32 t_parameter(ScUiTerminal *m,ScU32 index,ScU32 fallback)
{
    return index<m->parameter_count && m->parameters[index]?m->parameters[index]:fallback;
}
static void t_position(ScUiTerminal *m,ScU32 row,ScU32 column)
{
    TermScreen *s=m->screen;
    ScU32 top=s->origin?s->top:0,bottom=s->origin?s->bottom:s->rows-1;
    if(row<top)row=top;
    if(row>bottom)row=bottom;
    if(column>=s->columns)column=s->columns-1;
    s->row=row;
    s->column=column;
    s->pending=0;
    t_damage(m,0,(ScI32)s->rows-1);
}
static ScStatus t_alternate(ScUiTerminal *m,int enable,int save)
{
    int active=(m->flags&SC_UI_TERM_ALTERNATE)!=0;
    if(active==enable)return 0;
    if(enable){
        TermScreen screen;
        memset(&screen,0,sizeof(screen));
        ScStatus status=t_screen_make(m,&screen,m->primary.columns,m->primary.rows,0);
        if(status<0)return status;
        if(save){
            m->primary.saved_column=m->primary.column;
            m->primary.saved_row=m->primary.row;
        }
        t_screen_free(&m->alternate);
        m->alternate=screen;
        m->screen=&m->alternate;
        m->flags|=SC_UI_TERM_ALTERNATE;
    }
    else {
        m->flags&=~SC_UI_TERM_ALTERNATE;
        m->screen=&m->primary;
        if(save){
            m->primary.column=m->primary.saved_column;
            m->primary.row=m->primary.saved_row;
            m->primary.pending=0;
        }
        t_screen_free(&m->alternate);
    }
    m->view_back=0;
    t_selection_clear(m);
    t_damage(m,0,(ScI32)m->screen->rows-1);
    return 0;
}
static ScStatus t_mode(ScUiTerminal *m,int enable)
{
    for(ScU32 i=0;i<m->parameter_count;i++){
        ScU32 value=m->parameters[i];
        if(m->private_mode){
            if(value==1){
                if(enable)m->flags|=SC_UI_TERM_APPLICATION_CURSOR;
                else m->flags&=~SC_UI_TERM_APPLICATION_CURSOR;
            }
            else if(value==6){
                m->screen->origin=(ScU32)enable;
                t_position(m,enable?m->screen->top:0,0);
            }
            else if(value==7){
                if(enable)m->flags|=SC_UI_TERM_WRAP;
                else m->flags&=~SC_UI_TERM_WRAP;
            }
            else if(value==25){
                if(enable)m->flags|=SC_UI_TERM_CURSOR;
                else m->flags&=~SC_UI_TERM_CURSOR;
            }
            else if(value==47 || value==1047 || value==1049){
                ScStatus status=t_alternate(m,enable,value==1049);
                if(status<0)return status;
            }
            else if(value==2004){
                if(enable)m->flags|=SC_UI_TERM_BRACKETED_PASTE;
                else m->flags&=~SC_UI_TERM_BRACKETED_PASTE;
            }
        }
        else if(value==4)m->screen->insert=(ScU32)enable;
    }
    t_damage(m,0,(ScI32)m->screen->rows-1);
    return 0;
}
static void t_sgr(ScUiTerminal *m)
{
    for(ScU32 i=0;i<m->parameter_count;i++){
        ScU32 p=m->parameters[i];
        if(!p){
            m->foreground=m->background=16;
            m->attributes=0;
        }
        else if(p==1)m->attributes|=SC_UI_TERM_BOLD;
        else if(p==4)m->attributes|=SC_UI_TERM_UNDERLINE;
        else if(p==7)m->attributes|=SC_UI_TERM_INVERSE;
        else if(p==22)m->attributes&=~SC_UI_TERM_BOLD;
        else if(p==24)m->attributes&=~SC_UI_TERM_UNDERLINE;
        else if(p==27)m->attributes&=~SC_UI_TERM_INVERSE;
        else if(p>=30 && p<=37)m->foreground=p-30;
        else if(p>=40 && p<=47)m->background=p-40;
        else if(p>=90 && p<=97)m->foreground=p-90+8;
        else if(p>=100 && p<=107)m->background=p-100+8;
        else if(p==39)m->foreground=16;
        else if(p==49)m->background=16;
        /* 公开主题Painter只支持已登记角色。扩展色序列整体消费，不能把
         * 38;2;255;0;0里的0误当reset；不伪称支持真彩ANSI调色盘。 */
        else if((p==38 || p==48) && i+1<m->parameter_count){
            ScU32 mode=m->parameters[++i];
            ScU32 skip=mode==2?3:mode==5?1:0;
            if(skip>m->parameter_count-i-1)skip=m->parameter_count-i-1;
            i+=skip;
        }
    }
}
static void t_shift_characters(ScUiTerminal *m,ScU32 count,int insert)
{
    TermScreen *s=m->screen;
    TermRow *r=s->lines[s->row];
    ScU32 x=s->column;
    if(count>s->columns-x)count=s->columns-x;
    if(!count)return;
    /* 先撤掉跨切割边界的宽字。memmove转移marks所有权，源重复区只清零。 */
    if(r->cells[x].flags&SC_UI_TERM_CONTINUATION)t_erase_cells(m,r,x,x);
    if(insert){
        for(ScU32 i=s->columns-count;i<s->columns;i++)ScStringRelease(r->cells[i].marks);
        memmove(r->cells+x+count,r->cells+x,(s->columns-x-count)*sizeof(TermCell));
        memset(r->cells+x,0,count*sizeof(TermCell));
        for(ScU32 i=x;i<x+count;i++)t_cell_clear(r->cells+i,m->foreground,m->background,m->attributes);
    }
    else{
        for(ScU32 i=x;i<x+count;i++)ScStringRelease(r->cells[i].marks);
        memmove(r->cells+x,r->cells+x+count,(s->columns-x-count)*sizeof(TermCell));
        memset(r->cells+s->columns-count,0,count*sizeof(TermCell));
        for(ScU32 i=s->columns-count;i<s->columns;i++)t_cell_clear(r->cells+i,m->foreground,m->background,m->attributes);
    }
    t_row_normalize(m,r);
    r->used=s->columns;
    t_damage(m,(ScI32)s->row,(ScI32)s->row);
}
static ScStatus t_csi(ScUiTerminal *m,unsigned char final)
{
    TermScreen *s=m->screen;
    ScU32 n=t_parameter(m,0,1),p=m->parameters[0];
    if(n>65535)n=65535;
    switch(final){
        case 'A':t_position(m,s->row>n?s->row-n:0,s->column);
        break;
        case 'B':case 'e':t_position(m,s->row+n,s->column);
        break;
        case 'C':case 'a':t_position(m,s->row,s->column+n);
        break;
        case 'D':t_position(m,s->row,s->column>n?s->column-n:0);
        break;
        case 'E':t_position(m,s->row+n,0);
        break;
        case 'F':t_position(m,s->row>n?s->row-n:0,0);
        break;
        case 'G':case '`':t_position(m,s->row,n-1);
        break;
        case 'd':t_position(m,n-1+(s->origin?s->top:0),s->column);
        break;
        case 'H':case 'f':t_position(m,n-1+(s->origin?s->top:0),t_parameter(m,1,1)-1);
        break;
        case 'J':
        if(p==3)t_history_clear(m);
        else for(ScU32 y=0;y<s->rows;y++){
            if(p==2 || (p==0 && y>s->row) || (p==1 && y<s->row))t_row_clear(m,s->lines[y]);
            else if(y==s->row && p<2)t_erase_cells(m,s->lines[y],p?0:s->column,p?s->column:s->columns-1);
        }
        t_damage(m,0,(ScI32)s->rows-1);
        break;
        case 'K':if(p<=2)t_erase_cells(m,s->lines[s->row],p==0?s->column:0,p==1?s->column:s->columns-1);
        t_damage(m,(ScI32)s->row,(ScI32)s->row);
        break;
        case 'X':t_erase_cells(m,s->lines[s->row],s->column,s->column+n-1);
        t_damage(m,(ScI32)s->row,(ScI32)s->row);
        break;
        case '@':t_shift_characters(m,n,1);
        break;
        case 'P':t_shift_characters(m,n,0);
        break;
        case 'L':case 'M':if(s->row>=s->top && s->row<=s->bottom){
            if(n>s->bottom-s->row+1)n=s->bottom-s->row+1;
            while(n--){
                ScStatus status=t_scroll_screen(m,s->row,s->bottom,final=='M'?1:-1);
                if(status<0)return status;
            }
        }
        break;
        case 'S':case 'T':if(n>s->bottom-s->top+1)n=s->bottom-s->top+1;
        while(n--){
            ScStatus status=t_scroll_screen(m,s->top,s->bottom,final=='S'?1:-1);
            if(status<0)return status;
        }
        break;
        case 'r':if(!m->private_mode){
            ScU32 top=n-1,bottom=t_parameter(m,1,s->rows)-1;
            if(top<bottom && bottom<s->rows){
                s->top=top;
                s->bottom=bottom;
                t_position(m,s->origin?top:0,0);
            }
        }
        break;
        case 's':s->saved_column=s->column;
        s->saved_row=s->row;
        break;
        case 'u':t_position(m,s->saved_row,s->saved_column);
        break;
        case 'm':t_sgr(m);
        break;
        case 'h':return t_mode(m,1);
        case 'l':return t_mode(m,0);
        case 'n':if(!m->private_mode){
            if(p==5)return t_queue(m,"\033[0n",4);
            if(p==6){
                char reply[40];
                int bytes=snprintf(reply,sizeof(reply),"\033[%u;%uR",s->row+1,s->column+1);
                return bytes>0 && bytes<(int)sizeof(reply)?t_queue(m,reply,(ScU32)bytes):SC_UI_E_RANGE;
            }
        }
        break;
        case 'c':if(!p)return t_queue(m,"\033[?1;2c",7);
        break;
        default:break;
    }
    return 0;
}
static ScStatus t_osc_finish(ScUiTerminal *m)
{
    if(m->osc_overflow || m->osc_used<2 || m->osc[1]!=';' || (m->osc[0]!='0' && m->osc[0]!='2'))return 0;
    if(ScUtf8Validate(m->osc+2,m->osc_used-2,0)<0)return 0;
    for(ScU32 i=2;i<m->osc_used;i++)if((unsigned char)m->osc[i]<32)return 0;
    ScString *title=0;
    ScStatus status=ScStringCreate(m->osc+2,m->osc_used-2,&title);
    if(!status){
        ScStringRelease(m->title);
        m->title=title;
    }
    return status;
}
static ScStatus t_control(ScUiTerminal *m,unsigned char c)
{
    TermScreen *s=m->screen;
    if(c==7){
        m->bells++;
        return 0;
    }
    if(c==8){
        s->pending=0;
        if(s->column)s->column--;
        if((s->lines[s->row]->cells[s->column].flags&SC_UI_TERM_CONTINUATION) && s->column)s->column--;
        t_damage(m,(ScI32)s->row,(ScI32)s->row);
    }
    else if(c==9){
        ScU32 next=(s->column+8)&~7u;
        s->column=next<s->columns?next:s->columns-1;
        s->pending=0;
        t_damage(m,(ScI32)s->row,(ScI32)s->row);
    }
    /* 旧shell与nano输出LF代表下一行起点；PTY的raw合同只约束输入，
     * 本模拟器采用明确的LF→CRLF默认，与SandCore既有TTY一致。 */
    else if(c==10 || c==11 || c==12)return t_linefeed(m,1);
    else if(c==13){
        s->column=s->pending=0;
        t_damage(m,(ScI32)s->row,(ScI32)s->row);
    }
    return 0;
}
static ScStatus t_ground(ScUiTerminal *m,unsigned char c)
{
    if(m->utf8_needed){
        if((c&0xc0)==0x80){
            m->utf8_scalar=(m->utf8_scalar<<6)|(c&63);
            m->utf8_seen++;
            if(m->utf8_seen<m->utf8_needed)return 0;
            ScU32 scalar=m->utf8_scalar;
            m->utf8_needed=m->utf8_seen=0;
            if(scalar<m->utf8_min || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff)){
                m->invalid_utf8++;
                scalar=0xfffd;
            }
            return t_put(m,scalar);
        }
        m->utf8_needed=m->utf8_seen=0;
        m->invalid_utf8++;
        ScStatus status=t_put(m,0xfffd);
        if(status<0)return status;
    }
    if(c<32 || c==127){
        if(c==27){
            m->parser=TERM_ESC;
            return 0;
        }
        return t_control(m,c);
    }
    if(c<128)return t_put(m,c);
    if(c>=0xc2 && c<=0xdf){
        m->utf8_scalar=c&31;
        m->utf8_needed=2;
        m->utf8_min=128;
    }
    else if(c>=0xe0 && c<=0xef){
        m->utf8_scalar=c&15;
        m->utf8_needed=3;
        m->utf8_min=2048;
    }
    else if(c>=0xf0 && c<=0xf4){
        m->utf8_scalar=c&7;
        m->utf8_needed=4;
        m->utf8_min=65536;
    }
    else {
        m->invalid_utf8++;
        return t_put(m,0xfffd);
    }
    m->utf8_seen=1;
    return 0;
}
static ScStatus t_byte(ScUiTerminal *m,unsigned char c)
{
    if(m->parser==TERM_GROUND)return t_ground(m,c);
    if(m->parser==TERM_OSC){
        if(c==7){
            m->parser=TERM_GROUND;
            return t_osc_finish(m);
        }
        if(c==27){
            m->parser=TERM_OSC_ESC;
            return 0;
        }
        if(c && m->osc_used<sizeof(m->osc))m->osc[m->osc_used++]=(char)c;
        else m->osc_overflow=1;
        return 0;
    }
    if(m->parser==TERM_OSC_ESC){
        if(c=='\\'){
            m->parser=TERM_GROUND;
            return t_osc_finish(m);
        }
        m->parser=c==27?TERM_OSC_ESC:TERM_OSC;
        m->osc_overflow=1;
        return 0;
    }
    if(c==27){
        m->parser=TERM_ESC;
        return 0;
    }
    if(c==24 || c==26){
        m->parser=TERM_GROUND;
        return 0;
    }
    if(c<32)return t_control(m,c);
    if(m->parser==TERM_CHARSET){
        m->parser=TERM_GROUND;
        return 0;
    }
    if(m->parser==TERM_ESC){
        m->parser=TERM_GROUND;
        if(c=='['){
            memset(m->parameters,0,sizeof(m->parameters));
            m->parameter_count=1;
            m->private_mode=m->intermediate=0;
            m->parser=TERM_CSI;
        }
        else if(c==']'){
            m->osc_used=m->osc_overflow=0;
            m->parser=TERM_OSC;
        }
        else if(c=='(' || c==')' || c=='*' || c=='+')m->parser=TERM_CHARSET;
        else if(c=='7'){
            m->screen->saved_column=m->screen->column;
            m->screen->saved_row=m->screen->row;
        }
        else if(c=='8')t_position(m,m->screen->saved_row,m->screen->saved_column);
        else if(c=='D')return t_linefeed(m,0);
        else if(c=='E')return t_linefeed(m,1);
        else if(c=='M'){
            if(m->screen->row==m->screen->top)return t_scroll_screen(m,m->screen->top,m->screen->bottom,-1);
            if(m->screen->row)m->screen->row--;
            t_damage(m,0,(ScI32)m->screen->rows-1);
        }
        else if(c=='c')return ScUiTerminalClear(m,SC_UI_TERM_RESET);
        return 0;
    }
    if(m->parser==TERM_CSI){
        if(c>='0' && c<='9'){
            ScU32 *p=m->parameters+m->parameter_count-1;
            if(*p<65535)*p=*p*10+(c-'0');
            if(*p>65535)*p=65535;
            return 0;
        }
        if(c==';' || c==':'){
            if(m->parameter_count<16)m->parameter_count++;
            else m->intermediate=255;
            return 0;
        }
        if(c=='?' && m->parameter_count==1 && !m->parameters[0]){
            m->private_mode=1;
            return 0;
        }
        if(c>=' ' && c<='?'){
            m->intermediate=c;
            return 0;
        }
        if(c>='@' && c<='~'){
            m->parser=TERM_GROUND;
            if(m->intermediate)return 0;
            return t_csi(m,c);
        }
        m->parser=TERM_GROUND;
    }
    return 0;
}
ScStatus ScUiTerminalFeed(ScUiTerminal *m,const void *data,ScU32 bytes,ScU32 *accepted)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(!accepted || (bytes && !data))return SC_UI_E_INVALID;
    *accepted=0;
    ScStatus status=0;
    for(ScU32 i=0;i<bytes;i++){
        ScU32 old_row=m->screen->row,old_col=m->screen->column;
        status=t_byte(m,((const unsigned char *)data)[i]);
        *accepted=i+1;
        if(old_row!=m->screen->row || old_col!=m->screen->column){
            t_damage(m,(ScI32)old_row,(ScI32)old_row);
            t_damage(m,(ScI32)m->screen->row,(ScI32)m->screen->row);
        }
        if(status<0)break;
    }
    t_flush_damage(m);
    return status;
}
ScStatus ScUiTerminalFinish(ScUiTerminal *m)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    ScStatus status=0;
    if(m->utf8_needed){
        m->utf8_needed=m->utf8_seen=0;
        m->invalid_utf8++;
        status=t_put(m,0xfffd);
    }
    m->parser=TERM_GROUND;
    t_flush_damage(m);
    return status;
}
