/* 标准SDK真正需要的语义集中在此，不在头里伪造bool/变参栈运算。
 * 它在基础parser之后定义，共同Node仍32B；聚合参数用连续4B槽。 */
static int standard_integer(int type)
{
    int kind=types[type].kind;
    return kind==TY_CHAR || kind==TY_SHORT || kind==TY_INT || kind==TY_BOOL;
}
static int standard_ice(int node,int depth)
{
    if(depth>64)fatal("constant expression nesting exceeds supported depth");
    Node *n=nodes+node;
    if(n->kind==N_CONST)return standard_integer(n->type);
    if(n->kind==N_CAST)return standard_integer(n->type) && standard_ice(n->a,depth+1);
    if(n->kind==N_UNARY)return standard_ice(n->a,depth+1);
    if(n->kind==N_BINARY){
        if(!standard_ice(n->a,depth+1))return 0;
        u32 left=constant_value(n->a);
        if((n->value==T_AND && !left) || (n->value==T_OR && left))return 1;
        return standard_ice(n->b,depth+1);
    }
    if(n->kind==N_COND)return standard_ice(n->a,depth+1) &&
        standard_ice(constant_value(n->a)?n->b:n->c,depth+1);
    return 0;
}
static int standard_null(int node)
{
    Node *n=nodes+node;
    if(types[n->type].kind==TY_PTR && types[types[n->type].base].kind==TY_VOID && n->kind==N_CAST)
        return standard_ice(n->a,0) && constant_value(n->a)==0;
    return standard_integer(n->type) && standard_ice(node,0) && constant_value(node)==0;
}
static int standard_value_conversion(int target,int value)
{
    int source=promoted(nodes[value].type);Type *to=types+target,*from=types+source;
    if(standard_integer(target)){
        if(!standard_integer(source) && !(to->kind==TY_BOOL && from->kind==TY_PTR))
            fatal("integer argument requires a scalar conversion, not an aggregate or pointer");
    }else if(to->kind==TY_PTR){
        if(!standard_null(value)){
            if(from->kind!=TY_PTR)fatal("pointer argument requires a compatible pointer or null constant");
            Type *a=types+to->base,*b=types+from->base;
            if(a->kind==TY_FUNC || b->kind==TY_FUNC){
                if(a->kind!=TY_FUNC || b->kind!=TY_FUNC || !sclc_type_same(to->base,from->base,0,0))
                    fatal("function pointer argument has an incompatible signature");
            }else{
                if((b->flags&(Q_CONST|Q_VOLATILE))&~a->flags)
                    fatal("pointer argument discards pointed-to qualifiers");
                if(a->kind!=TY_VOID && b->kind!=TY_VOID && !sclc_type_same(to->base,from->base,0,1))
                    fatal("pointer argument has an incompatible object type");
            }
        }
    }else if(abi_aggregate(target)){
        abi_stack_bytes(target);
        if(to->kind!=from->kind || aggregate_identity[target]!=aggregate_identity[source])
            fatal("aggregate value requires the same complete aggregate type");
        abi_stack_bytes(source);
        return value;
    }else fatal("unsupported argument value type");
    if(target==nodes[value].type && to->kind!=TY_BOOL)return value;
    return node_new(N_CAST,target,value,0,0,0);
}
static void standard_bool_source(int value)
{
    int type=promoted(nodes[value].type);
    if(!standard_integer(type) && types[type].kind!=TY_PTR)
        fatal("bool conversion requires an integer or pointer scalar");
}
static int standard_conditional_type(int yes,int no)
{
    int left=promoted(nodes[yes].type),right=promoted(nodes[no].type);
    Type *a=types+left,*b=types+right;
    if(standard_integer(left) && standard_integer(right))
        return ((a->flags|b->flags)&Q_UNSIGNED)?C_UINT:C_INT;
    if(a->kind==TY_PTR && standard_null(no))return left;
    if(b->kind==TY_PTR && standard_null(yes))return right;
    if(a->kind==TY_PTR && b->kind==TY_PTR){
        Type *x=types+a->base,*y=types+b->base;
        int qualifiers=(x->flags|y->flags)&(Q_CONST|Q_VOLATILE),base;
        if(x->kind==TY_FUNC || y->kind==TY_FUNC){
            if(x->kind!=TY_FUNC || y->kind!=TY_FUNC || !sclc_type_same(a->base,b->base,0,0))
                fatal("conditional function pointers have incompatible signatures");
            base=a->base;
        }else if(x->kind==TY_VOID || y->kind==TY_VOID)base=C_VOID;
        else{
            if(!sclc_type_same(a->base,b->base,0,1))fatal("conditional object pointers have incompatible types");
            base=a->base;
        }
        return pointer_to(qualified(base,qualifiers));
    }
    if(a->kind==TY_VOID && b->kind==TY_VOID)return C_VOID;
    if((a->kind==TY_STRUCT || a->kind==TY_UNION) && a->kind==b->kind &&
        aggregate_identity[left]==aggregate_identity[right])return left;
    fatal("conditional branches do not have a common value type");
    return C_VOID;
}
static int standard_default_conversion(int value)
{
    int target=promoted(nodes[value].type);
    if(abi_aggregate(target)){abi_stack_bytes(target);return value;}
    if(!standard_integer(target) && types[target].kind!=TY_PTR)
        fatal("unnamed argument requires an integer, pointer or complete aggregate");
    return target==nodes[value].type?value:node_new(N_CAST,target,value,0,0,0);
}
static int standard_va_list(int node,int writable)
{
    Type *t=types+nodes[node].type;
    return (!writable || (lvalue(node) && !(t->flags&Q_CONST))) && t->kind==TY_PTR &&
        types[t->base].kind==TY_CHAR && types[t->base].flags==Q_UNSIGNED;
}
static int standard_type_name(void)
{
    int storage=0,name=0,type=declaration_spec(&storage);
    if(type<0 || storage)fatal("builtin requires an object type-name");
    type=declarator(type,&name,1);
    if(name)fatal("type-name cannot declare an identifier");
    return type;
}
static int standard_offsetof(void)
{
    int type=standard_type_name();need(',');u32 offset=0;
    for(;;){
        if(types[type].kind!=TY_STRUCT && types[type].kind!=TY_UNION)
            fatal("offsetof member requires a complete aggregate");
        if(!types[type].size || current_token()->k!=T_ID)fatal("offsetof needs a member name");
        int field=types[type].fields;
        while(field && symbols[field].name!=current_token()->v)field=symbols[field].next;
        if(!field)fatal("offsetof member does not exist");
        if((u32)symbols[field].value>0xffffffffu-offset)fatal("offsetof exceeds the address domain");
        offset+=(u32)symbols[field].value;type=symbols[field].type;position++;
        while(take('[')){
            if(types[type].kind!=TY_ARRAY || !types[type].count)fatal("offsetof index needs a complete array");
            int index=expression(2);need(']');
            if(!standard_ice(index,0))fatal("offsetof array index is not an integer constant");
            u32 number=constant_value(index),stride=(u32)types[types[type].base].size;
            if(number>=(u32)types[type].count || !stride || number>(0xffffffffu-offset)/stride)
                fatal("offsetof array index exceeds its object");
            offset+=number*stride;type=types[type].base;
        }
        if(take(')'))break;
        need('.');
    }
    return constant(offset,C_UINT);
}
static int standard_builtin(void)
{
    Tok *token=current_token();int kind=-1;
    if(word(token,"__func__")){
        if(!function_symbol)fatal("__func__ requires a function body");
        position++;
        if(function_name_literal<0){
            const char *name=spelling(symbols[function_symbol].name);int bytes=length(name)+1;
            if(bytes>DATA_MAX-data_used)fatal("function name exceeds initialized data capacity");
            function_name_literal=data_used;function_name_bytes=bytes;
            for(int i=0;i<bytes;i++)data[data_used++]=(u8)name[i];
            if(shared_output)scl_literal_new(function_name_literal,bytes);
        }
        return node_new(N_STRING,array_of(qualified(C_CHAR,Q_CONST),function_name_bytes),0,0,0,function_name_literal);
    }
    if(word(token,"__builtin_offsetof")){position++;need('(');return standard_offsetof();}
    if(word(token,"__builtin_va_start"))kind=N_VA_START;
    else if(word(token,"__builtin_va_arg"))kind=N_VA_ARG;
    else if(word(token,"__builtin_va_copy"))kind=N_VA_COPY;
    else if(word(token,"__builtin_va_end"))kind=N_VA_END;
    if(kind<0)return 0;
    position++;need('(');int ap=expression(2),value=0,type=C_VOID,where=0;
    if(!standard_va_list(ap,1))fatal("varargs cursor must be a writable va_list lvalue");
    if(kind==N_VA_START){
        need(',');
        if(!function_symbol || !types[symbols[function_symbol].type].flags)
            fatal("va_start requires a variadic function definition");
        int parameter=types[symbols[function_symbol].type].fields;
        if(!parameter)fatal("va_start requires a named parameter");
        while(parameters[parameter].next)parameter=parameters[parameter].next;
        if(current_token()->k!=T_ID || current_token()->v!=parameters[parameter].name)
            fatal("va_start requires the rightmost named parameter");
        int symbol=find_symbol(current_token()->v,0);
        if(!symbol || symbols[symbol].kind!=S_PARAM)fatal("va_start parameter is shadowed or not a real parameter");
        where=symbols[symbol].value+abi_stack_bytes(symbols[symbol].type);position++;
    }else if(kind==N_VA_ARG){
        need(',');type=standard_type_name();
        if(abi_aggregate(type))abi_stack_bytes(type);
        else if(types[type].size!=4 || (types[type].kind!=TY_INT && types[type].kind!=TY_PTR))
            fatal("va_arg supports promoted 32-bit scalars and complete aggregates in this profile");
    }else if(kind==N_VA_COPY){
        need(',');value=expression(2);
        if(!standard_va_list(value,0))fatal("va_copy source must have va_list type");
    }
    need(')');return node_new(kind,type,ap,value,0,where);
}
static int standard_undeclared(int name)
{
    const char *functions[]={"malloc","calloc","realloc","free","memcpy","memmove","memset","memcmp",
        "strlen","strcmp","strncmp","strcpy","strncpy","strcat","strncat","strchr","strrchr","strstr",
        "strspn","strcspn","strpbrk","strtok","fopen","fclose","fread","fwrite","fseek","ftell","fflush",
        "printf","fprintf","sprintf","snprintf","vprintf","vfprintf","vsprintf","vsnprintf","atoi",
        "atol","strtol","strtoul","qsort","bsearch","abs","labs","atexit","exit","abort","getenv",
        "fgetc","fputc","fgets","fputs","getc","putc","getchar","putchar","puts","ungetc","rewind",
        "feof","ferror","clearerr","setbuf","setvbuf","remove","rename","perror","strerror",
        "isalnum","isalpha","iscntrl","isdigit","isgraph","islower","isprint","ispunct","isspace",
        "isupper","isxdigit","tolower","toupper","rand","srand","_Exit","fgetpos","fsetpos",
        "isblank","memchr","strcoll","strxfrm"};
    for(int i=0;i<(int)(sizeof(functions)/sizeof(functions[0]));i++)
        if(equal(spelling(name),functions[i]))return 1;
    return standard_root_name(name)!=0;
}
static int standard_root_name(int name)
{
    if(equal(spelling(name),"__scl_root_run"))return 1;
    if(equal(spelling(name),"__scl_root_unwind"))return 2;
    return 0;
}
static void standard_root_declaration(int symbol,int type,int kind)
{
    Type *function=types+type;
    if(function->kind!=TY_FUNC || function->flags || function->count!=(kind==1?4:1) ||
        types[function->base].kind!=(kind==1?TY_INT:TY_VOID))fatal("root adapter has an invalid prototype");
    int p=function->fields;
    if(!p || types[parameters[p].type].kind!=TY_PTR)fatal("root adapter requires its private jump pointer");
    Type *jump=types+types[parameters[p].type].base;
    if(jump->kind!=TY_STRUCT || jump->size!=48 || jump->flags)
        fatal("root adapter jump layout must be mutable and 48 bytes");
    int field=jump->fields;
    for(int index=0;index<12;index++){
        if(!field || symbols[field].value!=index*4 || types[symbols[field].type].kind!=TY_INT ||
            types[symbols[field].type].size!=4 || types[symbols[field].type].flags!=Q_UNSIGNED)
            fatal("root adapter requires twelve consecutive mutable unsigned words");
        field=symbols[field].next;
    }
    if(field)fatal("root adapter has unexpected extra jump members");
    if(kind==1){
        p=parameters[p].next;
        for(int index=0;index<2;index++){
            int pointer=parameters[p].type;
            if(types[pointer].kind!=TY_PTR || types[types[pointer].base].kind!=TY_FUNC)
                fatal("root adapter entry and finish must be function pointers");
            Type *callback=types+types[pointer].base;
            if(callback->flags || callback->count!=index || types[callback->base].kind!=TY_INT)
                fatal("root adapter callback has an invalid signature");
            if(index){int context=parameters[callback->fields].type;
                if(types[context].kind!=TY_PTR || types[types[context].base].kind!=TY_VOID)
                    fatal("root finisher context must be void pointer");}
            p=parameters[p].next;
        }
        int context=parameters[p].type;
        if(types[context].kind!=TY_PTR || types[types[context].base].kind!=TY_VOID)
            fatal("root adapter context must be void pointer");
    }
    root_adapter_symbols[kind-1]=symbol;
}
