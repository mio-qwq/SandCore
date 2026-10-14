/* mio：SCL 是另一种最终封装，不给旧 SCX/SKM 地址回填增加隐藏语义。
 * 这里的清单、源数据区间与排序工作区按实际需要从原有用户堆申请；
 * 只读重排保留来源范围，不能用一个“最后数据偏移”猜混合段的地址。 */
#include "runtime/SCLFMT.H"

typedef struct { int name; u32 major,minor,version; int string_offset; } SclCDependency;
typedef struct { int name,kind,symbol,string_offset; } SclCExport;
typedef struct { int name,kind,dependency,symbol,index,used,string_offset; u32 size; } SclCImport;
typedef struct { int offset,size,access,alignment,rva; } SclCSpan;
typedef struct { u32 patch,type,target_kind,target,addend; } SclCFix;
typedef struct { u32 rva,memory,file_offset,file_size,access; } SclCSegment;

static SclCDependency *sclc_dependencies;
static SclCExport *sclc_exports;
static SclCImport *sclc_imports;
static SclCSpan *sclc_spans;
static SclCFix *sclc_fixes;
static int sclc_dep_count,sclc_dep_capacity,sclc_export_count,sclc_export_capacity;
static int sclc_import_count,sclc_import_capacity,sclc_span_count,sclc_span_capacity;
static int sclc_fix_count,sclc_fix_capacity;
static int sclc_library,sclc_init,sclc_fini;
static u32 sclc_major,sclc_minor,sclc_version;
static char *sclc_json;
static int sclc_at,sclc_json_size;
static int sclc_ro_rva,sclc_rw_rva,sclc_bss_rva;

static int scl_path_same(const char *a,const char *b)
{
    /* 规范名rootless，固定日志回退可以含根斜杠；二者是同一身份。
     * 这里只比较已经解析的名字，不再调用RESOLVE叠加cwd。 */
    if(*a=='/')a++;
    if(*b=='/')b++;
    for(;;){int x=(u8)*a++,y=(u8)*b++;
        if(x>='a' && x<='z')x-=32;
        if(y>='a' && y<='z')y-=32;
        if(x!=y)return 0;
        if(!x)return 1;}
}

