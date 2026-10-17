/* 单文件和自动项目沿真实SCO/SCL表生成精确导入，不要求用户手写链接JSON。
 * 只扫描固定系统库；自定义库/导出/版本仍由高级项目的显式清单控制。 */
typedef struct {char name[128];U kind,bytes,defined,provider;} StAutoSymbol;
static void application_image_name(const char *path,char out[36])
{
    const char *base=leaf(path);U n=(U)strlen(base)-4u;if(n>31u)n=31u;
    for(U i=0;i<n;i++){U c=(unsigned char)base[i];if(c>='a'&&c<='z')c-=32;out[i]=(char)((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'?c:'_');}
    memcpy(out+n,".SCX",5);
}
static ScStatus auto_blob(const char *name,unsigned char **out,U *bytes)
{
    ScPathInfo q;rec(&q,sizeof(q));ScStatus s=ScPathQuery(name,&q);
    if(s<0)return s;
    if(q.kind!=SC32_PATH_FILE||q.bytes>16777216u)return SC32_E_RANGE;
    unsigned char *p=malloc(q.bytes?q.bytes:1u);if(!p)return SC32_E_MEMORY;
    FILE *f=fopen(name,"rb");if(!f){free(p);return SC32_E_IO;}
    U n=(U)fread(p,1,q.bytes,f);int closed=fclose(f);
    if(n!=q.bytes||closed){free(p);return SC32_E_IO;}
    *out=p;*bytes=n;return 0;
}
static U auto_range(U bytes,U offset,U count,U width)
{return offset<=bytes&&width&&count<=(bytes-offset)/width;}
static const char *auto_string(const unsigned char *p,U bytes,U strings,U length,U at)
{
    if(!auto_range(bytes,strings,length,1)||at>=length||(at&&p[strings+at-1u]))return 0;
    const char *s=(const char *)p+strings+at;const char *end=memchr(s,0,length-at);
    if(!end||end==s||end-s>127)return 0;
    for(const char *c=s;c<end;c++)if(!((*c>='A'&&*c<='Z')||(*c>='a'&&*c<='z')||*c=='_'||(c>s&&*c>='0'&&*c<='9')))return 0;
    return s;
}
static int auto_compare(const void *a,const void *b)
{return strcmp(((const StAutoSymbol *)a)->name,((const StAutoSymbol *)b)->name);}
static StAutoSymbol *auto_find(StAutoSymbol *symbols,U count,const char *name)
{
    U lo=0,hi=count;
    while(lo<hi){U at=lo+(hi-lo)/2u;int order=strcmp(symbols[at].name,name);if(order<0)lo=at+1u;else hi=at;}
    return lo<count&&!strcmp(symbols[lo].name,name)?symbols+lo:0;
}
static ScStatus auto_dependency(const char *name)
{
    StBuild *b=&st.build;
    for(U i=0;i<b->dependency_count;i++)if(same_name(name,b->dependencies[i].path))return 0;
    if(b->dependency_count==b->dependency_capacity){
        U n=b->dependency_capacity?b->dependency_capacity*2u:16u;
        if(n>16777216u/sizeof(StDependency))return SC32_E_RANGE;
        StDependency *p=realloc(b->dependencies,n*sizeof(*p));if(!p)return SC32_E_MEMORY;
        b->dependencies=p;b->dependency_capacity=n;
    }
    ScPathInfo q;rec(&q,sizeof(q));ScStatus s=ScPathQuery(name,&q);if(s<0)return s;
    StDependency *d=b->dependencies+b->dependency_count++;strcpy(d->path,name);d->revision=q.generation;return 0;
}
static ScStatus automatic_manifest(void)
{
    StBuild *b=&st.build;StAutoSymbol *symbols=0;U count=0,capacity=0;ScStatus s=0;
    for(U i=0;i<b->input_count&&s>=0;i++)if(b->inputs[i].member.source){
        unsigned char *p=0;U bytes=0;s=auto_blob(b->inputs[i].object,&p,&bytes);
        if(s>=0&&(bytes<96u||memcmp(p,"SCO1MIO\0",8)||read32(p+24)!=bytes))s=SC32_E_INVALID;
        U offset=s>=0?read32(p+36):0,total=s>=0?read32(p+40):0;
        U strings=s>=0?read32(p+68):0,length=s>=0?read32(p+72):0;
        if(s>=0&&!auto_range(bytes,offset,total,32))s=SC32_E_INVALID;
        for(U k=0;k<total&&s>=0;k++){
            const unsigned char *row=p+offset+k*32u;if(read32(row+4)!=SCO_GLOBAL)continue;
            const char *name=auto_string(p,bytes,strings,length,read32(row));if(!name){s=SC32_E_INVALID;break;}
            if(count==capacity){U next=capacity?capacity*2u:64u;
                if(next>16777216u/sizeof(*symbols)){s=SC32_E_RANGE;break;}
                StAutoSymbol *more=realloc(symbols,next*sizeof(*more));if(!more){s=SC32_E_MEMORY;break;}
                symbols=more;capacity=next;
            }
            StAutoSymbol *d=symbols+count++;memset(d,0,sizeof(*d));strcpy(d->name,name);
            d->kind=read32(row+8);d->bytes=read32(row+20);d->defined=read32(row+12)!=0;d->provider=0xffffffffu;
            if((d->kind!=SCO_FUNCTION&&d->kind!=SCO_OBJECT)||!d->bytes)s=SC32_E_INVALID;
        }
        free(p);
    }
    if(s<0){free(symbols);return s;}
    /* 排序后一次合并跨TU定义，避免把项目自己实现的函数错当系统导入。 */
    qsort(symbols,count,sizeof(*symbols),auto_compare);U used=0;
    for(U i=0;i<count;i++){
        if(used&&!strcmp(symbols[used-1u].name,symbols[i].name)){
            StAutoSymbol *d=symbols+used-1u;
            if(d->kind!=symbols[i].kind){s=SC32_E_INVALID;break;}
            if(symbols[i].bytes>d->bytes)d->bytes=symbols[i].bytes;
            d->defined|=symbols[i].defined;
        }else symbols[used++]=symbols[i];
    }
    count=used;
    const char *libraries[]={"SCLDR.SCL","SC32API.SCL","SCCLIB.SCL","SCLIB.SCL","SCUI.SCL","SCSHELL.SCL","SCGFX.SCL","SCTERM.SCL","SCSOUND.SCL"};
    U versions[9][3],needed[9];memset(versions,0,sizeof(versions));memset(needed,0,sizeof(needed));
    for(U lib=0;lib<9u&&s>=0;lib++){
        char file[68];snprintf(file,sizeof(file),"/SYS/LIB/%s",libraries[lib]);
        unsigned char *p=0;U bytes=0;s=auto_blob(file,&p,&bytes);
        if(s==SC32_E_NOT_FOUND){s=0;continue;}if(s<0)break;
        if(bytes<128u||memcmp(p,"SCL1MIO\0",8)||read32(p+24)!=bytes){free(p);s=SC32_E_INVALID;break;}
        U offset=read32(p+56),total=read32(p+60),strings=read32(p+88),length=read32(p+92);
        if(!auto_range(bytes,offset,total,20)){free(p);s=SC32_E_INVALID;break;}
        for(U k=0;k<3;k++)versions[lib][k]=read32(p+32u+k*4u);
        for(U k=0;k<total&&s>=0;k++){
            const unsigned char *row=p+offset+k*20u;const char *name=auto_string(p,bytes,strings,length,read32(row));
            if(!name){s=SC32_E_INVALID;break;}StAutoSymbol *d=auto_find(symbols,count,name);
            if(!d||d->defined)continue;
            if(d->kind!=read32(row+12)||d->bytes>read32(row+8)){snprintf(b->first_error,sizeof(b->first_error),"系统库 %s 的 %s 类型或容量与源码不符；请恢复配套 SDK。",libraries[lib],name);s=SC32_E_VERSION;break;}
            if(d->provider!=0xffffffffu&&d->provider!=lib){snprintf(b->first_error,sizeof(b->first_error),"符号 %s 同时由多个系统库提供；请在项目设置中指定链接清单。",name);s=SC32_E_INVALID;break;}
            d->provider=lib;needed[lib]=1;
        }
        free(p);if(s>=0&&needed[lib])s=auto_dependency(file);
    }
    for(U i=0;i<count&&s>=0;i++)if(!symbols[i].defined&&symbols[i].provider==0xffffffffu){
        snprintf(b->first_error,sizeof(b->first_error),"链接错误：找不到符号 %s；检查函数名称。第三方库请在项目设置中指定链接清单。",symbols[i].name);
        s=SC32_E_NOT_FOUND;
    }
    if(s<0){free(symbols);return s;}
    s=join(b->automatic_manifest,b->directory,"AUTO.JSON");
    FILE *f=s>=0?fopen(b->automatic_manifest,"wb"):0;if(s>=0&&!f)s=SC32_E_IO;
    if(s>=0){
        /* 临时输出和内部标识必须逐字节相同；最终发布文件名独立保留。 */
        if(fprintf(f,"{\"kind\":\"application\",\"name\":\"%s\",\"dependencies\":[",leaf(b->candidate))<0)s=SC32_E_IO;
        U comma=0;
        for(U lib=0;lib<9u;lib++)if(needed[lib]){
            if(fprintf(f,"%s{\"name\":\"%s\",\"abi_major\":%u,\"min_minor\":%u,\"min_version\":%u}",comma?",":"",libraries[lib],versions[lib][0],versions[lib][1],versions[lib][2])<0)s=SC32_E_IO;
            comma=1;
        }
        if(fprintf(f,"],\"imports\":[")<0)s=SC32_E_IO;
        comma=0;
        for(U i=0;i<count;i++)if(!symbols[i].defined){StAutoSymbol *d=symbols+i;
            if(fprintf(f,"%s{\"name\":\"%s\",\"dependency\":\"%s\",\"kind\":\"%s\",\"size\":%u}",comma?",":"",d->name,libraries[d->provider],d->kind==SCO_FUNCTION?"function":"data",d->bytes)<0)s=SC32_E_IO;
            comma=1;
        }
        if(fprintf(f,"]}\n")<0)s=SC32_E_IO;
        if(fclose(f))s=SC32_E_IO;
        ScPathInfo q;rec(&q,sizeof(q));if(s>=0)s=ScPathQuery(b->automatic_manifest,&q);
        if(s>=0)b->manifest_revision=q.generation;
    }
    free(symbols);return s;
}
