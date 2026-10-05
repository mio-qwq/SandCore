/* =====================================================================
 * mio：C 类型图与递归下降语法。类型先决定字节数/指针步长，再建有类型
 * 的表达式节点，后端不重新猜测“这是地址还是整数”。数组在取值时
 * 衰减为指针，在 sizeof/取地址时保留数组身份，因此二维索引的步长
 * 由完整元素类型决定，不硬写成四字节。
 *
 * 局部名字退出作用域后变不可见，符号记录仍保留到函数生成完毕。
 * 若立刻复用符号槽，先前语法树中的变量就会错误指向后一个同名变量。
 * 所有声明先检查容量，聚合/数组大小检查溢出，错误不进入代码生成。
 * ===================================================================== */
#define C_VOID 0
#define C_CHAR 1
#define C_UCHAR 2
#define C_SHORT 3
#define C_USHORT 4
#define C_INT 5
#define C_UINT 6
#define Q_UNSIGNED 1
#define Q_CONST 2
#define Q_VOLATILE 4
#define D_STATIC 1
#define D_EXTERN 2
#define D_TYPEDEF 4
#define D_INLINE 8

static int switch_node;
static char joined_literal[8192];
static Tok *current_token(void) { error_position=tokens[position].where;
    return tokens+position; }
static int take(int k) { if(current_token()->k!=k) return 0;
    position++;
    return 1; }
static int take_word(const char *s) { if(!word(current_token(),s)) return 0;
    position++;
    return 1; }
