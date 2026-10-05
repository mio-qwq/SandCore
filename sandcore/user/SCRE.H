#ifndef SCRE_H
#define SCRE_H
#include "SCTEXT.H"
/* 自写Thompson NFA。每个输入字节至多展开512个状态，避免朴素
 * 正则回溯在串口管线中指数爆炸。模式/嵌套超限或不支持反向引用
 * 明确报错；匹配不是猜测成功，编译失败不能降级成字面搜索。 */
#define RE_STATES 512
enum {RE_BYTE,RE_ANY,RE_CLASS,RE_BOL,RE_EOL,RE_SPLIT,RE_MATCH};
typedef struct {int type,a,b,c;u8 set[32];} re_state;
typedef struct {int start,head,tail;} re_fragment;
static re_state re_states[RE_STATES];
static int re_links[RE_STATES*2],re_count,re_error,re_depth,re_start,re_fold;
static const char *re_cursor;
static int re_new(int type,int c,int a,int b)
{if(re_count==RE_STATES){re_error=1;return 0;}int i=re_count++;re_states[i].type=type;re_states[i].c=c;re_states[i].a=a;re_states[i].b=b;
    for(int j=0;j<32;j++)re_states[i].set[j]=0;re_links[i*2]=re_links[i*2+1]=-1;return i;}
