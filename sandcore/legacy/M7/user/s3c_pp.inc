/* =====================================================================
 * mio：宏预处理。展开作用于标记而不是字符串替换，因此注释/字符串
 * 中同名内容不会被替换，函数参数也按嵌套括号而不是第一个逗号切开。
 * include 与宏递归分别受 DEPTH_MAX 约束；参数/替换缓冲属于 BSS，
 * 深层展开不会在用户栈上叠出几十 KB 的二维数组。
 * 原始参数用于 #/##，普通参数先展开；随后禁用当前宏再重扫替换体，
 * 防止直接/间接递归，同时允许 f(f(1)) 的内层参数先正常展开。
 * ===================================================================== */
static int macro_find(int name)
{ for(int i=0;i<macro_count;i++) if(macros[i].active && macros[i].name==name) return i;
    return -1; }
static void pp_add(Tok *out,int *used,int capacity,Tok *t)
{
    if(*used>=capacity) fatal("preprocessor token capacity exceeded");
    tokcopy(out+*used,t);
    (*used)++;
}
static const char *operator_text(int k)
{
    const char *ops[]={"==","!=","<=",">=","<<",">>","&&","||","++","--","->","+=","-=","*=","/=","%=","&=","|=","^=","<<=",">>=","...","##"};
    int kinds[]={T_EQ,T_NE,T_LE,T_GE,T_SHL,T_SHR,T_AND,T_OR,T_INC,T_DEC,T_ARROW,T_ADDSET,T_SUBSET,T_MULSET,T_DIVSET,T_MODSET,T_ANDSET,T_ORSET,T_XORSET,T_SHLSET,T_SHRSET,T_ELLIPSIS,T_PASTE};
    for(int i=0;i<23;i++) if(k==kinds[i]) return ops[i];
    return 0;
}
static void token_text(Tok *t,char *out,int capacity)
{
    if(t->k==T_ID) copy(out,spelling(t->v),capacity);
    else if(t->k==T_STR || t->k==T_NUM || t->k==T_UNUM) copy(out,spelling(raw_text(t)),capacity);
    else if(t->k<256) { out[0]=(char)t->k;
        out[1]=0; }
    else {
        const char *s=operator_text(t->k);
        if(!s) fatal("cannot spell token");
        copy(out,s,capacity);
    }
}
static void string_token(Tok *t,const char *s,int n,int where)
{
    t->k=T_STR;
    t->v=store_text(s,n);
    t->where=where;
    char raw[2048];
    int used=0;
    raw[used++]='"';
    for(int i=0;i<n;i++) {
        if(s[i]=='"' || s[i]=='\\') raw[used++]='\\';
        if(used>2044) fatal("stringification capacity exceeded");
        raw[used++]=s[i];
    }
    raw[used++]='"';
    *(int *)(pool+t->v-12)=store_text(raw,used);
}
static void stringify(Tok *tokens,int n,Tok *out,int where)
{
    char text[1024],part[1024];
    int used=0;
    for(int i=0;i<n;i++) {
        token_text(tokens+i,part,sizeof(part));
        int size=length(part);
        if(i && ((u32)tokens[i].where&0x80000000u)) {
            if(used>=1023) fatal("stringification exceeds 1023 bytes");
            text[used++]=' ';
        }
        if(used+size>1023) fatal("stringification exceeds 1023 bytes");
        for(int j=0;j<size;j++) text[used++]=part[j];
    }
    string_token(out,text,used,where);
}
static void paste(Tok *left,Tok *right)
{
    /* 空参数在 ## 边上保留占位，不能把它前面的无关 token 一并粘走。 */
    if(left->k==-1) { tokcopy(left,right);
        return; }
    if(right->k==-1) return;
    char text[1024],part[1024];
    token_text(left,text,sizeof(text));
    token_text(right,part,sizeof(part));
    if(length(text)+length(part)>=1024) fatal("token paste exceeds 1023 bytes");
    append(text,part,sizeof(text));
    Lexer l;
    l.source=text;
    l.offset=0;
    l.line=1;
    l.file=0;
    l.comment=0;
    Tok joined,end;
    lexnext(&l,&joined);
    lexnext(&l,&end);
    if(end.k || !joined.k) fatal("token paste must form exactly one token");
    joined.where=left->where;
    tokcopy(left,&joined);
}
static int macro_parameter(Macro *m,int name)
{ for(int i=0;i<m->argc;i++) if(m->params[i]==name) return i;
    return -1; }
