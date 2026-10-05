/* mio：词法器只识别字符/标记，不执行宏。每个源文件保存独立 Lexer，
 * include 递归返回后不会丢失原文件游标。物理反斜杠换行先被 peek 吃掉，
 * 因而长宏、字符串及分裂的标识符均遵守 C 的行连接顺序；行号仍增加。 */
static int lexpeek(Lexer *l)
{
    for(;;) {
        char *s=l->source+l->offset;
        if(s[0]=='\\' && s[1]=='\n') { l->offset+=2;
            l->line++;
            continue; }
        if(s[0]=='\\' && s[1]=='\r' && s[2]=='\n') { l->offset+=3;
            l->line++;
            continue; }
        return (u8)s[0];
    }
}
static int lexmatch(Lexer *l,const char *s)
{
    Lexer test=*l;
    while(*s) { if(lexpeek(&test)!=(u8)*s++) return 0;
        test.offset++; }
    *l=test;
    return 1;
}
static int hexvalue(int c)
{
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int escape(Lexer *l)
{
    int c=lexpeek(l);
    if(!c) fatal("unfinished escape");
    l->offset++;
    if(c=='n') return '\n';
    if(c=='r') return '\r';
    if(c=='t') return '\t';
    if(c=='b') return '\b';
    if(c=='f') return '\f';
    if(c=='v') return '\v';
    if(c=='a') return 7;
    if(c=='x') {
        u32 value=0;
        int n=0;
        while(hexvalue(lexpeek(l))>=0) { value=value*16+hexvalue(lexpeek(l));
            l->offset++;
            n++; }
        if(!n) fatal("hex escape needs digits");
        return value&255;
    }
    if(c>='0' && c<='7') {
        int value=c-'0',n=1;
        while(n<3 && lexpeek(l)>='0' && lexpeek(l)<='7') { value=value*8+lexpeek(l)-'0';
            l->offset++;
            n++; }
        return value&255;
    }
    return c;
}
static int store_source(Lexer *l,int start)
{
    char text[1024];
    int n=0;
    for(int i=start;i<l->offset;i++) {
        if(l->source[i]=='\\' && l->source[i+1]=='\n') { i++;
            continue; }
        if(l->source[i]=='\\' && l->source[i+1]=='\r' && l->source[i+2]=='\n') { i+=2;
            continue; }
        if(n>=1023) fatal("literal spelling exceeds 1023 bytes");
        text[n++]=l->source[i];
    }
    return store_text(text,n);
}
static int identifier_char(int c)
{ return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' || (c>='0' && c<='9'); }
static void lexnext(Lexer *l,Tok *t)
{
    int c,space=0;
    for(;;) {
        c=lexpeek(l);
        if(c==' ' || c=='\t' || c=='\r' || c=='\v' || c=='\f') { l->offset++;
            space=1;
            continue; }
        if(l->comment) {
            int ended=0;
            while(lexpeek(l)) {
                if(lexmatch(l,"*/")) { l->comment=0;
                    ended=1;
                    break; }
                if(lexpeek(l)=='\n') {
                    t->where=(l->file<<16)|l->line;
                    t->k=T_NL;
                    t->v=0;
                    l->line++;
                    l->offset++;
                    return;
                }
                l->offset++;
            }
            if(!ended) fatal("unfinished comment");
            continue;
        }
        if(lexmatch(l,"//")) { space=1;
            while(lexpeek(l) && lexpeek(l)!='\n') l->offset++;
            continue; }
        if(lexmatch(l,"/*")) { space=1;
            l->comment=1;
            continue; }
        break;
    }
    t->where=(l->file<<16)|l->line|(space?(int)0x80000000u:0);
    error_position=t->where;
    t->v=0;
    if(!c) { t->k=0;
        return; }
    if(c=='\n') { l->offset++;
        l->line++;
        t->k=T_NL;
        return; }
    if(identifier_char(c) && !(c>='0' && c<='9')) {
        char name[128];
        int n=0;
        while(identifier_char(lexpeek(l))) {
            if(n>=127) fatal("identifier exceeds 127 bytes");
            name[n++]=(char)lexpeek(l);
            l->offset++;
        }
        t->k=T_ID;
        t->v=intern(name,n);
        return;
    }
    if(c>='0' && c<='9') {
        int start=l->offset,base=10,digits=0,unsigned_flag=0;
        u32 value=0;
        if(lexmatch(l,"0x") || lexmatch(l,"0X")) base=16;
        else if(c=='0') base=8;
        while(hexvalue(lexpeek(l))>=0 && hexvalue(lexpeek(l))<base) {
            value=value*(u32)base+(u32)hexvalue(lexpeek(l));
            l->offset++;
            digits++;
        }
        if(!digits) fatal("integer literal needs digits");
        while(lexpeek(l)=='u' || lexpeek(l)=='U' || lexpeek(l)=='l' || lexpeek(l)=='L') {
            if(lexpeek(l)=='u' || lexpeek(l)=='U') unsigned_flag=1;
            l->offset++;
        }
        if(identifier_char(lexpeek(l)) || lexpeek(l)=='.') fatal("invalid or unsupported numeric literal");
        t->k=unsigned_flag || value>0x7FFFFFFFu?T_UNUM:T_NUM;
        t->v=store_source(l,start);
        *(u32 *)(pool+t->v-8)=value;
        return;
    }
    if(c=='\"' || c=='\'') {
        int start=l->offset,quote=c,n=0;
        char bytes[1024];
        l->offset++;
        while(lexpeek(l) && lexpeek(l)!=quote) {
            int ch=lexpeek(l);
            l->offset++;
            if(ch=='\n') fatal("newline inside literal");
            if(ch=='\\') ch=escape(l);
            if(n>=1023) fatal("literal exceeds 1023 bytes");
            bytes[n++]=(char)ch;
        }
        if(lexpeek(l)!=quote) fatal("unfinished literal");
        l->offset++;
        if(quote=='\'') {
            if(n!=1) fatal("character literal must contain one byte");
            t->k=T_NUM;
            t->v=store_source(l,start);
            *(u32 *)(pool+t->v-8)=(u8)bytes[0];
        } else { t->k=T_STR;
        t->v=store_text(bytes,n);
            int raw=store_source(l,start);
            *(int *)(pool+t->v-12)=raw; }
        return;
    }
    /* 最长匹配必须先测三字符，否则 >>= 会被拆成 >> 与 =，复合赋值
     * 的副作用与类型规则就不再可能正确恢复。字符串/注释先于此表。 */
    const char *ops[]={"<<=",">>=","...","==","!=","<=",">=","<<",">>","&&","||","++","--","->","+=","-=","*=","/=","%=","&=","|=","^=","##"};
    int kinds[]={T_SHLSET,T_SHRSET,T_ELLIPSIS,T_EQ,T_NE,T_LE,T_GE,T_SHL,T_SHR,T_AND,T_OR,T_INC,T_DEC,T_ARROW,T_ADDSET,T_SUBSET,T_MULSET,T_DIVSET,T_MODSET,T_ANDSET,T_ORSET,T_XORSET,T_PASTE};
    for(int i=0;i<23;i++) if(lexmatch(l,ops[i])) { t->k=kinds[i];
        return; }
    if(c>=128) fatal("non-ASCII identifier is unsupported");
    t->k=c;
    l->offset++;
}
