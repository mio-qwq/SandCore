#include "SCNANO.H"
#include "SCTEXT.H"
#define NANO_LIMIT 0x00400000u
#define NANO_LINES 131072u
#define NANO_HISTORY 32u
#define NANO_HISTORY_BYTES (NANO_LIMIT*2u)
#define NK_UP 256
#define NK_DOWN 257
#define NK_LEFT 258
#define NK_RIGHT 259
#define NK_HOME 260
#define NK_END 261
#define NK_PAGEUP 262
#define NK_PAGEDOWN 263
#define NK_DELETE 264
#define NK_RESIZE 265
typedef struct {u32 at,old_size,new_size,before,after;u8 *old_text,*new_text;} nano_edit;
typedef struct {
    u8 *data;u32 capacity,lo,hi,*starts,lines,line_capacity;
    u32 top,left,columns,rows,kind,goal,revision,next_revision,clean_revision;
    int terminal,readonly,goal_valid,failure;
    char name[64],notice[128],search[256];
    u8 *cut;u32 cut_size;
    nano_edit history[NANO_HISTORY];u32 edits,applied,history_bytes;
    char *frame;u32 frame_capacity,frame_size;
} nano_state;
static u32 nano_size(nano_state *n){return n->capacity-(n->hi-n->lo);}
static u8 nano_byte(nano_state *n,u32 at){return n->data[at<n->lo?at:at+n->hi-n->lo];}
static void nano_copy(nano_state *n,u32 at,u8 *out,u32 size)
{for(u32 i=0;i<size;i++)out[i]=nano_byte(n,at+i);}
static void nano_move(nano_state *n,u32 at)
{
    /* 移动的是光标与gap之间的字节，连续键入不搬动整个文件。索引
     * 使用逻辑偏移，不持有gap底层指针，扩容/撤销后也不会悬挂。 */
    while(n->lo>at)n->data[--n->hi]=n->data[--n->lo];
    while(n->lo<at)n->data[n->lo++]=n->data[n->hi++];
}
static int nano_reserve(nano_state *n,u32 bytes,u32 lines)
{
    if(bytes>NANO_LIMIT || lines>NANO_LINES)return -2;
    if(lines>n->line_capacity){u32 capacity=n->line_capacity?n->line_capacity:1024;
        while(capacity<lines)capacity*=2;u32 *index=sc_alloc(capacity*4);if(!index)return -4;
        for(u32 i=0;i<n->lines;i++)index[i]=n->starts[i];if(n->starts)sc_free(n->starts);n->starts=index;n->line_capacity=capacity;}
    if(bytes>n->capacity){u32 capacity=n->capacity?n->capacity:8192;while(capacity<bytes)capacity*=2;
        u8 *data=sc_alloc(capacity);if(!data)return -4;u32 size=nano_size(n),tail=size-n->lo;
        for(u32 i=0;i<n->lo;i++)data[i]=nano_byte(n,i);for(u32 i=0;i<tail;i++)data[capacity-tail+i]=nano_byte(n,n->lo+i);
        if(n->data)sc_free(n->data);n->data=data;n->capacity=capacity;n->hi=capacity-tail;}
    return 0;
}
static u32 nano_line(nano_state *n,u32 at)
{u32 lo=0,hi=n->lines;while(lo+1<hi){u32 mid=lo+(hi-lo)/2;if(n->starts[mid]<=at)lo=mid;else hi=mid;}return lo;}
static u32 nano_end(nano_state *n,u32 row)
{return row+1<n->lines?n->starts[row+1]-1:nano_size(n);}
static u32 nano_scalar(nano_state *n,u32 at,u32 end,u32 *code)
{
    if(at>=end)return 0;u8 b=nano_byte(n,at);u32 size=b<128?1:b>=0xC2&&b<=0xDF?2:b>=0xE0&&b<=0xEF?3:b>=0xF0&&b<=0xF4?4:0;
    if(!size || size>end-at)return 0;u32 c=size==1?b:b&((1u<<(7-size))-1);
    for(u32 i=1;i<size;i++){b=nano_byte(n,at+i);if((b&0xC0)!=0x80)return 0;c=(c<<6)|(b&63);}
    if((size==2&&c<128)||(size==3&&c<2048)||(size==4&&c<65536)||c>0x10FFFF||(c>=0xD800&&c<=0xDFFF))return 0;
    *code=c;return size;
}
static u32 nano_next(nano_state *n,u32 at)
{u32 c,size=nano_scalar(n,at,nano_size(n),&c);return at+(size?size:1);}
static u32 nano_previous(nano_state *n,u32 at)
{
    if(!at)return 0;u32 start=at-1;while(start && at-start<4 && (nano_byte(n,start)&0xC0)==0x80)start--;
    u32 c,size=nano_scalar(n,start,at,&c);return size==at-start?start:at-1;
}
static u32 nano_width(nano_state *n,u32 at,u32 end,u32 col,u32 *size)
{
    u32 c;*size=nano_scalar(n,at,end,&c);if(!*size){*size=1;return 4;}
    if(c=='\t')return 8-(col&7);if(c<32 || c==127)return 2;return c>=0x2E80?2:1;
}
static u32 nano_column(nano_state *n,u32 at)
{u32 row=nano_line(n,at),col=0,end=nano_end(n,row);for(u32 i=n->starts[row];i<at && i<end;){u32 step;col+=nano_width(n,i,end,col,&step);i+=step;}return col;}
static void nano_vertical(nano_state *n,int direction,u32 distance)
{
    u32 row=nano_line(n,n->lo);if(!n->goal_valid){n->goal=nano_column(n,n->lo);n->goal_valid=1;}
    if(direction<0)row=row>distance?row-distance:0;else row=distance>n->lines-1-row?n->lines-1:row+distance;
    u32 at=n->starts[row],end=nano_end(n,row),col=0;
    while(at<end){u32 step,width=nano_width(n,at,end,col,&step);if(col+width>n->goal)break;col+=width;at+=step;}nano_move(n,at);
}
static void nano_edit_free(nano_edit *e){if(e->old_text)sc_free(e->old_text);if(e->new_text)sc_free(e->new_text);*e=(nano_edit){0};}
static void nano_history_drop(nano_state *n,u32 index)
{n->history_bytes-=n->history[index].old_size+n->history[index].new_size;nano_edit_free(n->history+index);}
static int nano_replace(nano_state *n,u32 at,u32 removed,const u8 *text,u32 added)
{
    u32 size=nano_size(n);if(at>size || removed>size-at || added>NANO_LIMIT-(size-removed))return -2;
    u32 first=nano_line(n,at)+1,last=nano_line(n,at+removed)+1;
    u32 fresh=0;for(u32 i=0;i<added;i++)if(text[i]=='\n')fresh++;
    u32 lines=n->lines-(last-first)+fresh;int r=nano_reserve(n,size-removed+added,lines);if(r<0)return r;
    nano_move(n,at);n->hi+=removed;for(u32 i=0;i<added;i++)n->data[n->lo++]=text[i];
    u32 tail=n->lines-last;
    if(fresh>last-first){for(u32 i=tail;i;i--)n->starts[first+fresh+i-1]=n->starts[last+i-1]-removed+added;}
    else for(u32 i=0;i<tail;i++)n->starts[first+fresh+i]=n->starts[last+i]-removed+added;
    u32 next=first;for(u32 i=0;i<added;i++)if(text[i]=='\n')n->starts[next++]=at+i+1;
    n->lines=lines;n->goal_valid=0;return 0;
}
static int nano_change(nano_state *n,u32 at,u32 removed,const u8 *text,u32 added)
{
    if(n->readonly){copy(n->notice,"Read only",128);return -5;}if(!removed && !added)return 0;
    u32 bytes=nano_size(n);if(at>bytes || removed>bytes-at || added>NANO_LIMIT-(bytes-removed))return -2;
    nano_edit e={0};e.at=at;e.old_size=removed;e.new_size=added;
    if(removed){e.old_text=sc_alloc(removed);if(!e.old_text)return -4;nano_copy(n,at,e.old_text,removed);}
    if(added){e.new_text=sc_alloc(added);if(!e.new_text){nano_edit_free(&e);return -4;}for(u32 i=0;i<added;i++)e.new_text[i]=text[i];}
    int r=nano_replace(n,at,removed,text,added);if(r<0){nano_edit_free(&e);return r;}
    while(n->edits>n->applied)nano_history_drop(n,--n->edits);
    while(n->edits && (n->edits==NANO_HISTORY || n->history_bytes>NANO_HISTORY_BYTES-(removed+added))){nano_history_drop(n,0);
        for(u32 i=1;i<n->edits;i++)n->history[i-1]=n->history[i];n->edits--;n->applied--;}
    e.before=n->revision;e.after=++n->next_revision;n->revision=e.after;n->history[n->edits++]=e;n->applied=n->edits;n->history_bytes+=removed+added;
    return 0;
}
static int nano_undo(nano_state *n,int redo)
{
    if(n->readonly)return -5;if(redo?n->applied==n->edits:!n->applied){copy(n->notice,redo?"No redo":"No undo",128);return 0;}
    nano_edit *e=n->history+(redo?n->applied:n->applied-1);
    int r=nano_replace(n,e->at,redo?e->old_size:e->new_size,redo?e->new_text:e->old_text,redo?e->new_size:e->old_size);if(r<0)return r;
    if(redo){n->applied++;n->revision=e->after;}else{n->applied--;n->revision=e->before;}return 0;
}
static int nano_load(const char *name,u8 **out,u32 *size,int missing)
{
    char path[64];u32 meta[2];if(cli_path(name,path)<0)return -1;int stat=sc_stat(path,meta);
    if(stat==-1 && missing){*out=0;*size=0;return 0;}if(stat<0 || meta[0]!=1 || meta[1]>NANO_LIMIT)return -2;
    int fd=sc_stream_open(name,1,0);if(fd<0)return fd;char *data=cli_slurp(fd,NANO_LIMIT,size);sc_stream_close(fd,0);if(!data)return -4;
    for(u32 i=0;i<*size;i++)if(!data[i]){sc_free(data);return -2;}*out=(u8 *)data;return 0;
}
static int nano_save(nano_state *n,const char *name)
{
    if(n->readonly || !*name || length(name)>=64)return -5;char resolved[64];if(cli_path(name,resolved)<0)return -1;
    u32 bytes=nano_size(n);int fd=sc_stream_open(name,2,bytes);if(fd<0)return fd;
    int r=cli_write(fd,n->data,n->lo);if(r>=0)r=cli_write(fd,n->data+n->hi,n->capacity-n->hi);
    if(r<0){sc_stream_close(fd,0);return r;}r=sc_stream_close(fd,1);if(r<0){sc_stream_close(fd,0);return r;}
    /* 仅真实提交成功后才变为已保存，失败时缓冲/撤销链仍在。 */
    copy(n->name,name,64);n->clean_revision=n->revision;copy(n->notice,"Saved",128);return 0;
}
static void nano_dimensions(nano_state *n)
{
    u32 info[8];if(sc_terminal_info2(n->terminal,info)<0){n->failure=1;return;}
    n->kind=info[1];n->columns=info[2];n->rows=info[3];
    if(n->columns<16 || n->rows<5){n->failure=1;return;}if(n->columns>200)n->columns=200;if(n->rows>80)n->rows=80;
    /* 历史窗口TTY只保留16KiB文本，不能让整屏UTF-8帧被悄悄滚掉。
     * 在这个已有协议上限定一次完整页的行数，原始文件始终完整保留。 */
    if(n->kind==5){u32 rows=15360/(n->columns*4+32);if(n->rows>rows)n->rows=rows;if(n->rows<5)n->failure=1;}
}
static int nano_emit(nano_state *n,const void *text,u32 size)
{if(size>n->frame_capacity-n->frame_size)return -1;for(u32 i=0;i<size;i++)n->frame[n->frame_size++]=((const char *)text)[i];return 0;}
static int nano_word(nano_state *n,const char *text){return nano_emit(n,text,(u32)length(text));}
static void nano_number(nano_state *n,u32 value)
{char digits[11];u32 used=0;do{digits[used++]=(char)('0'+value%10);value/=10;}while(value);for(u32 i=0;i<used/2;i++){char c=digits[i];digits[i]=digits[used-1-i];digits[used-1-i]=c;}nano_emit(n,digits,used);}
static void nano_label(nano_state *n,const char *text,u32 limit)
{for(u32 i=0;i<limit && text[i];i++){char c=text[i]>=32 && text[i]<127?text[i]:'?';nano_emit(n,&c,1);}}
static void nano_content(nano_state *n,u32 row)
{
    u32 at=n->starts[row],end=nano_end(n,row),col=0,shown=0,limit=n->columns-1;const char *hex="0123456789abcdef";
    /* 所有非文字字节可视化，文件里的ESC不传给外部终端。光标使用
     * 可见竖线，不向仅支持文本/清屏的历史TTY偷偷注入ANSI定位。 */
    for(;;){if(at==n->lo && col>=n->left && shown<limit){nano_word(n,"|");shown++;}
        if(at>=end || shown>=limit)break;u32 size,width=nano_width(n,at,end,col,&size);u8 byte=nano_byte(n,at);char text[4];
        if(col+width<=n->left){col+=width;at+=size;continue;}
        if(byte=='\t'){for(u32 j=0;j<width;j++){if(col+j>=n->left && shown<limit){nano_word(n," ");shown++;}}}
        else if(col>=n->left && width<=limit-shown){if(byte<32 || byte==127){text[0]='^';text[1]=byte==127?'?':(char)(byte+64);nano_emit(n,text,2);}
            else {u32 code;if(!nano_scalar(n,at,end,&code)){text[0]='\\';text[1]='x';text[2]=hex[byte>>4];text[3]=hex[byte&15];nano_emit(n,text,4);}
                else {for(u32 i=0;i<size;i++)text[i]=(char)nano_byte(n,at+i);nano_emit(n,text,size);}}shown+=width;}
        else if(col>=n->left)break;
        col+=width;at+=size;
    }nano_word(n,"\n");
}
static int nano_draw(nano_state *n,const char *prompt,const char *answer)
{
    nano_dimensions(n);if(n->failure)return -1;u32 needed=(n->columns*4+32)*n->rows+1024;
    if(needed>n->frame_capacity){char *frame=sc_alloc(needed);if(!frame)return -4;if(n->frame)sc_free(n->frame);n->frame=frame;n->frame_capacity=needed;}
    n->frame_size=0;u32 row=nano_line(n,n->lo),body=n->rows-4,col=nano_column(n,n->lo);
    if(row<n->top)n->top=row;if(row>=n->top+body)n->top=row-body+1;
    if(col<n->left)n->left=col;if(col>=n->left+n->columns-2)n->left=col-(n->columns-3);
    nano_word(n," SandNano  ");nano_label(n,n->name[0]?n->name:"New buffer",n->columns>30?n->columns-30:1);
    if(n->readonly)nano_word(n," [RO]");else if(n->revision!=n->clean_revision)nano_word(n," *");nano_word(n,"\n");
    for(u32 i=0;i<body;i++){if(n->top+i<n->lines)nano_content(n,n->top+i);else nano_word(n,"~\n");}
    u32 begin=n->frame_size;
    if(prompt){nano_label(n,prompt,48);nano_word(n,": ");nano_label(n,answer,n->columns);}
    else {nano_word(n,"Line ");nano_number(n,row+1);nano_word(n,"  Col ");nano_number(n,col+1);nano_word(n,"  ");nano_label(n,n->notice,96);}
    if(n->frame_size-begin>n->columns-1)n->frame_size=begin+n->columns-1;nano_word(n,"\n");
    nano_label(n,"^O Save  ^X Exit  ^W Find  ^K Cut  ^U Paste",n->columns-1);nano_word(n,"\n");
    nano_label(n,"M-U Undo M-E Redo  ^T Line  F8 Help",n->columns-1);
    for(;;){int r=sc_terminal_clear(n->terminal);if(r!=-6){if(r<0)return r;break;}sc_yield();}
    return cli_write(n->terminal,n->frame,n->frame_size)<0?-1:0;
}
static int nano_raw(nano_state *n,int timeout)
{
    u32 start=(u32)sc_tick();for(;;){u8 c;int r=sc_stream_read(n->terminal,&c,1);if(r==1)return c;if(!r)return -2;if(r!=-6)return -1;
        if(timeout && (u32)sc_tick()-start>=(u32)timeout)return -3;sc_yield();}
}
static int nano_key(nano_state *n)
{
    int c=nano_raw(n,5);if(c==-3){u32 columns=n->columns,rows=n->rows;nano_dimensions(n);return columns!=n->columns || rows!=n->rows?NK_RESIZE:-3;}
    if(c<0)return c;
    if(n->kind==5){if(c>=0x80 && c<=0x83)return NK_UP+c-0x80;
        if(c==1)return 7;if(c==2)return 15;if(c==5)return 23;if(c==6)return 11;if(c==7)return 21;if(c==0x88)return 7;if(c==0x89)return 20;if(c==0x8A)return 24;}
    if(c!=27)return c;int next=nano_raw(n,20);if(next<0)return 27;
    if(next=='u' || next=='U')return 266;if(next=='e' || next=='E')return 267;
    if(next!='[' && next!='O')return 27;
    char sequence[12];u32 used=0;for(;;){next=nano_raw(n,20);if(next<0 || used==11)return 27;sequence[used++]=(char)next;
        if(next>='@' && next<='~')break;}sequence[used]=0;
    if(equal(sequence,"A"))return NK_UP;if(equal(sequence,"B"))return NK_DOWN;if(equal(sequence,"C"))return NK_RIGHT;if(equal(sequence,"D"))return NK_LEFT;
    if(equal(sequence,"H")||equal(sequence,"1~")||equal(sequence,"7~"))return NK_HOME;if(equal(sequence,"F")||equal(sequence,"4~")||equal(sequence,"8~"))return NK_END;
    if(equal(sequence,"3~"))return NK_DELETE;if(equal(sequence,"5~"))return NK_PAGEUP;if(equal(sequence,"6~"))return NK_PAGEDOWN;
    if(equal(sequence,"12~"))return 15;if(equal(sequence,"13~"))return 23;if(equal(sequence,"14~"))return 24;if(equal(sequence,"19~"))return 7;return 27;
}
static int nano_prompt(nano_state *n,const char *label,char *out,u32 capacity)
{
    u32 used=(u32)length(out);if(used>=capacity)return -1;
    if(nano_draw(n,label,out)<0)return -1;
    for(;;){int c=nano_key(n);if(c==-3)continue;if(c==NK_RESIZE){if(nano_draw(n,label,out)<0)return -1;continue;}
        if(c<0 || c==27)return -1;if(c=='\r'||c=='\n')return (int)used;
        if(c==21){used=0;out[0]=0;}
        else if(c==8 || c==127){if(used)out[--used]=0;}
        else if(c>=32 && c<127 && used+1<capacity){out[used++]=(char)c;out[used]=0;}
        if(nano_draw(n,label,out)<0)return -1;
    }
}
static int nano_write_prompt(nano_state *n)
{char path[64];copy(path,n->name,64);if(nano_prompt(n,"Write file",path,64)<0)return -1;int r=nano_save(n,path);if(r<0)copy(n->notice,"Write failed; buffer preserved",128);return r;}
static void nano_find(nano_state *n)
{
    char text[256];copy(text,n->search,256);if(nano_prompt(n,"Find literal",text,256)<0 || !text[0])return;
    copy(n->search,text,256);u32 bytes=nano_size(n),size=(u32)length(text),start=n->lo;
    if(size>bytes){copy(n->notice,"Not found",128);return;}
    for(u32 scanned=0,at=start<bytes?start+1:0;scanned<=bytes;scanned++,at=at==bytes?0:at+1){
        if(at<=bytes-size){u32 i=0;while(i<size && nano_byte(n,at+i)==(u8)text[i])i++;if(i==size){nano_move(n,at);n->goal_valid=0;copy(n->notice,at<=start?"Found (wrapped)":"Found",128);return;}}
        if(!(scanned&4095))sc_yield();
    }copy(n->notice,"Not found",128);
}
static int nano_cut(nano_state *n)
{
    if(n->readonly)return -5;u32 row=nano_line(n,n->lo),start=n->starts[row],end=row+1<n->lines?n->starts[row+1]:nano_size(n),size=end-start;
    if(!size)return 0;u8 *text=sc_alloc(size);if(!text)return -4;nano_copy(n,start,text,size);
    int r=nano_change(n,start,size,0,0);if(r<0){sc_free(text);return r;}if(n->cut)sc_free(n->cut);n->cut=text;n->cut_size=size;return 0;
}
static void nano_release(nano_state *n)
{
    for(u32 i=0;i<n->edits;i++)nano_edit_free(n->history+i);if(n->data)sc_free(n->data);if(n->starts)sc_free(n->starts);if(n->cut)sc_free(n->cut);if(n->frame)sc_free(n->frame);
    if(n->terminal>=3)sc_stream_close(n->terminal,0);
}
static int nano_main(void)
{
    if(cli_parse()<0)return 2;nano_state n={0};n.terminal=-1;n.revision=n.next_revision=n.clean_revision=1;u32 initial_line=1;int file=-1;
    for(int i=0;i<cli_argc;i++){const char *arg=cli_argv[i];if(equal(arg,"--help")){cli_text(1,"nano [-v] [+LINE] [FILE]\nSelf-written editor: ^O save, ^X exit, ^W find, ^T line, ^K cut, ^U paste, Alt-U/E undo/redo\n");return 0;}
        if(equal(arg,"-v")||equal(arg,"--view"))n.readonly=1;else if(arg[0]=='+' && arg[1]){if(text_number(arg+1,&initial_line)<0 || !initial_line)return 2;}
        else if(arg[0]=='-' || file>=0)return 2;else file=i;}
    u8 *loaded=0;u32 bytes=0;
    if(file>=0){if(length(cli_argv[file])>=64)return 2;copy(n.name,cli_argv[file],64);int r=nano_load(n.name,&loaded,&bytes,1);if(r<0)return cli_error("nano: read",r);}
    n.lines=0;if(nano_reserve(&n,bytes?bytes:1,1)<0){if(loaded)sc_free(loaded);nano_release(&n);return 1;}
    n.hi=n.capacity;n.lines=1;n.starts[0]=0;
    if(nano_replace(&n,0,0,loaded,bytes)<0){if(loaded)sc_free(loaded);nano_release(&n);return 1;}if(loaded)sc_free(loaded);
    nano_move(&n,n.starts[initial_line>n.lines?n.lines-1:initial_line-1]);
    n.terminal=sc_stream_open("",8,0);u32 terminal[8];
    if(n.terminal<0 || sc_terminal_info2(n.terminal,terminal)<0 || !terminal[2] || !terminal[3]){nano_release(&n);return cli_error("nano: interactive terminal required",-1);}
    int result=0,redraw=1;
    for(;;){if(redraw){if(nano_draw(&n,0,0)<0){result=1;break;}redraw=0;}
        int key=nano_key(&n);if(key==-3)continue;if(key<0){result=n.revision!=n.clean_revision?1:0;break;}redraw=1;n.notice[0]=0;
        if(key==NK_RESIZE || key==12)continue;
        if(key==24){if(n.revision==n.clean_revision)break;char choice[8]="";
            if(nano_prompt(&n,"Save changes? y/n (Esc cancel)",choice,8)<0)continue;
            if(equal(choice,"n")||equal(choice,"N"))break;if((equal(choice,"y")||equal(choice,"Y")) && nano_write_prompt(&n)>=0)break;continue;}
        if(key==15){nano_write_prompt(&n);continue;}if(key==23){nano_find(&n);continue;}
        if(key==20){char line[16]="";u32 row;if(nano_prompt(&n,"Go to line",line,16)>=0 && !text_number(line,&row) && row && row<=n.lines){nano_move(&n,n.starts[row-1]);n.goal_valid=0;}else copy(n.notice,"Invalid line",128);continue;}
        if(key==7){copy(n.notice,"^R insert file; Alt-U/E undo/redo; ^A/E home/end",128);continue;}
        int r=0;
        if(key==18){char path[64]="";if(nano_prompt(&n,"Insert file",path,64)>=0){u8 *text=0;u32 size;r=nano_load(path,&text,&size,0);if(r>=0)r=nano_change(&n,n.lo,0,text,size);if(text)sc_free(text);}}
        else if(key==266 || key==267)r=nano_undo(&n,key==267);
        else if(key==11)r=nano_cut(&n);else if(key==21)r=nano_change(&n,n.lo,0,n.cut,n.cut_size);
        else if(key==NK_UP || key==NK_DOWN || key==NK_PAGEUP || key==NK_PAGEDOWN)nano_vertical(&n,key==NK_UP||key==NK_PAGEUP?-1:1,key==NK_PAGEUP||key==NK_PAGEDOWN?n.rows-4:1);
        else if(key==NK_LEFT){if(n.lo)nano_move(&n,nano_previous(&n,n.lo));n.goal_valid=0;}
        else if(key==NK_RIGHT){if(n.lo<nano_size(&n))nano_move(&n,nano_next(&n,n.lo));n.goal_valid=0;}
        else if(key==1 || key==NK_HOME){nano_move(&n,n.starts[nano_line(&n,n.lo)]);n.goal_valid=0;}
        else if(key==5 || key==NK_END){nano_move(&n,nano_end(&n,nano_line(&n,n.lo)));n.goal_valid=0;}
        else if(key==8 || key==127){if(n.lo){u32 at=nano_previous(&n,n.lo);r=nano_change(&n,at,n.lo-at,0,0);}}
        else if(key==NK_DELETE){if(n.lo<nano_size(&n))r=nano_change(&n,n.lo,nano_next(&n,n.lo)-n.lo,0,0);}
        else if(key=='\r'||key=='\n'||key=='\t'||(key>=32 && key<256)){
            u8 text[4];u32 size=1;text[0]=(u8)(key=='\r'?'\n':key);
            if(key>=128){size=key>=0xC2&&key<=0xDF?2:key>=0xE0&&key<=0xEF?3:key>=0xF0&&key<=0xF4?4:0;u32 code=size?((u32)key&((1u<<(7-size))-1)):0;
                for(u32 i=1;i<size;i++){int c=nano_raw(&n,100);if(c<128 || c>191){size=0;break;}text[i]=(u8)c;code=(code<<6)|(u32)(c&63);}
                if((size==2&&code<128)||(size==3&&code<2048)||(size==4&&code<65536)||code>0x10FFFF||(code>=0xD800&&code<=0xDFFF))size=0;
                if(!size){copy(n.notice,"Invalid UTF-8 input",128);continue;}}
            r=nano_change(&n,n.lo,0,text,size);
        }
        if(r<0)copy(n.notice,r==-5?"Read only":"Edit failed; buffer preserved",128);
    }
    cli_text(n.terminal,"\n");nano_release(&n);return result;
}