static int macro_alias(int name)
{
    /* 对象宏可给函数宏起别名：#define ALIAS FUNC。只跟单标记链，
     * 一旦看到函数宏即可把紧随调用的括号纳入本次重扫；循环受宏数限制。 */
    int i=macro_find(name);
    for(int n=0;n<MACRO_MAX && i>=0;n++) {
        if(macros[i].argc>=0) return i;
        if(macros[i].count!=1 || macro_body[macros[i].first].k!=T_ID) return -1;
        i=macro_find(macro_body[macros[i].first].v);
    }
    return -1;
}
static void pp_expand(Tok *input,int count,int depth,Tok *output,int *used,int capacity)
{
    if(depth>=DEPTH_MAX) fatal("macro expansion depth exceeded");
    for(int i=0;i<count;i++) {
        Tok *t=input+i;
        error_position=t->where;
        if(word(t,"__LINE__")) { Tok number_token;
            make_number(&number_token,(u32)(t->where&65535),t->where);
            pp_add(output,used,capacity,&number_token);
            continue; }
        if(word(t,"__FILE__")) {
            Tok string;
            int file=((u32)t->where>>16)&32767;
            string_token(&string,sources[file].path,length(sources[file].path),t->where);
            pp_add(output,used,capacity,&string);
            continue;
        }
        int original=t->k==T_ID?macro_find(t->v):-1,mi=original;
        if(mi<0 || macro_busy[mi]) { pp_add(output,used,capacity,t);
            continue; }
        Macro *m=macros+mi;
        if(m->argc<0 && i+1<count && input[i+1].k=='(') {
            int alias=macro_alias(t->v);
            if(alias>=0 && !macro_busy[alias]) { mi=alias;
                m=macros+mi; }
        }
        if(m->argc>=0) {
            if(i+1>=count || input[i+1].k!='(') { pp_add(output,used,capacity,t);
                continue; }
            for(int a=0;a<MACRO_ARGS;a++) macro_arg_lengths[depth][a]=0;
            int nesting=0,argument=0,closed=0;
            i+=2;
            for(;i<count;i++) {
                int k=input[i].k;
                if(k==')' && nesting==0) { closed=1;
                    break; }
                if(k==',' && nesting==0 && !(m->variadic && argument==m->argc-1)) {
                    argument++;
                    if(argument>=MACRO_ARGS) fatal("too many macro arguments");
                    continue;
                }
                if(k=='(') nesting++;
                if(k==')') nesting--;
                pp_add(macro_arguments[depth][argument],&macro_arg_lengths[depth][argument],MACRO_ARG_TOKENS,input+i);
            }
            if(!closed) fatal("unfinished macro invocation");
            int argc=argument+1;
            if(argc==1 && !macro_arg_lengths[depth][0] && m->argc==0) argc=0;
            if((!m->variadic && argc!=m->argc) || (m->variadic && argc<m->argc-1)) fatal("macro argument count mismatch");
        }
        Tok *replacement=replacements[depth];
        int n=0,pasting=0;
        for(int b=0;b<m->count;b++) {
            Tok *body=macro_body+m->first+b;
            if(body->k==T_PASTE) { if(!b || b==m->count-1) fatal("paste cannot be at macro edge");
                pasting=1;
                continue; }
            int parameter=body->k==T_ID?macro_parameter(m,body->v):-1;
            if(body->k=='#' && b+1<m->count) {
                int p=macro_parameter(m,macro_body[m->first+b+1].v);
                if(p>=0) {
                    Tok string;
                    stringify(macro_arguments[depth][p],macro_arg_lengths[depth][p],&string,t->where);
                    if(pasting && n) paste(replacement+n-1,&string);
                    else pp_add(replacement,&n,LINE_MAX,&string);
                    pasting=0;
                    b++;
                    continue;
                }
            }
            if(parameter>=0) {
                Tok *arg=macro_arguments[depth][parameter];
                int amount=macro_arg_lengths[depth][parameter];
                int raw=pasting || (b+1<m->count && macro_body[m->first+b+1].k==T_PASTE);
                if(raw) {
                    if(!amount && b+1<m->count && macro_body[m->first+b+1].k==T_PASTE) {
                        Tok empty;
                        empty.k=-1;
                        empty.v=0;
                        empty.where=body->where;
                        pp_add(replacement,&n,LINE_MAX,&empty);
                    }
                    for(int j=0;j<amount;j++) {
                        if(pasting && j==0 && n) paste(replacement+n-1,arg+j);
                        else pp_add(replacement,&n,LINE_MAX,arg+j);
                    }
                } else pp_expand(arg,amount,depth+1,replacement,&n,LINE_MAX);
            } else {
                if(pasting && n) paste(replacement+n-1,body);
                else pp_add(replacement,&n,LINE_MAX,body);
            }
            pasting=0;
        }
        int compact=0;
        for(int j=0;j<n;j++) if(replacement[j].k!=-1) tokcopy(replacement+compact++,replacement+j);
        n=compact;
        /* 替换结果最后若是函数宏，与原调用后紧随的 (...) 一起重扫。
         * 例如 ID(F)(1)；其余后续标记仍在外层处理，不一直禁用当前宏。 */
        if(n && replacement[n-1].k==T_ID && macro_alias(replacement[n-1].v)>=0
           && i+1<count && input[i+1].k=='(') {
            int nesting=0;
            i++;
            do {
                if(i>=count) fatal("unfinished rescanned macro invocation");
                if(input[i].k=='(') nesting++;
                if(input[i].k==')') nesting--;
                pp_add(replacement,&n,LINE_MAX,input+i);
                if(nesting) i++;
            } while(nesting);
        }
        macro_busy[original]=macro_busy[mi]=1;
        pp_expand(replacement,n,depth+1,output,used,capacity);
        macro_busy[original]=macro_busy[mi]=0;
    }
}
static int pp_precedence(int op)
{
    if(op==T_OR) return 1;
    if(op==T_AND) return 2;
    if(op=='|') return 3;
    if(op=='^') return 4;
    if(op=='&') return 5;
    if(op==T_EQ || op==T_NE) return 6;
    if(op=='<' || op=='>' || op==T_LE || op==T_GE) return 7;
    if(op==T_SHL || op==T_SHR) return 8;
    if(op=='+' || op=='-') return 9;
    if(op=='*' || op=='/' || op=='%') return 10;
    return 0;
}
static u32 pp_expression(Tok *ts,int count,int *at,int minimum,int evaluate)
{
    if(*at>=count) fatal("incomplete #if expression");
    Tok *t=ts+*at;
    (*at)++;
    u32 value=0;
    if(t->k==T_NUM || t->k==T_UNUM) value=number(t);
    else if(t->k==T_ID) value=0;
    else if(t->k=='(') {
        value=pp_expression(ts,count,at,1,evaluate);
        if(*at>=count || ts[*at].k!=')') fatal("#if needs closing parenthesis");
        (*at)++;
    } else if(t->k=='!' || t->k=='~' || t->k=='-' || t->k=='+') {
        value=pp_expression(ts,count,at,11,evaluate);
        if(t->k=='!') value=!value;
        if(t->k=='~') value=~value;
        if(t->k=='-') value=0u-value;
    } else fatal("invalid #if expression");
    while(*at<count) {
        int op=ts[*at].k,precedence=pp_precedence(op);
        if(precedence<minimum) break;
        (*at)++;
        int right_evaluate=evaluate;
        if((op==T_AND && !value) || (op==T_OR && value)) right_evaluate=0;
        u32 right=pp_expression(ts,count,at,precedence+1,right_evaluate);
        if(!evaluate) { value=0;
            continue; }
        if(op=='+') value+=right;
        else if(op=='-') value-=right;
        else if(op=='*') value*=right;
        else if(op=='/' || op=='%') {
            if(!right) fatal("division by zero in #if");
            if(value==0x80000000u && right==0xFFFFFFFFu) fatal("signed division overflow in #if");
            if(op=='/') value=(u32)((int)value/(int)right);
            else value=(u32)((int)value%(int)right);
        } else if(op=='&') value&=right;
        else if(op=='^') value^=right;
        else if(op=='|') value|=right;
        else if(op==T_SHL) value<<=(right&31);
        else if(op==T_SHR) value=(u32)((int)value>>(right&31));
        else if(op==T_EQ) value=value==right;
        else if(op==T_NE) value=value!=right;
        else if(op=='<') value=(int)value<(int)right;
        else if(op=='>') value=(int)value>(int)right;
        else if(op==T_LE) value=(int)value<=(int)right;
        else if(op==T_GE) value=(int)value>=(int)right;
        else if(op==T_AND) value=value && right;
        else if(op==T_OR) value=value || right;
    }
    if(minimum==1 && *at<count && ts[*at].k=='?') {
        (*at)++;
        u32 yes=pp_expression(ts,count,at,1,evaluate && value!=0);
        if(*at>=count || ts[*at].k!=':') fatal("#if conditional needs colon");
        (*at)++;
        u32 no=pp_expression(ts,count,at,1,evaluate && value==0);
        value=value?yes:no;
    }
    return value;
}
static int pp_condition(Tok *line,int count)
{
    int used=0;
    for(int i=0;i<count;i++) {
        if(word(line+i,"defined")) {
            int paren=0;
            if(++i<count && line[i].k=='(') { paren=1;
                i++; }
            if(i>=count || line[i].k!=T_ID) fatal("defined needs an identifier");
            Tok value;
            make_number(&value,macro_find(line[i].v)>=0,line[i].where);
            pp_add(condition_tokens,&used,LINE_MAX,&value);
            if(paren && (++i>=count || line[i].k!=')')) fatal("defined needs closing parenthesis");
        } else pp_add(condition_tokens,&used,LINE_MAX,line+i);
    }
    int expanded=0;
    pp_expand(condition_tokens,used,0,replacements[DEPTH_MAX-1],&expanded,LINE_MAX);
    int at=0;
    u32 value=pp_expression(replacements[DEPTH_MAX-1],expanded,&at,1,1);
    if(at!=expanded) fatal("extra token in #if");
    return value!=0;
}
static int load_source(const char *path)
{
    for(int i=0;i<source_count;i++) if(equal(path,sources[i].path)) return i;
    u32 info[2];
    if(sc_stat(path,info) || info[0]!=1) return -1;
    if(source_count>=FILE_MAX || length(path)>63) fatal("include file capacity exceeded");
    if(info[1]>(u32)(SOURCE_MAX-source_used-1)) fatal("source arena capacity exceeded");
    int i=source_count++;
    copy(sources[i].path,path,64);
    sources[i].source=source_used;
    sources[i].size=(int)info[1];
    int n=sc_read(path,source_arena+source_used,(int)info[1]);
    if(n!=(int)info[1]) fatal("source read failed");
    for(int k=0;k<n;k++) if(!source_arena[source_used+k]) fatal("NUL inside source file");
    source_arena[source_used+n]=0;
    source_used+=n+1;
    return i;
}
static void pp_flush(void)
{
    if(raw_used) pp_expand(raw_chunk,raw_used,0,tokens,&token_count,TOKEN_MAX-1);
    raw_used=0;
}
static void pp_file(const char *path,int depth);
static void pp_include(Tok *line,int count,const char *current,int depth)
{
    int n=0;
    pp_expand(line,count,0,replacements[DEPTH_MAX-1],&n,LINE_MAX);
    Tok *ts=replacements[DEPTH_MAX-1];
    char name[64]={0};
    if(n==1 && ts[0].k==T_STR) copy(name,spelling(ts[0].v),sizeof(name));
    else if(n>=3 && ts[0].k=='<' && ts[n-1].k=='>') {
        for(int i=1;i<n-1;i++) { char part[128];
            token_text(ts+i,part,sizeof(part));
            if(length(name)+length(part)>63) fatal("include path too long");
            append(name,part,sizeof(name)); }
    } else fatal("include needs quoted or angle filename");
    char candidate[64];
    copy(candidate,current,sizeof(candidate));
    int end=length(candidate);
    while(end && candidate[end-1]!='/') end--;
    candidate[end]=0;
    if(end+length(name)<64) {
        append(candidate,name,sizeof(candidate));
        if(load_source(candidate)>=0) { pp_file(candidate,depth+1);
            return; }
    }
    copy(candidate,"SYS/INC/",sizeof(candidate));
    if(length(candidate)+length(name)>63) fatal("include path too long");
    append(candidate,name,sizeof(candidate));
    if(load_source(candidate)<0) fatal("include file not found");
    pp_file(candidate,depth+1);
}
static void pp_define(Tok *line,int count,Lexer *lexer,int depth)
{
    if(count<3 || line[2].k!=T_ID) fatal("define needs an identifier");
    int mi=macro_find(line[2].v);
    if(mi<0) { if(macro_count>=MACRO_MAX) fatal("macro capacity exceeded");
        mi=macro_count++; }
    Macro *m=macros+mi;
    m->name=line[2].v;
    m->active=1;
    m->argc=-1;
    m->variadic=0;
    int at=3;
    Lexer adjacent=*lexer;
    adjacent.offset=line_ends[depth][2];
    if(at<count && line[at].k=='(' && lexpeek(&adjacent)=='(') {
        m->argc=0;
        at++;
        while(at<count && line[at].k!=')') {
            if(line[at].k==T_ELLIPSIS) {
                if(m->argc>=MACRO_ARGS) fatal("too many macro parameters");
                m->variadic=1;
                m->params[m->argc++]=intern("__VA_ARGS__",11);
                at++;
                break;
            }
            if(line[at].k!=T_ID || m->argc>=MACRO_ARGS) fatal("invalid macro parameter");
            for(int i=0;i<m->argc;i++) if(m->params[i]==line[at].v) fatal("duplicate macro parameter");
            m->params[m->argc++]=line[at++].v;
            if(at>=count || line[at].k!=',') break;
            at++;
            if(at>=count || line[at].k==')') fatal("missing macro parameter after comma");
        }
        if(at>=count || line[at].k!=')') fatal("macro parameter list is unfinished");
        at++;
    }
    m->first=macro_body_used;
    m->count=count-at;
    if(macro_body_used+m->count>MACRO_BODY_MAX) fatal("macro body capacity exceeded");
    for(;at<count;at++) tokcopy(macro_body+macro_body_used++,line+at);
}
static void pp_directive(Tok *line,int count,Lexer *l,int depth,int baseline)
{
    if(count<2) return;
    int active=!condition_count || conditions[condition_count-1].active;
    if(word(line+1,"if") || word(line+1,"ifdef") || word(line+1,"ifndef")) {
        if(condition_count>=128) fatal("conditional nesting exceeded");
        int truth=0;
        if(active) {
            if(word(line+1,"if")) truth=pp_condition(line+2,count-2);
            else {
                if(count!=3 || line[2].k!=T_ID) fatal("ifdef needs one identifier");
                truth=macro_find(line[2].v)>=0;
                if(word(line+1,"ifndef")) truth=!truth;
            }
        }
        Conditional *c=conditions+condition_count++;
        c->parent=active;
        c->active=active && truth;
        c->taken=c->active;
        c->otherwise=0;
        return;
    }
    if(word(line+1,"elif") || word(line+1,"else") || word(line+1,"endif")) {
        if(condition_count<=baseline) fatal("unmatched conditional directive");
        Conditional *c=conditions+condition_count-1;
        if(word(line+1,"endif")) { condition_count--;
            return; }
        if(c->otherwise) fatal("branch after #else");
        if(word(line+1,"else")) { c->otherwise=1;
            c->active=c->parent && !c->taken;
            c->taken=1; }
        else { c->active=c->parent && !c->taken && pp_condition(line+2,count-2);
            if(c->active) c->taken=1; }
        return;
    }
    if(!active) return;
    if(word(line+1,"define")) pp_define(line,count,l,depth);
    else if(word(line+1,"undef")) {
        if(count!=3 || line[2].k!=T_ID) fatal("undef needs one identifier");
        int i=macro_find(line[2].v);
        if(i>=0) macros[i].active=0;
    } else if(word(line+1,"include")) pp_include(line+2,count-2,sources[l->file].path,depth);
    else if(word(line+1,"pragma") && count==3 && word(line+2,"once")) source_once[l->file]=1;
    else if(word(line+1,"error")) fatal("active #error directive");
    else fatal("unsupported preprocessing directive");
}
static int pp_incomplete(void)
{
    for(int i=0;i<raw_used;i++) if(raw_chunk[i].k==T_ID && macro_alias(raw_chunk[i].v)>=0) {
        if(i+1==raw_used) return 1;
        if(raw_chunk[i+1].k!='(') continue;
        int nesting=0,j=i+1;
        for(;j<raw_used;j++) { if(raw_chunk[j].k=='(') nesting++;
            if(raw_chunk[j].k==')' && --nesting==0) break; }
        if(j==raw_used) return 1;
        i=j;
    }
    return 0;
}
static void pp_file(const char *path,int depth)
{
    if(depth>=DEPTH_MAX) fatal("include depth exceeded");
    int file=load_source(path);
    if(file<0) fatal("source file not found");
    if(source_once[file]) return;
    Lexer l;
    l.source=source_arena+sources[file].source;
    l.offset=0;
    l.line=1;
    l.file=file;
    l.comment=0;
    int baseline=condition_count;
    for(;;) {
        int n=0;
        Tok t;
        for(;;) {
            lexnext(&l,&t);
            if(!t.k || t.k==T_NL) break;
            if(n>=LINE_MAX) fatal("logical line exceeds token capacity");
            tokcopy(lines[depth]+n,&t);
            line_ends[depth][n]=l.offset;
            n++;
        }
        if(n && lines[depth][0].k=='#') {
            pp_flush();
            pp_directive(lines[depth],n,&l,depth,baseline);
        } else if(!condition_count || conditions[condition_count-1].active) {
            if(n) lines[depth][0].where|=(int)0x80000000u;
            for(int i=0;i<n;i++) pp_add(raw_chunk,&raw_used,16384,lines[depth]+i);
            if(raw_used>8192 && !pp_incomplete()) pp_flush();
        }
        if(!t.k) break;
    }
    pp_flush();
    if(condition_count!=baseline) fatal("unfinished conditional in source file");
}
static void preprocess(const char *path)
{
    /* 架构与编译器身份是本工具真实提供的宏，用户可用它选择 API 内联
     * 汇编兼容分支。不是冒充宿主 GCC，也不伪造不提供的标准库能力。 */
    const char *predefined[]={"__SCCC__","__i386__","__STDC_HOSTED__"};
    for(int i=0;i<3;i++) {
        Macro *m=macros+macro_count++;
        m->name=intern(predefined[i],length(predefined[i]));
        m->active=1;
        m->argc=-1;
        m->first=macro_body_used;
        m->count=1;
        make_number(macro_body+macro_body_used++,i==2?0:1,0);
    }
    pp_file(path,0);
    tokens[token_count].k=0;
    tokens[token_count].v=0;
    tokens[token_count].where=token_count?tokens[token_count-1].where:0;
}