static void *sclc_grow(void *previous,int used,int *capacity,int width)
{
    if(used<*capacity)return previous;
    if(*capacity>0x3fffffff/width)fatal("SCL work table capacity overflow");
    int next=*capacity?*capacity*2:8;
    if(next>0x7fffffff/width)fatal("SCL work table byte size overflow");
    u8 *memory=sc_alloc((u32)(next*width));
    if(!memory)fatal("SCL work table allocation failed");
    u8 *old=previous;
    for(int i=0;i<used*width;i++)memory[i]=old[i];
    if(previous)sc_free(previous);
    *capacity=next;
    return memory;
}
static int sclc_compare_name(int a,int b)
{
    u8 *x=(u8 *)spelling(a),*y=(u8 *)spelling(b);
    while(*x && *x==*y){x++;y++;}
    return (int)*x-(int)*y;
}
static int sclc_library_name(int name)
{
    char *p=spelling(name);int n=length(p);
    if(n<5 || n>35 || !equal(p+n-4,".SCL"))return 0;
    for(int i=0;i<n-4;i++)if(!((p[i]>='A' && p[i]<='Z') || (p[i]>='0' && p[i]<='9') || p[i]=='_'))return 0;
    return 1;
}
static int sclc_symbol_name(int name)
{
    char *p=spelling(name);int n=length(p);
    if(!n || n>127)return 0;
    for(int i=0;i<n;i++)if(!((p[i]>='a' && p[i]<='z') || (p[i]>='A' && p[i]<='Z') || p[i]=='_' || (i && p[i]>='0' && p[i]<='9')))return 0;
    return 1;
}
static void sclc_space(void)
{
    while(sclc_at<sclc_json_size && (sclc_json[sclc_at]==' ' || sclc_json[sclc_at]=='\t' || sclc_json[sclc_at]=='\r' || sclc_json[sclc_at]=='\n'))sclc_at++;
}
static int sclc_take(int c)
{
    sclc_space();
    if(sclc_at>=sclc_json_size || sclc_json[sclc_at]!=c)return 0;
    sclc_at++;return 1;
}
static void sclc_need(int c)
{ if(!sclc_take(c))fatal("SCL manifest has invalid JSON punctuation"); }
static int sclc_string(void)
{
    char text[128];int n=0,ended=0;
    sclc_need('"');
    while(sclc_at<sclc_json_size){
        int c=(u8)sclc_json[sclc_at++];
        if(c=='"'){ended=1;break;}
        if(c=='\\'){
            if(sclc_at>=sclc_json_size)fatal("SCL manifest has an unfinished escape");
            c=(u8)sclc_json[sclc_at++];
            if(c=='u'){
                int value=0;
                for(int i=0;i<4;i++){
                    if(sclc_at>=sclc_json_size)fatal("SCL manifest has a short Unicode escape");
                    int h=hexvalue((u8)sclc_json[sclc_at++]);
                    if(h<0)fatal("SCL manifest has an invalid Unicode escape");
                    value=value*16+h;
                }
                c=value;
            }else if(c=='b')c=8;
            else if(c=='f')c=12;
            else if(c=='n')c=10;
            else if(c=='r')c=13;
            else if(c=='t')c=9;
            else if(c!='"' && c!='\\' && c!='/')fatal("SCL manifest has an unknown escape");
        }
        if(c<=0 || c>=128 || n>=127)fatal("SCL manifest strings must be non-NUL ASCII within 127 bytes");
        if(c<32)fatal("SCL manifest names cannot contain control characters");
        text[n++]=(char)c;
    }
    if(!ended)fatal("SCL manifest has an unterminated string");
    return intern(text,n);
}
static u32 sclc_number(void)
{
    sclc_space();u32 value=0;int start=sclc_at;
    while(sclc_at<sclc_json_size && sclc_json[sclc_at]>='0' && sclc_json[sclc_at]<='9'){
        int digit=sclc_json[sclc_at++]-'0';
        if(value>(0xffffffffu-(u32)digit)/10u)fatal("SCL manifest integer exceeds u32");
        value=value*10u+(u32)digit;
    }
    if(start==sclc_at || (sclc_at-start>1 && sclc_json[start]=='0'))fatal("SCL manifest requires canonical unsigned decimal integers");
    if(sclc_at<sclc_json_size && sclc_json[sclc_at]!=' ' && sclc_json[sclc_at]!='\t' && sclc_json[sclc_at]!='\r' && sclc_json[sclc_at]!='\n'
       && sclc_json[sclc_at]!=',' && sclc_json[sclc_at]!=']' && sclc_json[sclc_at]!='}')fatal("SCL manifest integer suffix is unsupported");
    return value;
}
static int sclc_nullable_name(void)
{
    sclc_space();
    if(sclc_at+4<=sclc_json_size && sclc_json[sclc_at]=='n' && sclc_json[sclc_at+1]=='u' && sclc_json[sclc_at+2]=='l' && sclc_json[sclc_at+3]=='l'){
        sclc_at+=4;return 0;
    }
    return sclc_string();
}
static int sclc_kind(void)
{
    int name=sclc_string();
    if(equal(spelling(name),"function"))return SCL_SYMBOL_FUNCTION;
    if(equal(spelling(name),"data"))return SCL_SYMBOL_DATA;
    fatal("SCL symbol kind must be function or data");return 0;
}
static void sclc_member_seen(int *seen,int bit)
{
    if(*seen&bit)fatal("SCL manifest contains a duplicate JSON key");
    *seen|=bit;
}
static void sclc_read_dependencies(void)
{
    sclc_need('[');if(sclc_take(']'))return;
    do{
        sclc_dependencies=sclc_grow(sclc_dependencies,sclc_dep_count,&sclc_dep_capacity,sizeof(SclCDependency));
        SclCDependency *d=sclc_dependencies+sclc_dep_count++;int seen=0;
        d->name=0;d->major=d->minor=d->version=0;d->string_offset=0;
        sclc_need('{');
        if(!sclc_take('}'))do{
            int field=sclc_string(),bit=0;sclc_need(':');
            if(equal(spelling(field),"name")){bit=1;sclc_member_seen(&seen,bit);d->name=sclc_string();}
            else if(equal(spelling(field),"abi_major")){bit=2;sclc_member_seen(&seen,bit);d->major=sclc_number();}
            else if(equal(spelling(field),"min_minor")){bit=4;sclc_member_seen(&seen,bit);d->minor=sclc_number();}
            else if(equal(spelling(field),"min_version")){bit=8;sclc_member_seen(&seen,bit);d->version=sclc_number();}
            else fatal("SCL manifest dependency has an unknown field");
            if(sclc_take('}'))break;
            sclc_need(',');
        }while(1);
        if(seen!=15 || !d->major || !sclc_library_name(d->name))fatal("SCL dependency requires name, ABI and minimum versions");
    }while(sclc_take(','));
    sclc_need(']');
}
static void sclc_read_exports(void)
{
    sclc_need('[');if(sclc_take(']'))return;
    do{
        sclc_exports=sclc_grow(sclc_exports,sclc_export_count,&sclc_export_capacity,sizeof(SclCExport));
        SclCExport *e=sclc_exports+sclc_export_count++;int seen=0;
        e->name=e->kind=e->symbol=e->string_offset=0;sclc_need('{');
        if(!sclc_take('}'))do{
            int field=sclc_string();sclc_need(':');
            if(equal(spelling(field),"name")){sclc_member_seen(&seen,1);e->name=sclc_string();}
            else if(equal(spelling(field),"kind")){sclc_member_seen(&seen,2);e->kind=sclc_kind();}
            else fatal("SCL manifest export has an unknown field");
            if(sclc_take('}'))break;
            sclc_need(',');
        }while(1);
        if(seen!=3 || !sclc_symbol_name(e->name))fatal("SCL export requires name and kind");
    }while(sclc_take(','));
    sclc_need(']');
}
static void sclc_read_imports(void)
{
    sclc_need('[');if(sclc_take(']'))return;
    do{
        sclc_imports=sclc_grow(sclc_imports,sclc_import_count,&sclc_import_capacity,sizeof(SclCImport));
        SclCImport *e=sclc_imports+sclc_import_count++;int seen=0;
        e->name=e->kind=e->dependency=e->symbol=e->index=e->used=e->string_offset=0;e->size=0;sclc_need('{');
        if(!sclc_take('}'))do{
            int field=sclc_string();sclc_need(':');
            if(equal(spelling(field),"name")){sclc_member_seen(&seen,1);e->name=sclc_string();}
            else if(equal(spelling(field),"kind")){sclc_member_seen(&seen,2);e->kind=sclc_kind();}
            else if(equal(spelling(field),"dependency")){sclc_member_seen(&seen,4);e->dependency=sclc_string();}
            else if(equal(spelling(field),"size")){sclc_member_seen(&seen,8);e->size=sclc_number();}
            else fatal("SCL manifest import has an unknown field");
            if(sclc_take('}'))break;
            sclc_need(',');
        }while(1);
        if(seen!=15 || !e->size || !sclc_symbol_name(e->name) || !sclc_library_name(e->dependency))fatal("SCL import requires name, kind, dependency and minimum size");
    }while(sclc_take(','));
    sclc_need(']');
}
static void scl_manifest_load(const char *path,const char *output)
{
    u32 info[2];
    if(sc_stat(path,info) || info[0]!=1 || !info[1] || info[1]>1048576u)fatal("SCL manifest missing or exceeds 1 MiB");
    sclc_json=sc_alloc(info[1]+1u);if(!sclc_json)fatal("SCL manifest allocation failed");
    if(sc_read(path,sclc_json,(int)info[1])!=(int)info[1])fatal("SCL manifest read failed");
    sclc_json_size=(int)info[1];sclc_at=0;sclc_json[sclc_json_size]=0;
    for(int i=0;i<sclc_json_size;i++)if(!sclc_json[i])fatal("SCL manifest contains NUL");
    int seen=0;sclc_need('{');
    if(!sclc_take('}'))do{
        int field=sclc_string();sclc_need(':');
        if(equal(spelling(field),"name")){sclc_member_seen(&seen,1);sclc_library=sclc_string();}
        else if(equal(spelling(field),"abi_major")){sclc_member_seen(&seen,2);sclc_major=sclc_number();}
        else if(equal(spelling(field),"abi_minor")){sclc_member_seen(&seen,4);sclc_minor=sclc_number();}
        else if(equal(spelling(field),"library_version")){sclc_member_seen(&seen,8);sclc_version=sclc_number();}
        else if(equal(spelling(field),"exports")){sclc_member_seen(&seen,16);sclc_read_exports();}
        else if(equal(spelling(field),"imports")){sclc_member_seen(&seen,32);sclc_read_imports();}
        else if(equal(spelling(field),"dependencies")){sclc_member_seen(&seen,64);sclc_read_dependencies();}
        else if(equal(spelling(field),"init")){sclc_member_seen(&seen,128);sclc_init=sclc_nullable_name();}
        else if(equal(spelling(field),"fini")){sclc_member_seen(&seen,256);sclc_fini=sclc_nullable_name();}
        else fatal("SCL manifest has an unknown top-level field");
        if(sclc_take('}'))break;
        sclc_need(',');
    }while(1);
    sclc_space();
    if(seen!=511 || sclc_at!=sclc_json_size || !sclc_major || !sclc_library_name(sclc_library))fatal("SCL manifest requires exactly nine fields and a supported ABI");
    if((sclc_init && !sclc_symbol_name(sclc_init)) || (sclc_fini && !sclc_symbol_name(sclc_fini)))fatal("SCL callback name is invalid");
    const char *base=output;
    for(const char *p=output;*p;p++)if(*p=='/')base=p+1;
    if(!equal(base,spelling(sclc_library)))fatal("SCL output basename differs from manifest name");
    sc_free(sclc_json);sclc_json=0;
}