static re_fragment re_one(int state,int branch)
{int p=state*2+branch;return (re_fragment){state,p,p};}
static re_fragment re_merge(re_fragment a,re_fragment b)
{if(a.head<0)return b;if(b.head>=0){re_links[a.tail]=b.head;a.tail=b.tail;}return a;}
static void re_patch(re_fragment fragment,int target)
{for(int p=fragment.head;p>=0;p=re_links[p]){if(p&1)re_states[p/2].b=target;else re_states[p/2].a=target;}}
static re_fragment re_expression(void);
static re_fragment re_atom(void)
{
    if(++re_depth>32){re_error=1;re_depth--;return re_one(re_new(RE_SPLIT,0,-1,-2),0);}
    int c=(u8)*re_cursor++,state;
    if(c=='('){re_fragment a=re_expression();if(*re_cursor!=')')re_error=1;else re_cursor++;re_depth--;return a;}
    if(c=='['){
        state=re_new(RE_CLASS,0,-1,-2);int invert=*re_cursor=='^',any=0;if(invert)re_cursor++;
        while(*re_cursor && (*re_cursor!=']' || !any)){
            int lo=(u8)*re_cursor++;if(lo=='\\'){if(!*re_cursor){re_error=1;break;}lo=(u8)*re_cursor++;}
            int hi=lo;if(*re_cursor=='-' && re_cursor[1] && re_cursor[1]!=']'){re_cursor++;hi=(u8)*re_cursor++;if(hi=='\\' && *re_cursor)hi=(u8)*re_cursor++;}
            if(hi<lo){re_error=1;break;}for(int i=lo;i<=hi;i++)re_states[state].set[i>>3]|=(u8)(1u<<(i&7));any=1;
        }
        if(*re_cursor!=']' || !any)re_error=1;else re_cursor++;
        if(re_fold)for(int i='A';i<='Z';i++)if((re_states[state].set[i>>3]&(1u<<(i&7))) || (re_states[state].set[(i+32)>>3]&(1u<<((i+32)&7)))){
            re_states[state].set[i>>3]|=(u8)(1u<<(i&7));re_states[state].set[(i+32)>>3]|=(u8)(1u<<((i+32)&7));}
        if(invert)for(int i=0;i<32;i++)re_states[state].set[i]^=255;
    }else if(c=='\\'){
        if(!*re_cursor)re_error=1;c=(u8)*re_cursor;if(c)re_cursor++;
        if(c>='1'&&c<='9')re_error=1;state=re_new(RE_BYTE,c,-1,-2);
    }else if(c=='.')state=re_new(RE_ANY,0,-1,-2);
    else if(c=='^')state=re_new(RE_BOL,0,-1,-2);
    else if(c=='$')state=re_new(RE_EOL,0,-1,-2);
    else {if(!c || c=='*'||c=='+'||c=='?'||c==')'||c=='|')re_error=1;state=re_new(RE_BYTE,c,-1,-2);}
    re_depth--;return re_one(state,0);
}
static re_fragment re_sequence(void)
{
    re_fragment result={-1,-1,-1};
    while(*re_cursor && *re_cursor!=')' && *re_cursor!='|' && !re_error){
        re_fragment a=re_atom();char op=*re_cursor;
        if(op=='*'||op=='+'||op=='?'){
            re_cursor++;int split=re_new(RE_SPLIT,0,a.start,-1);
            if(op=='?'){re_fragment next=re_one(split,1);next=re_merge(next,a);next.start=split;a=next;}
            else {re_patch(a,split);int start=op=='*'?split:a.start;a=re_one(split,1);a.start=start;}
        }
        if(result.start<0)result=a;else {re_patch(result,a.start);result.head=a.head;result.tail=a.tail;}
    }
    if(result.start<0)result=re_one(re_new(RE_SPLIT,0,-1,-2),0);return result;
}
static re_fragment re_expression(void)
{
    re_fragment a=re_sequence();while(*re_cursor=='|' && !re_error){re_cursor++;re_fragment b=re_sequence();int start=re_new(RE_SPLIT,0,a.start,b.start);a=re_merge(a,b);a.start=start;}return a;
}
static int re_compile(const char *pattern,int extended,int fold)
{
    char basic[2048];int used=0;re_count=re_error=re_depth=0;re_fold=fold;
    if(!extended){for(int i=0;pattern[i];i++){
        char c=pattern[i];if(used>2044)return -1;
        if(c=='\\' && pattern[i+1] && (pattern[i+1]=='('||pattern[i+1]==')'||pattern[i+1]=='|'||pattern[i+1]=='+'||pattern[i+1]=='?'))basic[used++]=pattern[++i];
        else {if(c=='('||c==')'||c=='|'||c=='+'||c=='?')basic[used++]='\\';basic[used++]=c;
            if(c=='\\' && pattern[i+1])basic[used++]=pattern[++i];}
    }basic[used]=0;pattern=basic;}
    re_cursor=pattern;re_fragment a=re_expression();if(*re_cursor || re_error)return -1;
    int end=re_new(RE_MATCH,0,-2,-2);re_patch(a,end);re_start=a.start;return re_error?-1:0;
}
static int re_add(int state,int *list,int *count,u8 *seen,u32 position,u32 size)
{
    int stack[RE_STATES*2],used=0;stack[used++]=state;int match=0;
    while(used){int i=stack[--used];if(i<0 || seen[i])continue;seen[i]=1;re_state *s=&re_states[i];
        if(s->type==RE_MATCH){match=1;continue;}
        if(s->type==RE_SPLIT || (s->type==RE_BOL && !position) || (s->type==RE_EOL && position==size)){
            if(s->a>=0 && !seen[s->a])stack[used++]=s->a;if(s->type==RE_SPLIT && s->b>=0 && !seen[s->b])stack[used++]=s->b;
        }else if(s->type!=RE_BOL && s->type!=RE_EOL)list[(*count)++]=i;
    }return match;
}
static int re_matches(const char *text,u32 size)
{
    int a[RE_STATES],b[RE_STATES],count=0;u8 seen[RE_STATES];for(int i=0;i<RE_STATES;i++)seen[i]=0;
    if(re_add(re_start,a,&count,seen,0,size))return 1;
    for(u32 at=0;at<size;at++){
        for(int i=0;i<RE_STATES;i++)seen[i]=0;int next=0,matched=0;u8 c=(u8)text[at];
        for(int i=0;i<count;i++){re_state *s=&re_states[a[i]];int yes=s->type==RE_ANY || (s->type==RE_CLASS && (s->set[c>>3]&(1u<<(c&7))));
            if(s->type==RE_BYTE){int x=c,y=s->c;if(re_fold){if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;}yes=x==y;}
            if(yes)matched|=re_add(s->a,b,&next,seen,at+1,size);}
        matched|=re_add(re_start,b,&next,seen,at+1,size);if(matched)return 1;count=next;for(int i=0;i<count;i++)a[i]=b[i];
    }return 0;
}
typedef struct {re_state *states;int count,start,fold;} re_program;
static int re_program_compile(re_program *out,const char *pattern,int extended,int fold)
{out->states=0;if(re_compile(pattern,extended,fold)<0)return -1;out->states=sc_alloc((u32)re_count*sizeof(re_state));if(!out->states)return -4;
    for(int i=0;i<re_count;i++)out->states[i]=re_states[i];out->count=re_count;out->start=re_start;out->fold=fold;return 0;}
