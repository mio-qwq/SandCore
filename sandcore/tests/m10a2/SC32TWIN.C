#include "SC32API.H"

/* 这是独立的公开API应用，不复制库桥/控件树。512只约束本次留证
 * 缓冲；满后明确失败，绝不能被解释成窗口或OS消息队列上限。 */
#define TWIN_LOG_CAPACITY 512u
#define TWIN_READY 0u
#define TWIN_SNAPSHOT 1u
#define TWIN_FINISHED 2u
#define TWIN_FAILED 3u
#define TWIN_FLAG_FINISH 1u
#define TWIN_FLAG_CLEANUP 2u
#define TWIN_FLAG_OVERFLOW 4u
#define TWIN_FLAG_LOSS 8u
#define TWIN_FLAG_LOCAL_ERROR 16u
#define TWIN_FLAG_AUTO_END 32u
#define TWIN_FLAG_INITIAL_LOG 64u
#define TWIN_F5 (SC32_KEY_F1+4u)
#define TWIN_F6 (SC32_KEY_F1+5u)
#define TWIN_F9 (SC32_KEY_F1+8u)
#define TWIN_F10 (SC32_KEY_F1+9u)

typedef struct {
    char magic[8];
    ScU32 version,header_bytes,status,file_bytes,pid,pid_generation;
    ScU32 snapshot_count,event_count,event_capacity,event_size,info_size,info_count;
    ScU32 window_a,window_b,window_c,alive_mask,coverage,paints,submit_calls;
    ScU32 submitted_lo,submitted_hi,error_count;
    ScI32 first_code;ScU32 first_domain;ScI32 first_raw;
    ScU32 first_operation,heap_before,heap_after,flags,last_tick;
} TwinLogHeader;
typedef char twin_header_128[(sizeof(TwinLogHeader)==128)?1:-1];
typedef char twin_message_64[(sizeof(ScMessage)==64)?1:-1];
typedef char twin_info_96[(sizeof(ScWindowInfo)==96)?1:-1];
typedef struct {
    ScU32 index,window,live,focused,captured,buttons,modifiers,close_count;
    ScU32 *pixels,*candidate;
    ScU32 surface_width,surface_height,surface_bytes,full_needed,activity;
    ScI32 pointer_x,pointer_y;
    ScU32 model_revision,held[8];
    ScWindowInfo info;
} TwinWindow;

static TwinLogHeader report;
static TwinWindow windows[3];
static ScWindowInfo snapshots[3];
static ScMessage events[TWIN_LOG_CAPACITY];
static ScU32 event_counts[24],done;
static ScThemeInfo theme;
static ScDisplayInfo display;
static ScFile console_file;
static ScU32 console_available;