static int scl_object_const(int type)
{
    while(types[type].kind==TY_ARRAY){if(types[type].flags&Q_CONST)return 1;type=types[type].base;}
    return (types[type].flags&Q_CONST)!=0;
}
static int sclc_type_same(int a,int b,int depth,int parameter)
{
    if(a==b)return 1;
    if(depth>32)fatal("SCL declaration type nesting exceeds supported depth");
    Type *x=types+a,*y=types+b;
    int mask=parameter?~(Q_CONST|Q_VOLATILE):~0;
    if(x->kind!=y->kind || (x->flags&mask)!=(y->flags&mask))return 0;
    if(x->kind==TY_PTR)return sclc_type_same(x->base,y->base,depth+1,0);
    if(x->kind==TY_ARRAY)return (!x->count || !y->count || x->count==y->count) && sclc_type_same(x->base,y->base,depth+1,0);
    if(x->kind==TY_STRUCT || x->kind==TY_UNION)return x->fields==y->fields && x->size==y->size;
    if(x->kind==TY_FUNC){
        if(!sclc_type_same(x->base,y->base,depth+1,0))return 0;
        if(x->count<0 || y->count<0)return 1;
        if(x->count!=y->count)return 0;
        int left=x->fields,right=y->fields;
        /* C函数类型忽略形参最外层const/volatile；指向对象的限定
         * 仍在递归中严格比较，不能把 const int * 当作 int *。 */
        while(left && right){if(!sclc_type_same(parameters[left].type,parameters[right].type,depth+1,1))return 0;
            left=parameters[left].next;right=parameters[right].next;}
        return !left && !right;
    }
    return x->size==y->size;
}
static int scl_declaration_type(int previous,int type)
{
    if(!sclc_type_same(previous,type,0,0))fatal("SCL redeclaration has an incompatible C type");
    /* 完整定义后的 extern a[] 不能抹掉已确定的对象尺寸；反过来
     * a[] 后的完整定义取新类型。函数也保留已有的完整参数原型。 */
    if(types[previous].kind==TY_ARRAY && types[previous].count && !types[type].count)return previous;
    if(types[previous].kind==TY_FUNC && types[previous].count>=0 && types[type].count<0)return previous;
    return type;
}
static void scl_declare(int symbol,int storage)
{
    int old=scl_symbol_storage[symbol];
    int local=symbols[symbol].scope!=0;
    if((old&1) && !(old&2) && (storage&D_STATIC))fatal("SCL declaration changes external linkage to static");
    scl_symbol_storage[symbol]=old|1|(((storage&D_STATIC) || local)?2:0);
}
static void scl_literal_new(int offset,int size)
{
    sclc_spans=sclc_grow(sclc_spans,sclc_span_count,&sclc_span_capacity,sizeof(SclCSpan));
    SclCSpan *s=sclc_spans+sclc_span_count++;
    s->offset=offset;s->size=size;s->access=SCL_SEG_R;s->alignment=1;s->rva=0;
}
static void scl_global_new(int symbol,int size)
{
    scl_symbol_sizes[symbol]=size;
    if(!symbols[symbol].aux)return;
    sclc_spans=sclc_grow(sclc_spans,sclc_span_count,&sclc_span_capacity,sizeof(SclCSpan));
    SclCSpan *s=sclc_spans+sclc_span_count++;
    s->offset=symbols[symbol].value;s->size=size;
    s->access=scl_object_const(symbols[symbol].type)?SCL_SEG_R:SCL_SEG_R|SCL_SEG_W;
    s->alignment=alignment(symbols[symbol].type);s->rva=0;
}

