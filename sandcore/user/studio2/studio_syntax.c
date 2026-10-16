static void line_free(StLine *line){
    free(line->text);
    free(line->tokens);
    memset(line,0,sizeof(*line));
}
static void syntax_free(StDoc *d){
    for(U i=0;i<d->line_count;i++)line_free(d->lines+i);
    free(d->lines);
    d->lines=0;
    d->line_count=0;
}
static ScStatus token_add(StLine *l,U a,U b,U role)
{
    if(a==b)return 0;
    if(l->count==l->capacity){
        U cap=l->capacity?l->capacity*2u:8u;
        if(cap>1048576u/sizeof(StToken))return SC32_E_RANGE;
        StToken *p=realloc(l->tokens,cap*sizeof(*p));
        if(!p)return SC32_E_MEMORY;
        l->tokens=p;
        l->capacity=cap;
    }
    StToken *t=l->tokens+l->count++;
    t->start=a;
    t->end=b;
    t->role=role;
    return 0;
}
static int identifier(int c){
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(c>='0'&&c<='9');
}
static int keyword(const char *p,U n)
{
    const char *words[]={
        "auto","break","case","char","const","continue","default","do","else","enum","extern","for","goto","if","int","long","register","return","short","signed","sizeof","static","struct","switch","typedef","union","unsigned","void","volatile","while","inline","restrict","_Bool"
    };
    for(U i=0;i<sizeof(words)/sizeof(words[0]);i++)if(strlen(words[i])==n&&!memcmp(words[i],p,n))return 1;
    return 0;
}
static ScStatus lex_line(StLine *l,U state)
{
    l->state_in=state;
    l->count=0;
    U i=0;
    int first=1;
    ScStatus s=0;
    while(i<l->bytes&&s>=0){
        U start=i;
        int c=(unsigned char)l->text[i],role=SC32_COLOR_TEXT;
        if(state==1u){
            role=SC32_COLOR_MUTED;
            while(i<l->bytes){
                if(l->text[i]=='*'&&i+1u<l->bytes&&l->text[i+1]=='/'){
                    i+=2;
                    state=0;
                    break;
                }
                i++;
            }
        }
        else if(state==2u||state==3u){
            int end=state==2u?'"':'\'';
            role=SC32_COLOR_GREEN;
            int escaped=0;
            while(i<l->bytes){
                int v=l->text[i++];
                if(escaped){
                    escaped=0;
                    continue;
                }
                if(v=='\\'){
                    escaped=1;
                    continue;
                }
                if(v==end){
                    state=0;
                    break;
                }
            }
            if(state&&(!l->bytes||l->text[l->bytes-1]!='\\'))state=0;
        }
        else if(c=='/'&&i+1u<l->bytes&&l->text[i+1]=='/'){
            i=l->bytes;
            role=SC32_COLOR_MUTED;
        }
        else if(c=='/'&&i+1u<l->bytes&&l->text[i+1]=='*'){
            state=1;
            i+=2;
            role=SC32_COLOR_MUTED;
            while(i<l->bytes){
                if(l->text[i]=='*'&&i+1u<l->bytes&&l->text[i+1]=='/'){
                    i+=2;
                    state=0;
                    break;
                }
                i++;
            }
        }
        else if(c=='"'||c=='\''){
            state=c=='"'?2u:3u;
            role=SC32_COLOR_GREEN;
            i++;
            int escaped=0;
            while(i<l->bytes){
                int v=l->text[i++];
                if(escaped){
                    escaped=0;
                    continue;
                }
                if(v=='\\'){
                    escaped=1;
                    continue;
                }
                if(v==c){
                    state=0;
                    break;
                }
            }
            if(state&&l->text[l->bytes-1]!='\\')state=0;
        }
        else if(first&&c=='#'){
            i=l->bytes;
            role=SC32_COLOR_VIOLET;
        }
        else if(c>='0'&&c<='9'){
            i++;
            while(i<l->bytes&&(identifier((unsigned char)l->text[i])||l->text[i]=='.'))i++;
            role=SC32_COLOR_GOLD;
        }
        else if(identifier(c)){
            i++;
            while(i<l->bytes&&identifier((unsigned char)l->text[i]))i++;
            if(keyword(l->text+start,i-start))role=SC32_COLOR_ACCENT;
        }
        else{
            i++;
            if(c!=' '&&c!='\t'&&c!='\r')first=0;
        }
        if(c!=' '&&c!='\t'&&c!='\r')first=0;
        if(role!=SC32_COLOR_TEXT)s=token_add(l,start,i,(U)role);
    }
    l->state_out=state;
    return s;
}
static void syntax(StDoc *d)
{
    ScUiDocumentInfo info;
    rec(&info,sizeof(info));
    if(ScUiDocumentQuery(d->text,&info)<0)return;
    ScString *snapshot=0;
    ScStatus s=ScUiDocumentCopy(d->text,0,info.bytes,&snapshot);
    ScView view;
    rec(&view,sizeof(view));
    if(s>=0)s=ScStringView(snapshot,&view);
    U count=info.lines;
    StLine *fresh=s>=0?calloc(count,sizeof(*fresh)):0;
    if(s>=0&&!fresh)s=SC32_E_MEMORY;
    U at=0,state=0,spans=0;
    const char *p=view.data;
    /* 公共Document当前提供字节快照及整批span替换，所以拆行/提交是
     * O(n)；词法与每行分配只发生在变更处，未变行直接转移拥有缓存。
     * 插删行用两端行数差对齐后继，注释出入状态不等时继续真实分析。 */
    for(U i=0;i<count&&s>=0;i++){
        U start=at;
        while(at<view.bytes&&p[at]!='\n')at++;
        U n=at-start;
        if(at<view.bytes)at++;
        StLine *f=fresh+i,*old=0;
        if(i<d->line_count && d->lines[i].text && d->lines[i].bytes==n&&!memcmp(d->lines[i].text,p+start,n))old=d->lines+i;
        int shifted=(int)i+(int)d->line_count-(int)count;
        if(!old&&shifted>=0&&(U)shifted<d->line_count){
            StLine *candidate=d->lines+(U)shifted;
            if(candidate->text&&candidate->bytes==n&&!memcmp(candidate->text,p+start,n))old=candidate;
        }
        if(old){
            *f=*old;
            memset(old,0,sizeof(*old));
            if(f->state_in!=state)s=lex_line(f,state);
        }
        else{
            f->text=malloc(n+1u);
            if(!f->text){
                s=SC32_E_MEMORY;
                break;
            }
            memcpy(f->text,p+start,n);
            f->text[n]=0;
            f->bytes=n;
            s=lex_line(f,state);
        }
        state=f->state_out;
        if(f->count>0xffffffffu-spans)s=SC32_E_RANGE;
        else spans+=f->count;
    }
    ScUiStyleSpan *all=s>=0&&spans?calloc(spans,sizeof(*all)):0;
    if(s>=0&&spans&&!all)s=SC32_E_MEMORY;
    U used=0,offset=0;
    for(U i=0;i<count&&s>=0;i++){
        StLine *l=fresh+i;
        for(U k=0;k<l->count;k++){
            ScUiStyleSpan *r=all+used++;
            rec(r,sizeof(*r));
            r->start=offset+l->tokens[k].start;
            r->end=offset+l->tokens[k].end;
            r->foreground_role=l->tokens[k].role;
            r->background_role=SC32_COLOR_PAPER;
            r->revision=info.revision;
        }
        offset+=l->bytes+1u;
    }
    if(s>=0)s=ScUiEditSetSpans(st.ui,d->edit,all,used);
    free(all);
    ScStringRelease(snapshot);
    if(s>=0){
        syntax_free(d);
        d->lines=fresh;
        d->line_count=count;
        d->syntax_revision=info.revision;
    }
    else{
        if(fresh){
            for(U i=0;i<count;i++)line_free(fresh+i);
            free(fresh);
        }
        syntax_free(d);
        d->syntax_revision=info.revision;
        status("语法着色资源不足，文本和撤销历史保留");
    }
}
