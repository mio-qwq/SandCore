#include "SCPAGER.H"
#include "SCTEXT.H"
#include "SCRE.H"
#define PAGER_BYTES 0x01000000u
#define PAGER_LINES 262144u
typedef struct {
    int input,terminal,eof,error,event,cancel;
    u8 *data;u32 bytes,capacity,*starts,lines,line_capacity;
    u32 top,columns,rows,left,seen_columns,seen_rows;
    int numbered,chop,fold,quit_short,less;
    char search[256],notice[96];int reverse,has_search;
    re_program regex;
    char *frame;u32 frame_capacity,frame_size;
} pager_state;
static int pager_interrupted(pager_state *p)
{
    if(p->event>=0){int n=sc_stream_event(p->terminal,0);if(n<0 || n!=p->event){p->cancel=1;return 1;}}
    return 0;
}
static int pager_reserve(pager_state *p,u32 needed)
{
    if(needed>PAGER_BYTES)return -1;if(needed<=p->capacity)return 0;
    u32 capacity=p->capacity?p->capacity:8192;while(capacity<needed){if(capacity>=PAGER_BYTES){capacity=PAGER_BYTES;break;}capacity*=2;}
    u8 *data=sc_alloc(capacity);if(!data)return -4;for(u32 i=0;i<p->bytes;i++)data[i]=p->data[i];
    if(p->data)sc_free(p->data);p->data=data;p->capacity=capacity;return 0;
}
static int pager_line(pager_state *p,u32 offset)
{
    if(p->lines==PAGER_LINES)return -1;
    if(p->lines==p->line_capacity){u32 capacity=p->line_capacity?p->line_capacity*2:1024;if(capacity>PAGER_LINES)capacity=PAGER_LINES;
        u32 *lines=sc_alloc(capacity*4);if(!lines)return -4;for(u32 i=0;i<p->lines;i++)lines[i]=p->starts[i];if(p->starts)sc_free(p->starts);p->starts=lines;p->line_capacity=capacity;}
    p->starts[p->lines++]=offset;return 0;
}
static u32 pager_count(pager_state *p)
{return p->lines && p->starts[p->lines-1]==p->bytes?p->lines-1:p->lines;}
static int pager_more(pager_state *p)
{
    if(p->eof || p->error)return p->error?-1:0;u8 bytes[4096];int count;
    for(;;){if(pager_interrupted(p))return -1;count=sc_stream_read(p->input,bytes,sizeof(bytes));if(count!=-6)break;sc_yield();}
    if(count<0){p->error=count;return -1;}if(!count){p->eof=1;return 0;}
    int result=pager_reserve(p,p->bytes+(u32)count);if(result<0){p->error=result;return -1;}
    for(int i=0;i<count;i++){p->data[p->bytes++]=bytes[i];if(bytes[i]=='\n' && pager_line(p,p->bytes)<0){p->error=-4;return -1;}}
    return 1;
}
static int pager_fill(pager_state *p,u32 through)
{
    /* starts最后一项是尚未完成的行；只按需读到屏幕/搜索需要的完整
     * 行，管道无须先等EOF。每块只扫描一次，后退重用已缓存索引。 */
    while(!p->eof && !p->error && p->lines<=through+1)if(pager_more(p)<0)return -1;
    return p->error?-1:0;
}
static int pager_key(pager_state *p)
{
    for(;;){if(pager_interrupted(p))return -2;u8 c;int n=sc_stream_read(p->terminal,&c,1);
        if(n==1)return c;if(!n)return -2;if(n!=-6)return -1;sc_yield();}
}
static int pager_escape(pager_state *p)
{
    u32 start=(u32)sc_tick();while((u32)((u32)sc_tick()-start)<20){if(pager_interrupted(p))return -2;u8 c;int n=sc_stream_read(p->terminal,&c,1);
        if(n==1)return c;if(n!=-6)return -1;sc_yield();}return -1;
}
static void pager_dimensions(pager_state *p)
{
    u32 info[8];p->columns=80;p->rows=24;if(!sc_terminal_info2(p->terminal,info) && info[2] && info[3]){p->columns=info[2];p->rows=info[3];}
    if(p->columns<8)p->columns=8;if(p->columns>240)p->columns=240;if(p->rows<3)p->rows=3;if(p->rows>120)p->rows=120;
}
static int pager_emit(pager_state *p,const void *text,u32 bytes)
{
    if(bytes>p->frame_capacity-p->frame_size)return -1;for(u32 i=0;i<bytes;i++)p->frame[p->frame_size++]=((const char *)text)[i];return 0;
}
static int pager_word(pager_state *p,const char *text){return pager_emit(p,text,(u32)length(text));}
static int pager_number(pager_state *p,u32 value)
{char reverse[11],text[11];u32 n=0;do{reverse[n++]=(char)('0'+value%10);value/=10;}while(value);for(u32 i=0;i<n;i++)text[i]=reverse[n-1-i];return pager_emit(p,text,n);}
static u32 pager_scalar(const u8 *s,u32 bytes,u32 *code)
{
    u32 n=s[0]<128?1:s[0]>=0xC2&&s[0]<=0xDF?2:s[0]>=0xE0&&s[0]<=0xEF?3:s[0]>=0xF0&&s[0]<=0xF4?4:0;
    if(!n || n>bytes)return 0;u32 c=n==1?s[0]:s[0]&((1u<<(7-n))-1);
    for(u32 i=1;i<n;i++){if((s[i]&0xC0)!=0x80)return 0;c=(c<<6)|(s[i]&63);}
    if((n==2&&c<128)||(n==3&&c<2048)||(n==4&&c<65536)||c>0x10FFFF||(c>=0xD800&&c<=0xDFFF))return 0;*code=c;return n;
}
static int pager_content(pager_state *p,u32 index)
{
    u32 at=p->starts[index],end=index+1<p->lines?p->starts[index+1]:p->bytes;if(end>at && p->data[end-1]=='\n')end--;
    if(end>at && p->data[end-1]=='\r')end--;u32 col=0,shown=0,skip=p->left;
    if(p->numbered){if(pager_number(p,index+1)<0 || pager_word(p," ")<0)return -1;u32 n=index+1;do{shown++;n/=10;}while(n);shown++;}
    /* 控制字节显示为可见符号，输入文件里的ESC不能注入宿主终端。
     * UTF-8按完整标量提交，汉字按两列预算；长行明确横向裁剪而非
     * 向真实终端溢出造成第二行覆盖。缓存始终保留完整原始字节。 */
    for(;at<end && shown<p->columns;){u8 c=p->data[at];char safe[4];const void *text;u32 size,width,code;
        if(c=='\t'){width=8-(col&7);for(u32 i=0;i<width;i++){if(col++<skip)continue;if(shown==p->columns)break;if(pager_word(p," ")<0)return -1;shown++;}at++;continue;}
        if(c<32 || c==127){safe[0]='^';safe[1]=c==127?'?':(char)(c+64);text=safe;size=width=2;at++;}
        else {size=pager_scalar(p->data+at,end-at,&code);if(!size){const char *hex="0123456789abcdef";safe[0]='\\';safe[1]='x';safe[2]=hex[c>>4];safe[3]=hex[c&15];size=width=4;text=safe;at++;}
            else {text=p->data+at;at+=size;width=code>=0x2E80?2:1;}}
        if(col<skip){col+=width;continue;}if(width>p->columns-shown)break;col+=width;shown+=width;if(pager_emit(p,text,size)<0)return -1;
    }
    return pager_word(p,"\n");
}
static int pager_draw(pager_state *p,const char *name)
{
    pager_dimensions(p);u32 needed=(p->columns*4+16)*p->rows+512;
    if(needed>p->frame_capacity){char *frame=sc_alloc(needed);if(!frame)return -1;if(p->frame)sc_free(p->frame);p->frame=frame;p->frame_capacity=needed;}
    p->frame_size=0;u32 body=p->rows-2,count=pager_count(p);int short_view=p->quit_short && p->eof && !p->top && count<=body;
    if(short_view)body=count;
    for(u32 row=0;row<body;row++){if(p->top+row<count){if(pager_content(p,p->top+row)<0)return -1;}else if(pager_word(p,"~\n")<0)return -1;}
    if(short_view)return cli_write(1,p->frame,p->frame_size)<0?-1:0;
    /* 状态行截至当前网格，末尾不换行，避免恰好填满时滚走首行。
     * 名称是调用者参数，控制符仍由CLI路径规范/名称检查约束。 */
    char label[96];u32 at=0;while(name[at] && at<48){u8 c=(u8)name[at];label[at]=c>=32 && c<127?(char)c:'?';at++;}label[at]=0;
    u32 start=p->frame_size;if(pager_word(p,label)<0 || pager_word(p,"  line ")<0 || pager_number(p,count?p->top+1:0)<0 || pager_word(p,"/")<0 || pager_number(p,count)<0)return -1;
    if(pager_word(p,p->eof?" (END)":" +")<0 || pager_word(p,"  q quit / search")<0)return -1;
    if(p->notice[0]){if(pager_word(p,"  ")<0 || pager_word(p,p->notice)<0)return -1;}
    if(p->frame_size-start>p->columns)p->frame_size=start+p->columns;
    for(;;){int n=sc_terminal_clear(p->terminal);if(n!=-6){if(n<0)return -1;break;}if(pager_interrupted(p))return -1;sc_yield();}
    if(cli_write(p->terminal,p->frame,p->frame_size)<0)return -1;p->seen_columns=p->columns;p->seen_rows=p->rows;return 0;
}
static int pager_prompt(pager_state *p,char marker,char out[256])
{
    if(cli_text(p->terminal,"\n")<0 || cli_write(p->terminal,&marker,1)<0)return -1;u32 used=0;
    for(;;){int c=pager_key(p);if(c<0 || c==27 || c==3)return -1;if(c=='\r')continue;if(c=='\n'){out[used]=0;return (int)used;}
        if(c==8 || c==127){if(used){used--;cli_text(p->terminal,"\b \b");}continue;}
        if(c<32 || c>=127)continue;if(used==255){copy(p->notice,"search capacity",96);return -1;}out[used++]=(char)c;u8 byte=(u8)c;if(cli_write(p->terminal,&byte,1)<0)return -1;}
}
static int pager_search(pager_state *p,int reverse)
{
    if(!p->has_search){copy(p->notice,"no search",96);return 0;}u32 index=p->top;
    for(;;){if(pager_interrupted(p))return -1;if(reverse){if(!index)break;index--;}else {if(index==PAGER_LINES-1)break;index++;if(pager_fill(p,index)<0)return -1;if(index>=pager_count(p))break;}
        u32 at=p->starts[index],end=index+1<p->lines?p->starts[index+1]:p->bytes;if(end>at && p->data[end-1]=='\n')end--;
        u32 begin,finish;if(re_program_search(&p->regex,(const char *)p->data+at,end-at,0,&begin,&finish)){p->top=index;p->notice[0]=0;return 1;}sc_yield();}
    copy(p->notice,"pattern not found",96);return 0;
}
static int pager_run(pager_state *p,const char *name)
{
    if(pager_line(p,0)<0)return 1;pager_dimensions(p);u32 page=p->rows-2;
    if(pager_fill(p,page-1)<0)return p->cancel?130:cli_error("pager: read/capacity",p->error);
    if(p->quit_short && p->eof && !p->top && pager_count(p)<=page)return pager_draw(p,name)<0?1:0;
    for(;;){if(pager_fill(p,p->top+p->rows-3)<0)return p->cancel?130:cli_error("pager: read/capacity",p->error);
        u32 count=pager_count(p);if(p->top>=count)p->top=count?count-1:0;if(pager_draw(p,name)<0)return p->cancel?130:1;
        int key=pager_key(p);if(key<0 || key=='q' || key=='Q')return p->cancel?130:0;p->notice[0]=0;page=p->rows-2;
        if(key==' ' || key=='f' || key==6){if(p->eof && p->top+page>=count && !p->less)return 0;p->top+=page;}
        else if(key=='\n' || key=='j' || key==0x81 || key==14)p->top++;
        else if(key=='b' || key==2)p->top=p->top>page?p->top-page:0;
        else if(key=='k' || key==0x80 || key==16){if(p->top)p->top--;}
        else if(key=='d' || key==4)p->top+=page/2;
        else if(key=='u' || key==21)p->top=p->top>page/2?p->top-page/2:0;
        else if(key=='g')p->top=0;
        else if(key=='G'){while(!p->eof && !p->error)if(pager_more(p)<0)return p->cancel?130:1;count=pager_count(p);p->top=count>page?count-page:0;}
        else if(key=='h' || key==0x82)p->left=p->left>8?p->left-8:0;
        else if(key=='l' || key==0x83){if(p->left<65536)p->left+=8;}
        else if(key=='/' || key=='?'){char text[256];if(pager_prompt(p,(char)key,text)>=0){int valid=1;if(text[0]){re_program fresh={0};if(re_program_compile(&fresh,text,1,p->fold)<0){copy(p->notice,"invalid regular expression",96);valid=0;}
                    else {re_program_free(&p->regex);p->regex=fresh;copy(p->search,text,256);p->has_search=1;}}
                if(valid && p->has_search){p->reverse=key=='?';if(pager_search(p,p->reverse)<0)return p->cancel?130:1;}}}
        else if(key=='n' || key=='N'){if(pager_search(p,key=='N'?!p->reverse:p->reverse)<0)return p->cancel?130:1;}
        else if(key=='='){copy(p->notice,"h/l scroll; g/G first/last",96);}
        else if(key==27){/* 外部ANSI方向键转为与PS/2相同的导航，单Esc退出。 */
            int c=pager_escape(p);if(c!='[')return p->cancel?130:0;c=pager_escape(p);if(c=='A'){if(p->top)p->top--;}else if(c=='B')p->top++;
            else if(c=='C'){if(p->left<65536)p->left+=8;}else if(c=='D')p->left=p->left>8?p->left-8:0;}
    }
}
static void pager_release(pager_state *p)
{
    if(p->event>=0)sc_stream_event(p->terminal,2);if(p->input)sc_stream_close(p->input,0);if(p->terminal>=3)sc_stream_close(p->terminal,0);
    if(p->data)sc_free(p->data);if(p->starts)sc_free(p->starts);if(p->frame)sc_free(p->frame);re_program_free(&p->regex);
}
static int pager_main(int less_mode)
{
    if(cli_parse()<0)return 2;pager_state state={0};state.less=less_mode;state.chop=1;state.event=-1;state.terminal=-1;int at=0;
    for(;at<cli_argc;at++){const char *option=cli_argv[at];if(equal(option,"--")){at++;break;}if(option[0]=='+' && option[1]){u32 line;if(text_number(option+1,&line)<0 || !line || line>PAGER_LINES)return 2;state.top=line-1;continue;}
        if(option[0]!='-' || !option[1])break;for(int i=1;option[i];i++){if(option[i]=='N')state.numbered=1;else if(option[i]=='i')state.fold=1;else if(option[i]=='F')state.quit_short=1;
            else if(option[i]=='S')state.chop=1;else return cli_error("pager: option",-1);}}
    u32 info[8];if(sc_terminal_info2(1,info)<0 || !info[2] || !info[3]){int result=0;if(at==cli_argc)return cli_copy_fd(0,1)<0?1:0;
        for(int i=at;i<cli_argc;i++){int fd=cli_input(i);if(fd<0){cli_error(cli_argv[i],fd);result=1;continue;}if(cli_copy_fd(fd,1)<0)result=1;if(fd)sc_stream_close(fd,0);}return result;}
    if(at+1<cli_argc)return cli_error("pager: one input per session",-1);
    state.input=at==cli_argc?0:cli_input(at);if(state.input<0)return cli_error("pager",state.input);
    state.terminal=sc_stream_open("",8,0);if(state.terminal<0){if(state.input)sc_stream_close(state.input,0);return cli_error("pager: terminal",state.terminal);}
    state.event=sc_stream_event(state.terminal,1);if(state.event<0){pager_release(&state);return 1;}
    int result=pager_run(&state,at==cli_argc?"stdin":cli_argv[at]);cli_text(state.terminal,"\n");pager_release(&state);return result;
}