static int sclc_dep_compare(u8 *a,u8 *b)
{ return sclc_compare_name(((SclCDependency *)a)->name,((SclCDependency *)b)->name); }
static int sclc_export_compare(u8 *a,u8 *b)
{ return sclc_compare_name(((SclCExport *)a)->name,((SclCExport *)b)->name); }
static int sclc_import_compare(u8 *a,u8 *b)
{
    SclCImport *x=(SclCImport *)a,*y=(SclCImport *)b;
    if(x->index!=y->index)return x->index<y->index?-1:1;
    return sclc_compare_name(x->name,y->name);
}
static int sclc_fix_compare(u8 *a,u8 *b)
{
    u32 x=((SclCFix *)a)->patch,y=((SclCFix *)b)->patch;
    return x<y?-1:x!=y;
}
static void sclc_sort(u8 *entries,int count,int width,int (*compare)(u8 *,u8 *))
{
    if(count<2)return;
    if(count>0x7fffffff/width)fatal("SCL sort workspace overflow");
    u8 *work=sc_alloc((u32)(count*width));if(!work)fatal("SCL sort workspace allocation failed");
    for(int run=1;run<count;){
        for(int start=0;start<count;){
            int middle=run<count-start?start+run:count;
            int end=run<count-middle?middle+run:count;
            int left=start,right=middle,out=start;
            while(out<end){
                int source;
                if(right==end || (left<middle && compare(entries+left*width,entries+right*width)<=0))source=left++;
                else source=right++;
                for(int k=0;k<width;k++)work[out*width+k]=entries[source*width+k];
                out++;
            }
            start=end;
        }
        for(int i=0;i<count*width;i++)entries[i]=work[i];
        if(run>count/2)break;
        run*=2;
    }
    sc_free(work);
}
static int sclc_find_dependency(int name)
{
    int first=0,count=sclc_dep_count;
    while(count){int half=count/2,index=first+half;int order=sclc_compare_name(sclc_dependencies[index].name,name);
        if(!order)return index;
        if(order<0){first=index+1;count-=half+1;}else count=half;}
    return -1;
}
static int sclc_source_rva(int offset,int width)
{
    int first=0,count=sclc_span_count;
    while(count){int half=count/2,index=first+half;SclCSpan *s=sclc_spans+index;
        if(offset<s->offset)count=half;
        else if(offset-s->offset>=s->size){first=index+1;count-=half+1;}
        else{if(width>s->size-(offset-s->offset))fatal("SCL data range crosses a source span");return s->rva+offset-s->offset;}}
    fatal("SCL data offset has no retained source span");return 0;
}
static u32 sclc_symbol_rva(int symbol)
{
    Symbol *s=symbols+symbol;
    if(!s->defined)fatal("SCL internal target is undefined");
    if(s->kind==S_FUNCTION)return (u32)s->value;
    if(s->kind!=S_GLOBAL)fatal("SCL target is not a global object or function");
    if(s->aux)return (u32)sclc_source_rva(s->value,scl_symbol_sizes[symbol]);
    return (u32)(sclc_bss_rva+s->value);
}
static u32 sclc_callback(int name,int finalizing)
{
    if(!name)return SCL_NO_ENTRY;
    int symbol=find_symbol(name,0);
    if(!symbol || symbols[symbol].scope!=0 || symbols[symbol].kind!=S_FUNCTION || !symbols[symbol].defined || (scl_symbol_storage[symbol]&2))fatal("SCL callback must be a defined external function");
    Type *type=types+symbols[symbol].type;
    int result=types[type->base].kind;
    if(type->count!=0 || type->flags || result!=(finalizing?TY_VOID:TY_INT))fatal("SCL callback must be int(void) init or void(void) fini");
    if(!finalizing && (types[type->base].flags&Q_UNSIGNED))fatal("SCL init must return signed int");
    if(!scl_symbol_sizes[symbol])fatal("SCL callback has no machine code");
    return sclc_symbol_rva(symbol);
}