static void need(int k) { if(!take(k)) fatal("unexpected token; required punctuation is missing"); }
static int new_type(int kind,int size,int base,int count,int fields,int flags)
{
    if(type_count>=TYPE_MAX) fatal("type capacity exceeded");
    int i=type_count++;
    Type *t=types+i;
    t->kind=kind;
    t->size=size;
    t->base=base;
    t->count=count;
    t->fields=fields;
    t->flags=flags;
    return i;
}
static int qualified(int type,int flags)
{
    if((types[type].flags|flags)==types[type].flags) return type;
    Type *t=types+type;
    return new_type(t->kind,t->size,t->base,t->count,t->fields,t->flags|flags);
}
static int pointer_to(int base)
{
    for(int i=0;i<type_count;i++) if(types[i].kind==TY_PTR && types[i].base==base && !types[i].flags) return i;
    return new_type(TY_PTR,4,base,0,0,0);
}
static int array_of(int base,int count)
{
    if(count<0 || (count && types[base].size>0x3D0000/count)) fatal("array size exceeds address space");
    return new_type(TY_ARRAY,types[base].size*count,base,count,0,0);
}
static int alignment(int type)
{
    Type *t=types+type;
    if(t->kind==TY_ARRAY) return alignment(t->base);
    if(t->kind==TY_STRUCT || t->kind==TY_UNION) return t->base?t->base:1;
    return t->size>4?4:t->size?t->size:1;
}
static int promoted(int type)
{
    Type *t=types+type;
    if(t->kind==TY_ARRAY) return pointer_to(t->base);
    if(t->kind==TY_FUNC) return pointer_to(type);
    if(t->kind==TY_CHAR || t->kind==TY_SHORT) return C_INT;
    return type;
}
static int find_symbol(int name,int tags)
{
    for(int i=symbol_count-1;i>0;i--) {
        Symbol *s=symbols+i;
        if(s->name==name && s->scope>=0 && (tags?(s->kind==S_TAG):(s->kind!=S_TAG && s->kind!=S_FIELD))) return i;
    }
    return 0;
}
static int symbol_new(int name,int type,int kind,int value,int scope)
{
    if(symbol_count>=SYMBOL_MAX) fatal("symbol capacity exceeded");
    int i=symbol_count++;
    Symbol *s=symbols+i;
    s->name=name;
    s->type=type;
    s->kind=kind;
    s->value=value;
    s->scope=scope;
    s->defined=0;
    s->next=0;
    s->aux=0;
    return i;
}
static int node_new(int kind,int type,int a,int b,int c,int value)
{
    if(node_count>=NODE_MAX) fatal("function syntax tree capacity exceeded");
    int i=node_count++;
    Node *n=nodes+i;
    n->kind=kind;
    n->type=type;
    n->a=a;
    n->b=b;
    n->c=c;
    n->value=value;
    n->where=error_position;
    n->next=0;
    return i;
}
static int constant(u32 value,int type) { return node_new(N_CONST,type,0,0,0,(int)value); }
static int expression(int minimum);
static int declaration_spec(int *storage);
static int declarator(int base,int *name,int abstract);
static int statement(void);
static int initialize(int type,int symbol,int offset,int global);
static int aggregate(int kind)
{
    int name=0,type=-1,tag=0;
    if(current_token()->k==T_ID) { name=current_token()->v;
        position++;
        tag=find_symbol(name,1);
        if(tag) type=symbols[tag].type; }
    if(type<0) {
        type=new_type(kind,0,1,0,0,0);
        if(name) tag=symbol_new(name,type,S_TAG,0,scope_depth);
    }
    if(types[type].kind!=kind) fatal("aggregate tag kind mismatch");
    if(!take('{')) return type;
    if(types[type].fields) fatal("aggregate is already defined");
    int first=0,last=0,size=0,max_align=1;
    while(!take('}')) {
        int storage=0,base=declaration_spec(&storage);
        if(base<0 || storage) fatal("aggregate member needs an ordinary type");
        do {
            int member=0,mt=declarator(base,&member,0);
            if(!member || !types[mt].size || types[mt].kind==TY_FUNC) fatal("invalid aggregate member");
            for(int f=first;f;f=symbols[f].next) if(symbols[f].name==member) fatal("duplicate aggregate member");
            int align=alignment(mt);
            if(align>max_align) max_align=align;
            int at=kind==TY_UNION?0:(size+align-1)/align*align;
            if(at>0x3D0000-types[mt].size) fatal("aggregate exceeds address space");
            int f=symbol_new(member,mt,S_FIELD,at,-2);
            if(last) symbols[last].next=f;
            else first=f;
            last=f;
            if(at+types[mt].size>size) size=at+types[mt].size;
            if(current_token()->k==':') fatal("bit-fields are unsupported in this version");
        } while(take(','));
        need(';');
    }
    types[type].size=(size+max_align-1)/max_align*max_align;
    types[type].base=max_align;
    types[type].fields=first;
    types[type].count=1;
    (void)tag;
    return type;
}
static int enumeration(void)
{
    if(current_token()->k==T_ID) position++;
    if(!take('{')) return C_INT;
    u32 value=0;
    while(!take('}')) {
        if(current_token()->k!=T_ID) fatal("enum needs an identifier");
        int name=current_token()->v;
        position++;
        if(take('=')) value=constant_value(expression(2));
        int old=find_symbol(name,0);
        if(old && symbols[old].scope==scope_depth) fatal("duplicate enum name");
        symbol_new(name,C_INT,S_ENUM,(int)value,scope_depth);
        value++;
        if(!take(',')) { need('}');
            break; }
    }
    return C_INT;
}
static int declaration_spec(int *storage)
{
    int type=-1,flags=0,sign=0,short_flag=0,long_count=0,builtin=0;
    for(;;) {
        Tok *t=current_token();
        if(take_word("const")) flags|=Q_CONST;
        else if(take_word("volatile")) flags|=Q_VOLATILE;
        else if(take_word("static")) *storage|=D_STATIC;
        else if(take_word("extern")) *storage|=D_EXTERN;
        else if(take_word("typedef")) *storage|=D_TYPEDEF;
        else if(take_word("inline") || take_word("__inline__")) *storage|=D_INLINE;
        else if(take_word("register") || take_word("auto")) {}
        else if(take_word("unsigned")) { sign=1;
            builtin=1; }
        else if(take_word("signed")) { sign=-1;
            builtin=1; }
        else if(take_word("short")) { short_flag=1;
            builtin=1; }
        else if(take_word("long")) { long_count++;
            builtin=1;
            if(long_count>1) fatal("64-bit integers are unsupported"); }
        else if(take_word("int")) { type=C_INT;
            builtin=1; }
        else if(take_word("char")) { type=C_CHAR;
            builtin=1; }
        else if(take_word("void")) type=C_VOID;
        else if(take_word("struct")) type=aggregate(TY_STRUCT);
        else if(take_word("union")) type=aggregate(TY_UNION);
        else if(take_word("enum")) type=enumeration();
        else if(take_word("float") || take_word("double")) fatal("floating point is unsupported");
        else if(t->k==T_ID && type<0 && !builtin) {
            int s=find_symbol(t->v,0);
            if(!s || symbols[s].kind!=S_TYPEDEF) break;
            type=symbols[s].type;
            position++;
        } else break;
    }
    if(type<0 && builtin) type=C_INT;
    if(type<0) return -1;
    if(short_flag) { if(type!=C_INT) fatal("short qualifier needs int");
        type=C_SHORT; }
    if(sign==1) {
        if(type==C_CHAR) type=C_UCHAR;
        else if(type==C_SHORT) type=C_USHORT;
        else if(type==C_INT) type=C_UINT;
        else fatal("unsigned qualifier needs integer type");
    }
    return qualified(type,flags);
}
static int suffix(int base)
{
    if(take('[')) {
        int count=0;
        if(!take(']')) { count=(int)constant_value(expression(2));
            need(']'); }
        int inner=suffix(base);
        return array_of(inner,count);
    }
    if(take('(')) {
        int first=0,last=0,argc=0,variadic=0;
        if(!take(')')) {
            if(word(current_token(),"void") && tokens[position+1].k==')') { position+=2; }
            else for(;;) {
                if(take(T_ELLIPSIS)) { variadic=1;
                    need(')');
                    break; }
                int storage=0,type=declaration_spec(&storage),name=0;
                if(type<0) fatal("parameter needs a type");
                type=declarator(type,&name,1);
                if(types[type].kind==TY_ARRAY) type=pointer_to(types[type].base);
                if(types[type].kind==TY_FUNC) type=pointer_to(type);
                if(!types[type].size || types[type].kind==TY_STRUCT || types[type].kind==TY_UNION) fatal("aggregate by-value parameters are unsupported");
                if(param_count>=PARAM_MAX) fatal("parameter capacity exceeded");
                int p=param_count++;
                parameters[p].name=name;
                parameters[p].type=type;
                parameters[p].next=0;
                if(last) parameters[last].next=p;
                else first=p;
                last=p;
                argc++;
                if(take(')')) break;
                need(',');
            }
        }
        if(types[base].kind==TY_ARRAY || types[base].kind==TY_FUNC || types[base].kind==TY_STRUCT || types[base].kind==TY_UNION) fatal("unsupported function return type");
        return new_type(TY_FUNC,0,base,argc,first,variadic);
    }
    return base;
}
static void replace_hole(int type,int hole,int replacement)
{
    if(types[type].base==hole) { types[type].base=replacement;
        if(types[type].kind==TY_ARRAY) types[type].size=types[replacement].size*types[type].count;
        return;
    }
    if(types[type].kind==TY_PTR || types[type].kind==TY_ARRAY || types[type].kind==TY_FUNC) replace_hole(types[type].base,hole,replacement);
}
static int declarator(int base,int *name,int abstract)
{
    while(take('*')) {
        base=pointer_to(base);
        int flags=0;
        while(word(current_token(),"const") || word(current_token(),"volatile")) {
            if(take_word("const")) flags|=Q_CONST;
            else { position++;
                flags|=Q_VOLATILE; }
        }
        base=qualified(base,flags);
    }
    if(take('(')) {
        int hole=new_type(TY_VOID,0,0,0,0,0);
        int inner=declarator(hole,name,abstract);
        need(')');
        int outside=suffix(base);
        if(inner==hole) return outside;
        replace_hole(inner,hole,outside);
        return inner;
    }
    if(current_token()->k==T_ID) { *name=current_token()->v;
        position++; }
    else if(!abstract) fatal("declaration needs an identifier");
    return suffix(base);
}
static int type_ahead(void)
{
    Tok *t=current_token();
    const char *words[]={"void","char","short","int","long","unsigned","signed","struct","union","enum","const","volatile"};
    for(int i=0;i<12;i++) if(word(t,words[i])) return 1;
    int s=t->k==T_ID?find_symbol(t->v,0):0;
    return s && symbols[s].kind==S_TYPEDEF;
}
static int lvalue(int n)
{ return (nodes[n].kind==N_SYMBOL && symbols[nodes[n].value].kind!=S_FUNCTION) || nodes[n].kind==N_DEREF; }
static int binary(int op,int left,int right)
{
    int a=promoted(nodes[left].type),b=promoted(nodes[right].type),type=C_INT;
    if(op=='+' || op=='-') {
        if(types[a].kind==TY_PTR) type=a;
        else if(op=='+' && types[b].kind==TY_PTR) type=b;
        if(op=='-' && types[a].kind==TY_PTR && types[b].kind==TY_PTR) type=C_INT;
    }
    if(type==C_INT && op!=T_EQ && op!=T_NE && op!=T_LE && op!=T_GE && op!='<' && op!='>' && op!=T_AND && op!=T_OR)
        type=(types[a].flags|types[b].flags)&Q_UNSIGNED?C_UINT:C_INT;
    return node_new(N_BINARY,type,left,right,0,op);
}
static int primary(void)
{
    Tok *t=current_token();
    int n=0;
    if(t->k==T_NUM || t->k==T_UNUM) { n=constant(number(t),t->k==T_UNUM?C_UINT:C_INT);
        position++; }
    else if(t->k==T_STR) {
        int bytes=0;
        char *literal=joined_literal;
        while(current_token()->k==T_STR) {
            int at=current_token()->v,size=text_size(at);
            if(bytes+size>=8192) fatal("joined string literal exceeds 8191 bytes");
            for(int i=0;i<size;i++) literal[bytes++]=pool[at+i];
            position++;
        }
        if(data_used+bytes+1>DATA_MAX) fatal("initialized data capacity exceeded");
        int at=data_used;
        for(int i=0;i<bytes;i++) data[data_used++]=(u8)literal[i];
        data[data_used++]=0;
        n=node_new(N_STRING,array_of(C_CHAR,bytes+1),0,0,0,at);
    } else if(t->k==T_ID) {
        int name=t->v,s=find_symbol(name,0);
        position++;
        if(!s && current_token()->k=='(') {
            int type=new_type(TY_FUNC,0,C_INT,-1,0,0);
            s=symbol_new(name,type,S_FUNCTION,0,0);
        }
        if(!s || symbols[s].kind==S_TYPEDEF) fatal("unknown identifier");
        if(symbols[s].kind==S_ENUM) n=constant((u32)symbols[s].value,C_INT);
        else n=node_new(N_SYMBOL,symbols[s].type,0,0,0,s);
    } else if(take('(')) { n=expression(1);
    need(')'); }
    else fatal("expression needs a value");
    for(;;) {
        if(take('[')) {
            int subscript=expression(1);
            need(']');
            int pt=promoted(nodes[n].type);
            if(types[pt].kind!=TY_PTR) fatal("subscript needs an array or pointer");
            n=node_new(N_DEREF,types[pt].base,binary('+',n,subscript),0,0,0);
        } else if(current_token()->k=='.' || current_token()->k==T_ARROW) {
            int arrow=current_token()->k==T_ARROW;
            position++;
            int type=nodes[n].type;
            if(arrow) {
                type=promoted(type);
                if(types[type].kind!=TY_PTR) fatal("arrow needs a pointer");
                type=types[type].base;
                n=node_new(N_DEREF,type,n,0,0,0);
            }
            if(types[type].kind!=TY_STRUCT && types[type].kind!=TY_UNION) fatal("member access needs aggregate");
            if(current_token()->k!=T_ID) fatal("member access needs a name");
            int f=types[type].fields;
            while(f && symbols[f].name!=current_token()->v) f=symbols[f].next;
            if(!f) fatal("unknown aggregate member");
            position++;
            int field_type=qualified(symbols[f].type,types[type].flags&(Q_CONST|Q_VOLATILE));
            int address=node_new(N_ADDR,pointer_to(type),n,0,0,0);
            /* 字段偏移以字节加，不允许普通指针步长再次乘 sizeof(struct)。 */
            address=node_new(N_CAST,pointer_to(C_CHAR),address,0,0,0);
            address=binary('+',address,constant((u32)symbols[f].value,C_INT));
            n=node_new(N_DEREF,field_type,address,0,0,0);
        } else if(take('(')) {
            int type=promoted(nodes[n].type);
            if(types[type].kind!=TY_PTR || types[types[type].base].kind!=TY_FUNC) fatal("call needs a function");
            type=types[type].base;
            int args=0,argc=0;
            if(!take(')')) do {
                int value=expression(2);
                args=node_new(N_ARG,nodes[value].type,value,args,0,0);
                argc++;
                if(take(')')) break;
                need(',');
            } while(1);
            if(types[type].count>=0 && ((!types[type].flags && argc!=types[type].count) || (types[type].flags && argc<types[type].count))) fatal("function argument count mismatch");
            n=node_new(N_CALL,types[type].base,n,args,0,argc);
        } else if(current_token()->k==T_INC || current_token()->k==T_DEC) {
            int delta=current_token()->k==T_INC?1:-1;
            position++;
            if(!lvalue(n) || (types[nodes[n].type].flags&Q_CONST)) fatal("increment needs a writable lvalue");
            n=node_new(N_INC,nodes[n].type,n,0,1,delta);
        } else break;
    }
    return n;
}
static int unary(void)
{
    if(take_word("sizeof")) {
        int type;
        if(current_token()->k=='(') {
            int saved=position;
            position++;
            if(type_ahead()) { int storage=0,name=0;
                type=declaration_spec(&storage);
                type=declarator(type,&name,1);
                need(')'); }
            else { position=saved;
                type=nodes[unary()].type; }
        } else type=nodes[unary()].type;
        if(!types[type].size) fatal("sizeof needs a complete object type");
        return constant((u32)types[type].size,C_UINT);
    }
    if(current_token()->k=='(') {
        int saved=position;
        position++;
        if(type_ahead()) {
            int storage=0,name=0,type=declaration_spec(&storage);
            type=declarator(type,&name,1);
            need(')');
            return node_new(N_CAST,type,unary(),0,0,0);
        }
        position=saved;
    }
    int op=current_token()->k;
    if(op=='*' || op=='&' || op=='+' || op=='-' || op=='!' || op=='~' || op==T_INC || op==T_DEC) {
        position++;
        int a=unary(),type=promoted(nodes[a].type);
        if(op=='*') { if(types[type].kind!=TY_PTR) fatal("dereference needs pointer");
            return node_new(N_DEREF,types[type].base,a,0,0,0); }
        if(op=='&') { if(!lvalue(a) && nodes[a].kind!=N_STRING && types[nodes[a].type].kind!=TY_FUNC) fatal("address needs an object");
            return node_new(N_ADDR,pointer_to(nodes[a].type),a,0,0,0); }
        if(op==T_INC || op==T_DEC) {
            if(!lvalue(a) || (types[nodes[a].type].flags&Q_CONST)) fatal("increment needs writable lvalue");
            return node_new(N_INC,nodes[a].type,a,0,0,op==T_INC?1:-1);
        }
        return node_new(N_UNARY,op=='!'?C_INT:type,a,0,0,op);
    }
    return primary();
}
static int precedence(int op)
{
    if(op==',') return 1;
    if(op=='=' || (op>=T_ADDSET && op<=T_SHRSET)) return 2;
    if(op=='?') return 3;
    int p=pp_precedence(op);
    return p?p+3:0;
}
static int expression(int minimum)
{
    int left=unary();
    for(;;) {
        int op=current_token()->k,p=precedence(op);
        if(p<minimum) break;
        position++;
        if(op=='?') {
            int yes=expression(1);
            need(':');
            int no=expression(3);
            int type=promoted(nodes[yes].type);
            if(types[type].kind!=TY_PTR && (types[promoted(nodes[no].type)].flags&Q_UNSIGNED)) type=C_UINT;
            left=node_new(N_COND,type,left,yes,no,0);
        } else {
            int assignment=p==2,right=expression(assignment?p:p+1);
            if(assignment) {
                if(!lvalue(left) || (types[nodes[left].type].flags&Q_CONST) || types[nodes[left].type].kind==TY_ARRAY) fatal("assignment needs writable lvalue");
                left=node_new(N_ASSIGN,nodes[left].type,left,right,0,op);
            } else if(op==',') left=node_new(N_COMMA,nodes[right].type,left,right,0,0);
            else left=binary(op,left,right);
        }
    }
    return left;
}