static void zero(void *out,ScU32 bytes)
{
    unsigned char *p=out;
    for(ScU32 i=0;i<bytes;i++)p[i]=0;
}
static void record_init(void *out,ScU32 bytes)
{
    ScU32 *p=out;p[0]=bytes;p[1]=1;
}
static void cover(ScU32 bit){report.coverage|=1u<<bit;}
static ScStatus fail(ScStatus status,ScU32 action)
{
    report.error_count++;done=1;
    if(!report.first_code){
        report.first_code=status<0?status:SC32_E_FAILED;
        ScErrorInfo diagnostic;record_init(&diagnostic,sizeof(diagnostic));
        if(status<0 && !ScErrorQuery(&diagnostic) && diagnostic.code==status){
            report.first_domain=diagnostic.domain;report.first_raw=diagnostic.raw;
            report.first_operation=diagnostic.operation;
        }else{
            report.flags|=TWIN_FLAG_LOCAL_ERROR;
            report.first_domain=SC32_DOMAIN_LIBRARY;report.first_raw=status;
            report.first_operation=0x90000000u|action;
        }
    }
    return report.first_code;
}
static int remember(const ScMessage *message)
{
    if(report.event_count==TWIN_LOG_CAPACITY){
        report.flags|=TWIN_FLAG_OVERFLOW;fail(SC32_E_NO_SPACE,1);return 0;
    }
    /* 保存回调收到的完整64B值，不从快照制造输入沿，也不改tick/epoch。 */
    events[report.event_count++]=*message;
    event_counts[message->id<24u?message->id:0u]++;
    report.last_tick=message->tick;return 1;
}
static ScStatus refresh(TwinWindow *w)
{
    record_init(&w->info,sizeof(w->info));
    ScStatus result=ScWindowQuery(w->window,&w->info);
    return result?fail(result,2):0;
}
static ScStatus invalidate(TwinWindow *w,ScU32 whole)
{
    if(!w->live)return 0;
    ScU32 width=w->info.client_width,height=w->info.client_height;
    if(!width || !height)return 0;
    ScStatus result;
    if(whole)result=ScWindowInvalidate(w->window,0,0,width,height);
    else result=ScWindowInvalidate(w->window,0,0,width,height<32u?height:32u);
    return result?fail(result,3):0;
}
static ScStatus surface_drop(TwinWindow *w)
{
    ScStatus result=0;
    if(w->candidate){
        result=ScMemoryFreePages(w->candidate);
        if(result)fail(result,4);else w->candidate=0;
    }
    if(w->pixels){
        ScStatus released=ScMemoryFreePages(w->pixels);
        if(released){fail(released,4);result=released;}else w->pixels=0;
    }
    if(!w->pixels)w->surface_width=w->surface_height=w->surface_bytes=0;
    return result;
}
static ScStatus surface_prepare(TwinWindow *w,ScU32 width,ScU32 height)
{
    if(w->pixels && w->surface_width==width && w->surface_height==height)return 0;
    if(!width || !height || width>1920u || height>1080u)return fail(SC32_E_RANGE,5);
    ScU32 bytes=width*height*4u;void *fresh;
    ScStatus result=ScMemoryAllocatePages(bytes,&fresh);
    if(result)return fail(result,5);
    /* 候选始终有归属。分配失败保留旧surface；旧页释放失败时保留
     * candidate给正常收束再尝试，不把指针丢失伪称页已恢复。 */
    w->candidate=fresh;
    if(w->pixels){result=ScMemoryFreePages(w->pixels);if(result)return fail(result,5);}
    w->pixels=w->candidate;w->candidate=0;
    w->surface_width=width;w->surface_height=height;w->surface_bytes=bytes;w->full_needed=1;
    return 0;
}
static ScU32 color(ScU32 role)
{return 0xFF000000u|(theme.colors[role]&0x00FFFFFFu);}
static ScU32 blend(ScU32 first,ScU32 second,ScU32 amount)
{
    ScU32 a=theme.colors[first],b=theme.colors[second],inverse=32u-amount;
    ScU32 red=(((a>>16)&255u)*inverse+((b>>16)&255u)*amount)/32u;
    ScU32 green=(((a>>8)&255u)*inverse+((b>>8)&255u)*amount)/32u;
    ScU32 blue=((a&255u)*inverse+(b&255u)*amount)/32u;
    return 0xFF000000u|(red<<16)|(green<<8)|blue;
}
static ScI32 bounded(ScI32 value,ScI32 low,ScI32 high)
{
    if(high<low)return low;
    if(value<low)return low;
    return value>high?high:value;
}
static ScU32 scene(TwinWindow *w,ScU32 x,ScU32 y)
{
    ScU32 width=w->surface_width,height=w->surface_height;
    ScU32 accent=w->index==0?SC32_COLOR_ACCENT:w->index==1?SC32_COLOR_VIOLET:SC32_COLOR_GOLD;
    if(y<32u){
        if(x>=12u && x<width-12u && y>=9u && y<13u){
            if(x<24u)return color(w->focused?SC32_COLOR_GREEN:SC32_COLOR_MUTED);
            if(x<28u)return color(SC32_COLOR_FACE);
            if(x<28u+(width-40u)*(w->activity&31u)/32u)return color(accent);
        }
        return blend(SC32_COLOR_FACE_ALT,SC32_COLOR_PAPER,y);
    }
    if(w->index==2){
        if(x>=12u && x<width-12u && y>=42u && y<height-12u){
            if(x<15u)return color(SC32_COLOR_GOLD);
            return color(SC32_COLOR_FACE_ALT);
        }
        return color(SC32_COLOR_PAPER);
    }
    ScU32 split=width/2u;
    if(y>=height-26u && y<height-10u){
        ScU32 step=(width-24u)/6u,chip=step>20u?16u:step-4u;
        for(ScU32 i=0;i<6u;i++){
            ScU32 left=12u+i*step+(step-chip)/2u;
            if(x>=left && x<left+chip){
                ScU32 active=i<3u?(w->buttons&(1u<<i)):(w->modifiers&(1u<<(i-3u)));
                return color(active?(i<3u?accent:SC32_COLOR_GREEN):SC32_COLOR_FACE_ALT);
            }
        }
    }
    if(y>=42u && y<height-38u && x>=12u && x<width-12u){
        if(x==12u || x==split-6u || x==split+6u || x==width-13u || y==42u || y==height-39u)
            return color(SC32_COLOR_LINE);
        if(x>split-6u && x<split+6u)return color(SC32_COLOR_PAPER);
        if(x<split-6u){
            ScI32 mx=bounded(w->pointer_x,20,(ScI32)split-15),my=bounded(w->pointer_y,50,(ScI32)height-47);
            ScI32 dx=(ScI32)x-mx,dy=(ScI32)y-my;
            if(dx*dx+dy*dy<=16)return color(w->captured?SC32_COLOR_GOLD:accent);
            if(x<16u)return color(w->captured?SC32_COLOR_GOLD:accent);
            return color(SC32_COLOR_FACE_ALT);
        }
        if(y>=50u && y<54u && x>=split+15u && x<width-20u){
            ScU32 bar=split+15u+(width-split-35u)*(w->model_revision&15u)/16u;
            return color(x<bar?SC32_COLOR_GREEN:SC32_COLOR_LINE);
        }
        return color(SC32_COLOR_PAPER);
    }
    return color(SC32_COLOR_PAPER);
}
static void render(TwinWindow *w,ScU32 x,ScU32 y,ScU32 width,ScU32 height)
{
    for(ScU32 row=y;row<y+height;row++){
        ScU32 *target=w->pixels+row*w->surface_width+x;
        for(ScU32 column=x;column<x+width;column++)*target++=scene(w,column,row);
    }
}
static ScStatus paint(TwinWindow *w)
{
    ScPaintInfo claim;record_init(&claim,sizeof(claim));
    ScStatus result=ScPaintBegin(w->window,&claim);
    if(result==SC32_E_AGAIN)return 0;
    if(result)return fail(result,6);
    result=surface_prepare(w,claim.client_width,claim.client_height);if(result)return result;
    ScU32 x=w->full_needed?0u:(ScU32)claim.x,y=w->full_needed?0u:(ScU32)claim.y;
    ScU32 width=w->full_needed?claim.client_width:claim.width,height=w->full_needed?claim.client_height:claim.height;
    if(x>claim.client_width || y>claim.client_height || width>claim.client_width-x || height>claim.client_height-y)
        return fail(SC32_E_RANGE,6);
    render(w,x,y,width,height);
    ScSubmitRect rectangle={x,y,x,y,width,height};ScSubmitDesc submit;
    zero(&submit,sizeof(submit));record_init(&submit,sizeof(submit));
    submit.window=w->window;submit.generation=claim.generation;submit.geometry_epoch=claim.geometry_epoch;
    submit.pixels=w->pixels;submit.bytes=w->surface_bytes;submit.width=w->surface_width;
    submit.height=w->surface_height;submit.stride=w->surface_width*4u;submit.rectangles=&rectangle;submit.rectangle_count=1;
    result=ScWindowSubmit(&submit);
    if(result==SC32_E_STALE || result==SC32_E_AGAIN){w->full_needed=1;return 0;}
    if(result)return fail(result,7);
    report.submit_calls++;ScU32 bytes=width*height*4u,previous=report.submitted_lo;
    report.submitted_lo+=bytes;if(report.submitted_lo<previous)report.submitted_hi++;
    result=ScPaintEnd(&claim);
    if(result==SC32_E_STALE){w->full_needed=1;return 0;}
    if(result)return fail(result,8);
    w->full_needed=0;report.paints++;cover(21);return 0;
}
static int write_part(ScFile file,const void *data,ScU32 bytes)
{
    ScIoResult result;record_init(&result,sizeof(result));
    ScStatus status=ScFileWrite(file,data,bytes,&result);
    if(status){fail(status,9);return 0;}
    if(result.transferred!=bytes){fail(SC32_E_IO,9);return 0;}
    return 1;
}
static int write_log(ScU32 status)
{
    ScU32 focused=0;report.alive_mask=0;
    for(ScU32 i=0;i<3u;i++){
        TwinWindow *w=windows+i;
        if(w->live){
            if(refresh(w))return 0;
            ScInputSnapshot input;record_init(&input,sizeof(input));
            ScStatus checked=ScInputQuery(w->window,&input);if(checked){fail(checked,10);return 0;}
            focused+=input.focused!=0;report.alive_mask|=1u<<i;
        }
        snapshots[i]=w->info;
    }
    if(focused>1u){fail(SC32_E_FAILED,10);return 0;}
    if(windows[2].live && (windows[2].info.state&SC32_WINDOW_MODAL) && !(windows[0].info.state&SC32_WINDOW_INPUT))cover(0);
    ScMonotonicClock clock;record_init(&clock,sizeof(clock));
    ScStatus checked=ScSystemMonotonic(&clock);if(checked){fail(checked,10);return 0;}
    report.last_tick=clock.ticks.lo;report.status=report.error_count?TWIN_FAILED:status;
    report.snapshot_count++;report.file_bytes=512u+report.event_count*64u;
    ScFileOpenDesc description={sizeof(ScFileOpenDesc),1,"/HOME/M10P/GUI.LOG",
        SC32_FILE_WRITE|SC32_FILE_CREATE|SC32_FILE_TRUNCATE,63,{0,0,0}};
    ScFile file;checked=ScFileOpen(&description,&file);if(checked){fail(checked,11);return 0;}
    int success=write_part(file,&report,sizeof(report)) && write_part(file,snapshots,sizeof(snapshots))
        && write_part(file,event_counts,sizeof(event_counts)) && write_part(file,events,report.event_count*sizeof(ScMessage));
    if(success){checked=ScFileFlush(file);if(checked){fail(checked,12);success=0;}}
    checked=ScFileClose(file);if(checked){fail(checked,13);success=0;}
    return success;
}
static ScStatus console_mark(const char *text)
{
    if(!console_available)return 0;
    ScU32 length=0;while(text[length])length++;
    if(!write_part(console_file,text,length))return report.first_code;
    ScStatus result=ScFileFlush(console_file);return result?fail(result,14):0;
}
static ScStatus hidden_step(void)
{
    TwinWindow *b=windows+1;
    if(b->live && !(b->info.state&SC32_WINDOW_VISIBLE)){
        b->model_revision++;b->full_needed=1;cover(16);
        return invalidate(b,1);
    }
    return 0;
}
static ScStatus toggle_b(void)
{
    TwinWindow *b=windows+1;if(!b->live)return 0;
    if(refresh(b))return report.first_code;
    ScU32 action=b->info.state&SC32_WINDOW_VISIBLE?SC32_WINDOW_HIDE:SC32_WINDOW_SHOW;
    record_init(&b->info,sizeof(b->info));ScStatus result=ScWindowControl(b->window,action,&b->info);
    if(result)return fail(result,15);
    b->full_needed=1;return 0;
}
static ScStatus resize_b(void)
{
    TwinWindow *b=windows+1;if(!b->live)return 0;
    if(refresh(b))return report.first_code;
    if(!(b->info.state&SC32_WINDOW_VISIBLE) || !b->pixels)return 0;
    ScWindowInfo previous=b->info;
    ScStatus result=ScWindowInvalidate(b->window,0,0,1,1);if(result)return fail(result,16);
    ScPaintInfo old_claim;record_init(&old_claim,sizeof(old_claim));
    result=ScPaintBegin(b->window,&old_claim);if(result)return fail(result,16);
    ScU32 width=previous.client_width>170u?previous.client_width-32u:previous.client_width+32u;
    record_init(&b->info,sizeof(b->info));
    result=ScWindowPlace(b->window,previous.x,previous.y,width,previous.client_height,&b->info);
    if(result)return fail(result,16);
    result=ScPaintEnd(&old_claim);if(result!=SC32_E_STALE)return fail(SC32_E_FAILED,17);
    cover(20);
    ScSubmitRect rectangle={0,0,0,0,1,1};ScSubmitDesc submit;zero(&submit,sizeof(submit));record_init(&submit,sizeof(submit));
    submit.window=b->window;submit.generation=previous.generation;submit.geometry_epoch=previous.geometry_epoch;
    submit.pixels=b->pixels;submit.bytes=b->surface_bytes;submit.width=b->surface_width;
    submit.height=b->surface_height;submit.stride=b->surface_width*4u;submit.rectangles=&rectangle;submit.rectangle_count=1;
    result=ScWindowSubmit(&submit);if(result!=SC32_E_STALE)return fail(SC32_E_FAILED,18);
    cover(19);b->full_needed=1;return invalidate(b,1);
}
static ScStatus motion(TwinWindow *w,const ScMessage *message)
{
    ScI32 old_x=bounded(w->pointer_x,20,(ScI32)w->info.client_width/2-15);
    ScI32 old_y=bounded(w->pointer_y,50,(ScI32)w->info.client_height-47);
    w->pointer_x=(ScI32)message->data[0];w->pointer_y=(ScI32)message->data[1];
    ScI32 x=bounded(w->pointer_x,20,(ScI32)w->info.client_width/2-15);
    ScI32 y=bounded(w->pointer_y,50,(ScI32)w->info.client_height-47);
    if(w->captured && (w->pointer_x<0 || w->pointer_y<0 || (ScU32)w->pointer_x>=w->info.client_width || (ScU32)w->pointer_y>=w->info.client_height))cover(10);
    if(old_x==x && old_y==y)return 0;
    ScI32 left=old_x<x?old_x:x,top=old_y<y?old_y:y;
    ScI32 right=old_x>x?old_x:x,bottom=old_y>y?old_y:y;
    if(top<4 || left<4 || (ScU32)(right+5)>w->info.client_width || (ScU32)(bottom+5)>w->info.client_height)return 0;
    ScStatus result=ScWindowInvalidate(w->window,left-4,top-4,(ScU32)(right-left+9),(ScU32)(bottom-top+9));
    return result?fail(result,19):0;
}
static ScStatus procedure(ScWindow window,const ScMessage *message,void *context)
{
    TwinWindow *w=context;w->window=window;
    int saved=remember(message);
    if(message->id==SC32_MSG_CREATE){
        w->live=1;w->full_needed=1;w->pointer_x=28;w->pointer_y=64;
        return saved?refresh(w):report.first_code;
    }
    if(message->id==SC32_MSG_DESTROY){w->live=0;w->captured=0;return surface_drop(w);}
    if(!saved || done)return 0;
    if(message->id==SC32_MSG_PAINT)return paint(w);
    if(message->id==SC32_MSG_SIZE || message->id==SC32_MSG_MOVE){
        if(refresh(w))return report.first_code;
        cover(message->id==SC32_MSG_SIZE?17u:18u);
        if(message->id==SC32_MSG_SIZE)w->full_needed=1;
        return 0;
    }
    if(message->id==SC32_MSG_VISIBILITY){
        if(refresh(w))return report.first_code;
        if(w->index==1)cover(message->data[0]?15u:14u);
        w->full_needed=1;return 0;
    }
    if(message->id==SC32_MSG_FOCUS){
        w->focused=message->data[0]!=0;
        if(w->focused && w->index<2u)cover(w->index==0?1u:2u);
        if(!w->focused)cover(26);
        w->activity++;return invalidate(w,0);
    }
    if(message->id==SC32_MSG_CAPTURELOST){w->captured=0;cover(25);return invalidate(w,1);}
    if(message->id==SC32_MSG_THEME || message->id==SC32_MSG_DISPLAY){
        record_init(&theme,sizeof(theme));ScStatus result=ScThemeQuery(&theme);
        if(result)return fail(result,20);
        if(refresh(w))return report.first_code;
        w->full_needed=1;return invalidate(w,1);
    }
    if(message->id==SC32_MSG_CLOSE){
        w->close_count++;
        if(w->index==0 && w->close_count==1u){
            ScStatus result=ScWindowRejectClose(window);if(result)return fail(result,21);
            result=ScWindowSetTitle(window,"Twin A | Close rejected");if(result)return fail(result,21);
            cover(11);w->activity++;return invalidate(w,0);
        }
        if(w->index==1)cover(12);
        if(w->index==2)cover(13);
        return ScDefaultWindowProc(window,message,context);
    }
    if(message->id==SC32_MSG_MOUSEMOVE)return motion(w,message);
    if(message->id==SC32_MSG_BUTTONDOWN || message->id==SC32_MSG_BUTTONUP){
        w->buttons=message->data[2];w->modifiers=message->data[4];w->activity++;
        w->pointer_x=(ScI32)message->data[0];w->pointer_y=(ScI32)message->data[1];
        if(message->id==SC32_MSG_BUTTONDOWN){
            for(ScU32 bit=0;bit<3u;bit++){
                if(message->data[3]&(1u<<bit))cover(5u+bit);
            }
            if(w->index==0 && (message->data[3]&1u) && w->pointer_x>=12 && w->pointer_x<(ScI32)w->info.client_width/2-6 && w->pointer_y>=42 && w->pointer_y<(ScI32)w->info.client_height-38){
                ScStatus result=ScPointerCapture(window,1);if(result)return fail(result,22);
                w->captured=1;cover(8);
            }
        }else if(w->index==0 && w->captured && (message->data[3]&1u)){
            ScStatus result=ScPointerCapture(window,0);if(result)return fail(result,22);
            w->captured=0;cover(9);
        }
        return invalidate(w,1);
    }
    if(message->id==SC32_MSG_KEYDOWN || message->id==SC32_MSG_KEYUP){
        ScU32 physical=message->data[0];w->modifiers=message->data[3];w->activity++;
        if(physical<255u && !(message->flags&SC32_MSG_PULSE)){
            if(message->id==SC32_MSG_KEYDOWN)w->held[physical>>5]|=1u<<(physical&31u);
            else w->held[physical>>5]&=~(1u<<(physical&31u));
        }
        cover(message->id==SC32_MSG_KEYDOWN?3u:24u);
        if(hidden_step())return report.first_code;
        if(message->id==SC32_MSG_KEYDOWN && !message->data[4]){
            ScU32 symbol=message->data[1];
            if(symbol==TWIN_F5)return toggle_b();
            if(symbol==TWIN_F6)return resize_b();
            if(symbol==TWIN_F9){(void)write_log(TWIN_SNAPSHOT);return 0;}
            if(symbol==TWIN_F10){report.flags|=TWIN_FLAG_FINISH;(void)write_log(TWIN_FINISHED);done=1;return 0;}
        }
        return invalidate(w,1);
    }
    if(message->id==SC32_MSG_TEXT){
        cover(4);w->activity++;
        if(hidden_step())return report.first_code;
        return invalidate(w,0);
    }
    return ScDefaultWindowProc(window,message,context);
}
static ScStatus create_window(ScU32 index,const char *title,ScI32 x,ScI32 y,ScU32 width,ScU32 height,ScU32 style,ScU32 owner)
{
    TwinWindow *w=windows+index;w->index=index;record_init(&w->info,sizeof(w->info));
    ScWindowDesc description;zero(&description,sizeof(description));record_init(&description,sizeof(description));
    description.title=title;description.x=x;description.y=y;description.client_width=width;description.client_height=height;
    description.style=style;description.owner_window=owner;description.owner_generation=owner?windows[0].info.generation:0;
    description.procedure=procedure;description.context=w;
    ScWindow created=0;ScStatus result=ScWindowCreate(&description,&created);
    if(result)return fail(result,23);
    w->window=created;return 0;
}
static int any_live(void){return windows[0].live || windows[1].live || windows[2].live;}