/* 清单解析完成后再解析C名字与生成表：公共头里的未引用extern声明
 * 不制造导入，真实回填引用必须有清单记录，清单记录又必须真实使用。 */
static int *sclc_resolve(void)
{
    sclc_sort((u8 *)sclc_dependencies,sclc_dep_count,sizeof(SclCDependency),sclc_dep_compare);
    for(int i=0;i<sclc_dep_count;i++){
        if(sclc_dependencies[i].name==sclc_library || (i && !sclc_compare_name(sclc_dependencies[i-1].name,sclc_dependencies[i].name)))fatal("SCL duplicate or self dependency");
    }
    sclc_sort((u8 *)sclc_exports,sclc_export_count,sizeof(SclCExport),sclc_export_compare);
    for(int i=0;i<sclc_export_count;i++){
        SclCExport *e=sclc_exports+i;
        if(i && !sclc_compare_name(sclc_exports[i-1].name,e->name))fatal("SCL duplicate export");
        e->symbol=find_symbol(e->name,0);
        if(!e->symbol || symbols[e->symbol].scope!=0 || !symbols[e->symbol].defined || (scl_symbol_storage[e->symbol]&2))fatal("SCL export is missing or has static linkage");
        int kind=symbols[e->symbol].kind;
        if((e->kind==SCL_SYMBOL_FUNCTION && kind!=S_FUNCTION) || (e->kind==SCL_SYMBOL_DATA && kind!=S_GLOBAL))fatal("SCL export kind differs from C declaration");
    }
    int *lookup=sc_alloc((u32)(symbol_count*sizeof(int)));if(!lookup)fatal("SCL import lookup allocation failed");
    for(int i=0;i<symbol_count;i++)lookup[i]=-1;
    for(int i=0;i<sclc_import_count;i++){
        SclCImport *e=sclc_imports+i;e->index=sclc_find_dependency(e->dependency);
        if(e->index<0)fatal("SCL import has no declared dependency");
        e->symbol=find_symbol(e->name,0);
        if(!e->symbol || symbols[e->symbol].scope!=0 || symbols[e->symbol].defined || (scl_symbol_storage[e->symbol]&2))fatal("SCL import must reference a declared undefined external");
        if(lookup[e->symbol]>=0)fatal("SCL duplicate import symbol");
        lookup[e->symbol]=i;
        Symbol *s=symbols+e->symbol;Type *t=types+s->type;
        if(e->kind==SCL_SYMBOL_FUNCTION){if(s->kind!=S_FUNCTION || t->kind!=TY_FUNC || t->count<0)fatal("SCL function import needs a complete function prototype");}
        else{
            int minimum=t->size;
            if(t->kind==TY_ARRAY && !t->count)minimum=types[t->base].size;
            if(s->kind!=S_GLOBAL || !minimum || e->size<(u32)minimum)fatal("SCL data import minimum size is smaller than its C object or array element");
        }
    }
    sclc_sort((u8 *)sclc_imports,sclc_import_count,sizeof(SclCImport),sclc_import_compare);
    for(int i=0;i<sclc_import_count;i++)lookup[sclc_imports[i].symbol]=i;
    return lookup;
}