/* 声明/初始化与语句部分接在同一个源片段中，后端逐函数消费完成的树。 */
/* 初始化树保留“目标符号 + 字节偏移”，不用伪造普通赋值去绕 const。
 * 聚合花括号先生成整对象清零，再覆盖显式元素；省略元素因而确实为零。
 * 不完整数组在见到最后一个元素后补大小，局部栈槽随后才统一分配。
 * 全局初始化也使用这棵树，但后端只接受编译期常量/地址重定位。
 */
static void chain(int *first,int *last,int item)
{
    if(!item) return;
    if(*last) nodes[*last].next=item;
    else *first=item;
    while(nodes[item].next) item=nodes[item].next;
    *last=item;
}
static int initialize(int type,int symbol,int offset,int global)
{
    Type *t=types+type;
    int first=0,last=0;
    if(t->kind==TY_ARRAY && types[t->base].kind==TY_CHAR && current_token()->k==T_STR) {
        int string=primary(),n=types[nodes[string].type].count;
        if(!t->count) { t->count=n;
            t->size=n*types[t->base].size; }
        if(t->count<n-1) fatal("string initializer exceeds array");
        int from=nodes[string].value;
        for(int i=0;i<t->count;i++) {
            int value=constant(i<n?data[from+i]:0,C_INT);
            chain(&first,&last,node_new(N_INIT,t->base,symbol,offset+i,value,0));
        }
    } else if(take('{')) {
        if(t->kind==TY_ARRAY || t->kind==TY_STRUCT || t->kind==TY_UNION) {
            chain(&first,&last,node_new(N_INIT,type,symbol,offset,0,1));
            int i=0,field=t->fields;
            while(!take('}')) {
                int element,at;
                if(t->kind==TY_ARRAY) {
                    if(t->count && i>=t->count) fatal("too many array initializers");
                    element=t->base;
                    at=offset+i*types[element].size;
                } else {
                    if(!field) fatal("too many aggregate initializers");
                    element=symbols[field].type;
                    at=offset+symbols[field].value;
                }
                chain(&first,&last,initialize(element,symbol,at,global));
                i++;
                if(t->kind!=TY_ARRAY) field=t->kind==TY_UNION?0:symbols[field].next;
                if(take('}')) break;
                need(',');
            }
            if(t->kind==TY_ARRAY && !t->count) {
                if(!i) fatal("empty initializer cannot infer array size");
                t->count=i;
                t->size=i*types[t->base].size;
                if(t->size<0 || t->size>0x3D0000) fatal("inferred array exceeds address space");
            }
        } else {
            chain(&first,&last,initialize(type,symbol,offset,global));
            take(',');
            need('}');
        }
    } else {
        if(t->kind==TY_ARRAY) fatal("array initializer needs braces or string");
        int value=expression(2);
        if((t->kind==TY_STRUCT || t->kind==TY_UNION) && nodes[value].type!=type) fatal("aggregate initializer type mismatch");
        chain(&first,&last,node_new(N_INIT,type,symbol,offset,value,0));
    }
    (void)global;
    return first;
}
static void scope_close(int depth)
{
    for(int i=1;i<symbol_count;i++) if(symbols[i].scope==depth) symbols[i].scope=-1;
}
static int declaration_ahead(void)
{
    return type_ahead() || word(current_token(),"static") || word(current_token(),"extern")
        || word(current_token(),"typedef") || word(current_token(),"register")
        || word(current_token(),"auto") || word(current_token(),"inline");
}
static int local_declaration(void)
{
    int storage=0,base=declaration_spec(&storage),first=0,last=0;
    if(base<0) fatal("local declaration needs a type");
    if(take(';')) return 0;
    do {
        int name=0,type=declarator(base,&name,0),old=find_symbol(name,0),s;
        if(old && symbols[old].scope==scope_depth) fatal("duplicate local name");
        if(storage&D_TYPEDEF) s=symbol_new(name,type,S_TYPEDEF,0,scope_depth);
        else if(types[type].kind==TY_FUNC || (storage&D_EXTERN)) {
            if(old && symbols[old].scope==0) s=old;
            else s=symbol_new(name,type,types[type].kind==TY_FUNC?S_FUNCTION:S_GLOBAL,0,0);
        } else s=symbol_new(name,type,(storage&D_STATIC)?S_GLOBAL:S_LOCAL,0,scope_depth);
        int init=0;
        if(take('=')) {
            if((storage&D_TYPEDEF) || (storage&D_EXTERN) || types[type].kind==TY_FUNC) fatal("invalid initializer on declaration");
            init=initialize(type,s,0,storage&D_STATIC);
        }
        if(symbols[s].kind==S_LOCAL) {
            if(!types[type].size) fatal("local object needs complete type");
            local_bytes=(local_bytes+types[type].size+3)/4*4;
            if(local_bytes>65536) fatal("local frame exceeds 64KB");
            symbols[s].value=-local_bytes;
            symbols[s].defined=1;
            chain(&first,&last,init);
        } else if((storage&D_STATIC) && !(storage&D_TYPEDEF)) define_global(s,init);
    } while(take(','));
    need(';');
    return first?node_new(N_BLOCK,C_VOID,first,0,0,0):0;
}
static int asm_operand_list(int output)
{
    int first=0,last=0;
    if(current_token()->k==':' || current_token()->k==')') return 0;
    do {
        if(current_token()->k!=T_STR) fatal("asm operand needs a constraint string");
        int constraint=current_token()->v;
        position++;
        need('(');
        int value=expression(1);
        need(')');
        if(output && !lvalue(value)) fatal("asm output must be an lvalue");
        chain(&first,&last,node_new(N_ARG,nodes[value].type,value,0,0,constraint));
    } while(take(','));
    return first;
}
static int asm_statement(void)
{
    take_word("volatile");
    take_word("__volatile__");
    need('(');
    if(current_token()->k!=T_STR) fatal("asm template needs a string");
    int text=primary(),outputs=0,inputs=0;
    if(take(':')) {
        outputs=asm_operand_list(1);
        if(take(':')) {
            inputs=asm_operand_list(0);
            if(take(':')) {
                while(current_token()->k!=')') {
                    if(current_token()->k!=T_STR) fatal("asm clobber needs a string");
                    /* 固定寄存器后端不跨表达式缓存值；memory/cc 仍解析，
                     * 并要求所有非保留寄存器更改显式写在模板或约束里。 */
                    position++;
                    if(!take(',')) break;
                }
            }
        }
    }
    need(')');
    need(';');
    return node_new(N_ASM,C_VOID,text,outputs,inputs,0);
}
static int statement(void)
{
    if(current_token()->k==T_ID && tokens[position+1].k==':') {
        int name=current_token()->v;
        position+=2;
        return node_new(N_LABEL,C_VOID,statement(),0,0,name);
    }
    if(take('{')) {
        scope_depth++;
        int first=0,last=0;
        while(!take('}')) {
            if(!current_token()->k) fatal("unfinished block");
            chain(&first,&last,declaration_ahead()?local_declaration():statement());
        }
        scope_close(scope_depth--);
        return node_new(N_BLOCK,C_VOID,first,0,0,0);
    }
    if(take_word("if")) {
        need('(');
        int test=expression(1);
        need(')');
        int yes=statement(),no=0;
        if(take_word("else")) no=statement();
        return node_new(N_IF,C_VOID,test,yes,no,0);
    }
    if(take_word("while")) {
        need('(');
        int test=expression(1);
        need(')');
        return node_new(N_WHILE,C_VOID,test,statement(),0,0);
    }
    if(take_word("do")) {
        int body=statement();
        if(!take_word("while")) fatal("do needs while");
        need('(');
        int test=expression(1);
        need(')');
        need(';');
        return node_new(N_DO,C_VOID,test,body,0,0);
    }
    if(take_word("for")) {
        need('(');
        scope_depth++;
        int init=0,test=0,step=0;
        if(declaration_ahead()) init=local_declaration();
        else if(!take(';')) { init=node_new(N_EXPR,C_VOID,expression(1),0,0,0);
            need(';'); }
        if(!take(';')) { test=expression(1);
            need(';'); }
        if(!take(')')) { step=expression(1);
            need(')'); }
        int loop=node_new(N_FOR,C_VOID,test,statement(),step,0),first=0,last=0;
        chain(&first,&last,init);
        chain(&first,&last,loop);
        scope_close(scope_depth--);
        return node_new(N_BLOCK,C_VOID,first,0,0,0);
    }
    if(take_word("switch")) {
        need('(');
        int test=expression(1);
        need(')');
        int previous=switch_node;
        int n=node_new(N_SWITCH,C_VOID,test,0,0,0);
        switch_node=n;
        nodes[n].b=statement();
        switch_node=previous;
        return n;
    }
    if(word(current_token(),"case") || word(current_token(),"default")) {
        if(!switch_node) fatal("case outside switch");
        int otherwise=take_word("default"),value=0;
        if(!otherwise) { position++;
            value=(int)constant_value(expression(2)); }
        need(':');
        for(int c=nodes[switch_node].c;c;c=nodes[c].b)
            if((otherwise && nodes[c].type) || (!otherwise && !nodes[c].type && nodes[c].value==value)) fatal("duplicate switch label");
        int n=node_new(N_CASE,otherwise,0,nodes[switch_node].c,0,value);
        nodes[switch_node].c=n;
        nodes[n].a=statement();
        return n;
    }
    if(take_word("return")) {
        int value=0;
        if(!take(';')) { value=expression(1);
            need(';'); }
        int result=types[symbols[function_symbol].type].base;
        if((types[result].kind==TY_VOID && value) || (types[result].kind!=TY_VOID && !value)) fatal("return value disagrees with function type");
        return node_new(N_RETURN,result,value,0,0,0);
    }
    if(take_word("break")) { need(';');
        return node_new(N_BREAK,C_VOID,0,0,0,0); }
    if(take_word("continue")) { need(';');
        return node_new(N_CONTINUE,C_VOID,0,0,0,0); }
    if(take_word("__asm__") || take_word("asm")) return asm_statement();
    if(take_word("goto")) {
        if(current_token()->k!=T_ID) fatal("goto needs a label name");
        int name=current_token()->v;
        position++;
        need(';');
        return node_new(N_GOTO,C_VOID,0,0,0,name);
    }
    if(take(';')) return 0;
    int n=node_new(N_EXPR,C_VOID,expression(1),0,0,0);
    need(';');
    return n;
}
static void compile_unit(void)
{
    type_count=0;
    symbol_count=param_count=node_count=1;
    position=0;
    new_type(TY_VOID,0,0,0,0,0);
    new_type(TY_CHAR,1,0,0,0,0);
    new_type(TY_CHAR,1,0,0,0,Q_UNSIGNED);
    new_type(TY_SHORT,2,0,0,0,0);
    new_type(TY_SHORT,2,0,0,0,Q_UNSIGNED);
    new_type(TY_INT,4,0,0,0,0);
    new_type(TY_INT,4,0,0,0,Q_UNSIGNED);
    /* 入口与最终 main 调用由后端 finish_image 统一生成，函数偏移从
     * 预留的 24 字节之后算起。绝对地址仍须等代码/数据布局确定后回填。 */
    code_used=24;
    copy(map_text,"SCSYM1MIO\n",sizeof(map_text));
    map_used=length(map_text);
    while(current_token()->k) {
        /* 顶层声明之间没有半写的输出文件，是检查取消/窗口重排的安全点。
         * total使用实际预处理标记数；每100ms最多重绘一次，避免每函数
         * 整帧提交拖慢自编译。旧生成顺序与重定位表完全不受窗口影响。 */
        if(build_view_poll("Parsing C and generating x86",position,token_count,0))fatal("compilation cancelled");
        int storage=0,base=declaration_spec(&storage);
        if(base<0) fatal("top-level declaration needs a type");
        if(take(';')) continue;
        int function_body=0;
        do {
            int name=0,type=declarator(base,&name,0),s=find_symbol(name,0);
            if(storage&D_TYPEDEF) {
                if(s && symbols[s].kind!=S_TYPEDEF) fatal("typedef conflicts with object");
                if(!s) s=symbol_new(name,type,S_TYPEDEF,0,0);
            } else {
                int kind=types[type].kind==TY_FUNC?S_FUNCTION:S_GLOBAL;
                if(s && symbols[s].kind!=kind) fatal("declaration kind conflicts with previous name");
                if(!s) s=symbol_new(name,type,kind,0,0);
                symbols[s].type=type;
                if(kind==S_FUNCTION && current_token()->k=='{') {
                    if(symbols[s].defined) fatal("function is already defined");
                    function_symbol=s;
                    scope_depth=1;
                    local_bytes=0;
                    node_count=1;
                    int index=0;
                    for(int p=types[type].fields;p;p=parameters[p].next) {
                        if(!parameters[p].name) fatal("function definition parameter needs a name");
                        symbol_new(parameters[p].name,parameters[p].type,S_PARAM,8+4*index++,1);
                    }
                    int body=statement();
                    symbols[s].defined=1;
                    emit_function(s,body);
                    function_body=1;
                    scope_close(1);
                    scope_depth=0;
                    function_symbol=0;
                    node_count=1;
                    break;
                }
                if(kind==S_GLOBAL) {
                    int init=0;
                    if(take('=')) init=initialize(type,s,0,1);
                    if(!(storage&D_EXTERN) || init) define_global(s,init);
                } else if(take('=')) fatal("function cannot have initializer");
            }
        } while(take(','));
        /* 函数体已经吃掉自己的 }，其后不能强制要求分号；普通声明必须。 */
        if(!function_body) need(';');
        node_count=1;
    }
}