int main(void)
{
    const char *magic="SC32TWIN";for(ScU32 i=0;i<8u;i++)report.magic[i]=magic[i];
    report.version=1;report.header_bytes=128;report.event_capacity=TWIN_LOG_CAPACITY;
    report.event_size=64;report.info_size=96;report.info_count=3;
    ScProcessRef self;record_init(&self,sizeof(self));ScStatus result=ScProcessSelf(&self);
    if(result){fail(result,24);return 1;}
    report.pid=self.pid;report.pid_generation=self.generation;
    console_available=ScFileGetStandard(1,&console_file)==0;
    record_init(&theme,sizeof(theme));record_init(&display,sizeof(display));
    ScMemoryInfo memory;record_init(&memory,sizeof(memory));
    if((result=ScThemeQuery(&theme)) || (result=ScDisplayQuery(&display)) || (result=ScSystemMemory(&memory))){fail(result,25);return 1;}
    report.heap_before=report.heap_after=memory.process_heap_pages;
    ScPathStatInfo directory;record_init(&directory,sizeof(directory));
    result=ScPathStat("/HOME/M10P",&directory);
    if(result){result=ScPathCreate("/HOME/M10P",SC32_PATH_DIRECTORY,63);if(result){fail(result,26);return 1;}}
    else if(directory.kind!=SC32_PATH_DIRECTORY){fail(SC32_E_INVALID,26);return 1;}
    ScI32 width=((ScI32)display.width-52)/2-2;
    if(width>360)width=360;
    if(width<94)width=94;
    ScI32 work=(ScI32)display.height-(ScI32)theme.taskbar_height;
    ScI32 height=work-(ScI32)theme.title_height-81;
    if(height>220)height=220;
    if(height<80)height=80;
    ScI32 x=((ScI32)display.width-(2*(width+2)+20))/2;if(x<0)x=0;
    ScI32 y=(work-(height+(ScI32)theme.title_height+1))/2;if(y<0)y=0;
    ScI32 modal_width=width-32;
    if(modal_width>240)modal_width=240;
    if(modal_width<94)modal_width=94;
    ScI32 modal_height=height-56;
    if(modal_height>120)modal_height=120;
    if(modal_height<74)modal_height=74;
    if(create_window(0,"Twin A | capture left | F9 log | F10 finish",x,y,(ScU32)width,(ScU32)height,SC32_STYLE_STANDARD,0)
        || create_window(1,"Twin B | independent | F5 hide | F6 size",x+width+22,y,(ScU32)width,(ScU32)height,SC32_STYLE_STANDARD,0)
        || create_window(2,"Twin C | modal owner A | close me",x+(width-modal_width)/2,y+(ScI32)theme.title_height+20,
            (ScU32)modal_width,(ScU32)modal_height,SC32_STYLE_CLOSE|SC32_STYLE_MODAL,windows[0].window))done=1;
    report.window_a=windows[0].window;report.window_b=windows[1].window;report.window_c=windows[2].window;
    if(!done){report.flags|=TWIN_FLAG_INITIAL_LOG;(void)write_log(TWIN_READY);if(!done)(void)console_mark("SC32_TWIN_READY\n");}
    while(!done){
        ScMessage message;record_init(&message,sizeof(message));result=ScMessageGet(&message,SC32_INFINITE);
        if(result){fail(result,27);break;}
        if(message.id==SC32_MSG_QUEUELOSS){remember(&message);report.flags|=TWIN_FLAG_LOSS;fail(SC32_E_NO_SPACE,28);break;}
        result=ScMessageDispatch(&message);
        /* 显式关闭后的迟到普通事实不会再触及model，DESTROY副本由库吞。
         * 这些已退役目标的STALE是合法清理结果，不伪造新的输入回调。 */
        if(result && result!=SC32_E_STALE){fail(result,29);break;}
        if(!any_live()){report.flags|=TWIN_FLAG_AUTO_END;done=1;}
    }
    cover(22);
    for(ScI32 i=2;i>=0;i--){
        TwinWindow *w=windows+i;
        if(w->live){result=ScWindowDestroy(w->window);if(result && result!=SC32_E_STALE)fail(result,30);}
        (void)surface_drop(w);
    }
    record_init(&memory,sizeof(memory));result=ScSystemMemory(&memory);
    if(result)fail(result,31);else{
        report.heap_after=memory.process_heap_pages;
        if(report.heap_after!=report.heap_before || any_live())fail(SC32_E_FAILED,31);
        else{cover(23);report.flags|=TWIN_FLAG_CLEANUP;}
    }
    (void)write_log(report.error_count?TWIN_FAILED:TWIN_FINISHED);
    if(console_available){
        (void)console_mark(report.error_count?"SC32_TWIN_FAIL\n":"SC32_TWIN_EXIT\n");
        result=ScFileClose(console_file);
        if(result){fail(result,32);(void)write_log(TWIN_FAILED);}
    }
    return report.error_count?1:0;
}