static int sclc_segments(SclCSegment *segments)
{
    int count=0;u32 rva=0;
    if(code_used){SclCSegment *s=segments+count++;s->rva=0;s->memory=((u32)code_used+4095u)&~4095u;s->file_size=(u32)code_used;s->access=SCL_SEG_R|SCL_SEG_X;rva=s->memory;}
    for(int group=0;group<2;group++){
        int access=group?SCL_SEG_R|SCL_SEG_W:SCL_SEG_R;u32 used=0;
        if(group)sclc_rw_rva=(int)rva;else sclc_ro_rva=(int)rva;
        for(int i=0;i<sclc_span_count;i++){
            SclCSpan *s=sclc_spans+i;
            if(i && s->offset<sclc_spans[i-1].offset+sclc_spans[i-1].size)fatal("SCL source data spans overlap");
            if(s->access!=access)continue;
            used=(used+(u32)s->alignment-1u)&~((u32)s->alignment-1u);
            if((u32)s->size>SCL_MAX_IMAGE_SIZE-rva-used)fatal("SCL initialized data image overflow");
            s->rva=(int)(rva+used);used+=(u32)s->size;
        }
        if(used){SclCSegment *s=segments+count++;s->rva=rva;s->memory=(used+4095u)&~4095u;s->file_size=used;s->access=(u32)access;rva+=s->memory;}
    }
    sclc_bss_rva=(int)rva;
    if(bss_used){SclCSegment *s=segments+count++;s->rva=rva;s->memory=((u32)bss_used+4095u)&~4095u;s->file_size=0;s->access=SCL_SEG_R|SCL_SEG_W;rva+=s->memory;}
    if(!count || rva>SCL_MAX_IMAGE_SIZE)fatal("SCL image empty or exceeds format limit");
    return count;
}
static void sclc_relocations(int *lookup)
{
    for(int i=0;i<fix_count;i++){
        Fix *f=fixes+i;
        sclc_fixes=sclc_grow(sclc_fixes,sclc_fix_count,&sclc_fix_capacity,sizeof(SclCFix));
        SclCFix *r=sclc_fixes+sclc_fix_count++;
        if(f->offset<0)r->patch=(u32)sclc_source_rva(-f->offset-1,4);
        else{if(f->offset>code_used-4)fatal("SCL code relocation outside text");r->patch=(u32)f->offset;}
        r->type=f->kind==F_REL?SCL_RELOC_PC32:SCL_RELOC_ABS32;
        r->addend=(u32)f->addend-(f->kind==F_REL?4u:0u);
        if(f->kind==F_DATA){r->target_kind=SCL_TARGET_INTERNAL;r->target=(u32)sclc_source_rva(f->symbol,1);}
        else{
            if(f->symbol<=0 || f->symbol>=symbol_count)fatal("SCL relocation has invalid symbol");
            if(symbols[f->symbol].defined){r->target_kind=SCL_TARGET_INTERNAL;r->target=sclc_symbol_rva(f->symbol);}
            else{int index=lookup[f->symbol];if(index<0)fatal("SCL unresolved reference is absent from manifest imports");
                r->target_kind=SCL_TARGET_IMPORT;r->target=(u32)index;sclc_imports[index].used=1;}
        }
    }
    for(int i=0;i<sclc_import_count;i++)if(!sclc_imports[i].used)fatal("SCL manifest import is not referenced by machine code or initializer");
    sclc_sort((u8 *)sclc_fixes,sclc_fix_count,sizeof(SclCFix),sclc_fix_compare);
    for(int i=1;i<sclc_fix_count;i++)if(sclc_fixes[i].patch<sclc_fixes[i-1].patch+4u)fatal("SCL relocations overlap");
}
static u32 sclc_table_size(u32 cursor,int count,u32 width)
{
    if(cursor>SCL_MAX_FILE_SIZE || (u32)count>(SCL_MAX_FILE_SIZE-cursor)/width)fatal("SCL metadata exceeds format limit");
    return cursor+(u32)count*width;
}
static void sclc_map_row(const char *kind,int name,u32 rva,u32 size)
{
    char row[192],hex[9],number_text[12];hex_text(hex,rva);
    copy(row,kind,sizeof(row));append(row," ",sizeof(row));append(row,hex,sizeof(row));append(row," ",sizeof(row));
    append(row,spelling(name),sizeof(row));append(row," ",sizeof(row));decimal(number_text,(int)size);append(row,number_text,sizeof(row));append(row,"\n",sizeof(row));
    int n=length(row);if(map_used+n>=(int)sizeof(map_text))fatal("SCL RVA map capacity exceeded");
    for(int i=0;i<n;i++)map_text[map_used++]=row[i];
    map_text[map_used]=0;
}
static void finish_scl(const char *output)
{
    SclCSegment segments[4];int *lookup=sclc_resolve();int segment_count=sclc_segments(segments);
    sclc_relocations(lookup);sc_free(lookup);
    u32 init=sclc_callback(sclc_init,0),fini=sclc_callback(sclc_fini,1);
    u32 segment_off=128u,export_off=sclc_table_size(segment_off,segment_count,SCL_SEGMENT_SIZE);
    u32 import_off=sclc_table_size(export_off,sclc_export_count,SCL_EXPORT_SIZE);
    u32 reloc_off=sclc_table_size(import_off,sclc_import_count,SCL_IMPORT_SIZE);
    u32 dep_off=sclc_table_size(reloc_off,sclc_fix_count,SCL_RELOC_SIZE);
    u32 string_off=sclc_table_size(dep_off,sclc_dep_count,SCL_DEPENDENCY_SIZE);
    u32 string_size=(u32)length(spelling(sclc_library))+1u;
    for(int i=0;i<sclc_export_count;i++){sclc_exports[i].string_offset=(int)string_size;string_size+=(u32)length(spelling(sclc_exports[i].name))+1u;}
    for(int i=0;i<sclc_import_count;i++){sclc_imports[i].string_offset=(int)string_size;string_size+=(u32)length(spelling(sclc_imports[i].name))+1u;}
    for(int i=0;i<sclc_dep_count;i++){sclc_dependencies[i].string_offset=(int)string_size;string_size+=(u32)length(spelling(sclc_dependencies[i].name))+1u;}
    if(string_size>SCL_MAX_FILE_SIZE-string_off)fatal("SCL string table exceeds format limit");
    u32 file_size=string_off+string_size;
    for(int i=0;i<segment_count;i++){
        SclCSegment *s=segments+i;s->file_offset=0;
        if(s->file_size){file_size=(file_size+3u)&~3u;if(s->file_size>SCL_MAX_FILE_SIZE-file_size)fatal("SCL payload exceeds format limit");s->file_offset=file_size;file_size+=s->file_size;}
    }
    u8 *image=sc_alloc(file_size);if(!image)fatal("SCL output allocation failed");
    for(u32 i=0;i<file_size;i++)image[i]=0;
    const char *magic=SCL_MAGIC;for(int i=0;i<8;i++)image[i]=(u8)magic[i];
    put32(image+8,128);put32(image+12,1);put32(image+16,3);put32(image+24,file_size);
    put32(image+28,segments[segment_count-1].rva+segments[segment_count-1].memory);
    put32(image+32,sclc_major);put32(image+36,sclc_minor);put32(image+40,sclc_version);
    put32(image+48,segment_off);put32(image+52,(u32)segment_count);put32(image+56,export_off);put32(image+60,(u32)sclc_export_count);
    put32(image+64,import_off);put32(image+68,(u32)sclc_import_count);put32(image+72,reloc_off);put32(image+76,(u32)sclc_fix_count);
    put32(image+80,dep_off);put32(image+84,(u32)sclc_dep_count);put32(image+88,string_off);put32(image+92,string_size);put32(image+96,init);put32(image+100,fini);
    for(int i=0;i<segment_count;i++){
        SclCSegment *s=segments+i;u8 *row=image+segment_off+(u32)i*SCL_SEGMENT_SIZE;
        put32(row,s->rva);put32(row+4,s->memory);put32(row+8,s->file_offset);put32(row+12,s->file_size);put32(row+16,s->access);put32(row+20,4096);
        if(s->access&SCL_SEG_X)for(int k=0;k<code_used;k++)image[s->file_offset+(u32)k]=code[36+k];
    }
    for(int i=0;i<sclc_span_count;i++){
        SclCSpan *s=sclc_spans+i;
        for(int j=0;j<segment_count;j++)if((u32)s->rva>=segments[j].rva && (u32)s->rva-segments[j].rva<segments[j].file_size){
            u32 at=segments[j].file_offset+(u32)s->rva-segments[j].rva;
            for(int k=0;k<s->size;k++)image[at+(u32)k]=data[s->offset+k];
            break;
        }
    }
    copy((char *)(image+string_off),spelling(sclc_library),(int)string_size);
    for(int i=0;i<sclc_export_count;i++){
        SclCExport *e=sclc_exports+i;u8 *row=image+export_off+(u32)i*SCL_EXPORT_SIZE;
        u32 size=(u32)scl_symbol_sizes[e->symbol];
        put32(row,(u32)e->string_offset);put32(row+4,sclc_symbol_rva(e->symbol));put32(row+8,size);put32(row+12,(u32)e->kind);
        copy((char *)(image+string_off+(u32)e->string_offset),spelling(e->name),length(spelling(e->name))+1);
        sclc_map_row("EXPORT",e->name,sclc_symbol_rva(e->symbol),size);
    }
    for(int i=0;i<sclc_import_count;i++){
        SclCImport *e=sclc_imports+i;u8 *row=image+import_off+(u32)i*SCL_IMPORT_SIZE;
        put32(row,(u32)e->string_offset);put32(row+4,(u32)e->index);put32(row+8,(u32)e->kind);put32(row+12,e->size);
        copy((char *)(image+string_off+(u32)e->string_offset),spelling(e->name),length(spelling(e->name))+1);
        sclc_map_row("IMPORT",e->name,(u32)i,e->size);
    }
    for(int i=0;i<sclc_dep_count;i++){
        SclCDependency *e=sclc_dependencies+i;u8 *row=image+dep_off+(u32)i*SCL_DEPENDENCY_SIZE;
        put32(row,(u32)e->string_offset);put32(row+4,e->major);put32(row+8,e->minor);put32(row+12,e->version);
        copy((char *)(image+string_off+(u32)e->string_offset),spelling(e->name),length(spelling(e->name))+1);
        sclc_map_row("REQUIRE",e->name,(u32)i,e->major);
    }
    for(int i=0;i<sclc_fix_count;i++){
        SclCFix *e=sclc_fixes+i;u8 *row=image+reloc_off+(u32)i*SCL_RELOC_SIZE;
        put32(row,e->patch);put32(row+4,e->type);put32(row+8,e->target_kind);put32(row+12,e->target);put32(row+16,e->addend);
        for(int j=0;j<segment_count;j++)if(e->patch>=segments[j].rva && e->patch-segments[j].rva<segments[j].file_size){
            put32(image+segments[j].file_offset+e->patch-segments[j].rva,0);break;
        }
    }
    if(scl_validate(image,file_size))fatal("SCL output failed common format validation");
    /* 至此才提交正文：语法、清单、回填、权限和map容量失败不会打开旧输出。
     * 磁盘I/O失败遵守既有sc_write语义并报告，不能伪称有跨文件原子事务。 */
    if(sc_write(map_path,map_text,map_used)!=map_used)fatal("cannot write SCL RVA map");
    if(sc_write(output,image,(int)file_size)!=(int)file_size)fatal("cannot write SCL output");
    sc_free(image);sc_write(log_path,"SCCC OK SCL NATIVE\n",19);sc_puts("SCCC OK SCL NATIVE\n");
    if(sclc_dependencies)sc_free(sclc_dependencies);
    if(sclc_exports)sc_free(sclc_exports);
    if(sclc_imports)sc_free(sclc_imports);
    if(sclc_spans)sc_free(sclc_spans);
    if(sclc_fixes)sc_free(sclc_fixes);
}

/* 共同验证源码在编译器内部使用，并不进入所生成库的正文。
 * 本文件没有依赖宿主库；原生SCCC也能预处理并编译这些普通C函数。 */
#include "runtime/scl_format.c"