static void re_program_free(re_program *p){if(p->states)sc_free(p->states);p->states=0;}
static void re_span_add(const re_program *program,int state,u32 origin,int *list,u32 *tags,int *count,u8 *seen,u32 position,u32 size,u32 *best_start,u32 *best_end)
{
    int stack[RE_STATES*2],used=0;stack[used++]=state;
    while(used){int i=stack[--used];if(i<0 || seen[i])continue;seen[i]=1;re_state *s=&program->states[i];
        if(s->type==RE_MATCH){if(origin<*best_start || (origin==*best_start && position>*best_end)){*best_start=origin;*best_end=position;}continue;}
        if(s->type==RE_SPLIT || (s->type==RE_BOL && !position) || (s->type==RE_EOL && position==size)){
            if(s->a>=0 && !seen[s->a])stack[used++]=s->a;if(s->type==RE_SPLIT && s->b>=0 && !seen[s->b])stack[used++]=s->b;
        }else if(s->type!=RE_BOL && s->type!=RE_EOL){list[*count]=i;tags[(*count)++]=origin;}
    }
}
static int re_program_search(const re_program *p,const char *text,u32 size,u32 offset,u32 *begin,u32 *end)
{
    if(offset>size)return 0;int a[RE_STATES],b[RE_STATES],count=0;u32 ta[RE_STATES],tb[RE_STATES],best_start=0xFFFFFFFFu,best_end=0;u8 seen[RE_STATES];
    for(int i=0;i<RE_STATES;i++)seen[i]=0;re_span_add(p,p->start,offset,a,ta,&count,seen,offset,size,&best_start,&best_end);
    /* 活跃状态按最早起点顺序产生，第一次访问即最早标签；较晚标签
     * 到同一状态可安全丢弃。每输入位置至多展开512状态，不从每个
     * 候选起点重新扫描全文。找到最左匹配后继续求同起点最长结束。 */
    for(u32 at=offset;at<size;at++){for(int i=0;i<RE_STATES;i++)seen[i]=0;int next=0;u8 c=(u8)text[at];
        for(int i=0;i<count;i++){if(ta[i]>best_start)continue;re_state *s=&p->states[a[i]];int yes=s->type==RE_ANY || (s->type==RE_CLASS && (s->set[c>>3]&(1u<<(c&7))));
            if(s->type==RE_BYTE){int x=c,y=s->c;if(p->fold){if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;}yes=x==y;}
            if(yes)re_span_add(p,s->a,ta[i],b,tb,&next,seen,at+1,size,&best_start,&best_end);}
        if(best_start==0xFFFFFFFFu)re_span_add(p,p->start,at+1,b,tb,&next,seen,at+1,size,&best_start,&best_end);
        count=next;for(int i=0;i<count;i++){a[i]=b[i];ta[i]=tb[i];}if(!count && best_start!=0xFFFFFFFFu)break;
    }
    if(best_start==0xFFFFFFFFu)return 0;*begin=best_start;*end=best_end;return 1;
}
#endif
