/* SandCore原生SCO链接器。标准C库只存在于这个Ring3程序。
 * 每个TU只读一次；所有对象保留到最终提交，重定位/源码表借用不会悬空。
 * 全局哈希解析和分组布局与源码长度无关，不展开或合并任何C源码。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef SCLD_HOST
#include "SC32API.H"
#endif
#include "SCOFMT.H"
#include "SCDBGFILE.H"
#include "runtime/SCLFMT.H"
#include "runtime/SCDYN.H"
typedef unsigned int U;
typedef unsigned char B;
typedef struct { B *p;U n,cap; } Buf;
typedef struct { U offset,bytes,memory,align,access,rva; } Section;
typedef struct Object Object;
typedef struct Symbol Symbol;
struct Symbol { const char *name;U binding,kind,section,value,bytes,abi,flags;Object *owner;Symbol *definition;int imported; };
struct Object { const char *path;B *blob;U bytes,index;Section sec[4];Symbol *symbols;U nsym,nrel,rels,nfile,files,nline,lines,strings,string_bytes,file_base; };
typedef struct { char *name;U major,minor,version,so; } Dep;
typedef struct { char *name,*dependency;U kind,bytes,dep,used,so; } Import;
typedef struct { char *name;U kind,so;Symbol *symbol; } Export;
typedef struct { char *name,*init,*fini;U library,major,minor,version;Dep *dep;U nd,cd;Import *imp;U ni,ci;Export *exp;U ne,ce; } Manifest;
typedef struct { U patch,type,external,target,addend; } Reloc;
typedef struct { char *name;Symbol *definition,*declaration; } Global;
typedef struct { const char *p;U at,n; } Json;
static Manifest mf;
static Object *objects;
static U nobj,cobj;
static Global *globals;
static U global_cap;
static Reloc *relocs;
static U nrel,crel;
static U group_start[4],group_size[4],group_align[4],image_memory;
static U image_load;
static const char *context="command";
static void fail(const char *why,const char *detail)
{fprintf(stderr,"SCLD ERROR %s:0: %s%s%s\n",context,why,detail?": ":"",detail?detail:"");exit(1);}
static U rd(const B *p){return (U)p[0]|((U)p[1]<<8)|((U)p[2]<<16)|((U)p[3]<<24);}
static void wr(B *p,U v){p[0]=(B)v;p[1]=(B)(v>>8);p[2]=(B)(v>>16);p[3]=(B)(v>>24);}
static U plus(U a,U b,U limit){if(a>limit || b>limit-a)fail("size overflow",0);return a+b;}
static int power(U n){return n && !(n&(n-1u)) && n<=4096u;}
static U align_to(U value,U a){if(!power(a))fail("invalid alignment",0);return plus(value,a-1u,SCO_MAX_MEMORY)&~(a-1u);}
static void *zero(U n){void *p=calloc(n?n:1u,1u);if(!p)fail("out of memory",0);return p;}
static void *slot(void **base,U used,U *cap,U width)
{
    if(used==*cap){U next=*cap?*cap*2u:8u;if(next<*cap || next>SCO_MAX_MEMORY/width)fail("table too large",0);
        void *p=realloc(*base,next*width);if(!p)fail("out of memory",0);*base=p;*cap=next;}
    B *p=*base;memset(p+used*width,0,width);return p+used*width;
}
static void reserve(Buf *b,U need)
{
    if(need>b->cap){U next=b->cap?b->cap:256u;while(next<need){if(next>SCO_MAX_FILE/2u){next=SCO_MAX_FILE;break;}next*=2u;}
        if(need>next)fail("file exceeds 16MiB",0);
        B *p=realloc(b->p,next);
        if(!p)fail("out of memory",0);
        b->p=p;
        b->cap=next;
        }
}
static void bytes(Buf *b,const void *p,U n){U end=plus(b->n,n,SCO_MAX_FILE);reserve(b,end);memcpy(b->p+b->n,p,n);b->n=end;}
static U string(Buf *b,const char *s){U at=b->n;bytes(b,s,(U)strlen(s)+1u);return at;}
static void zeros(Buf *b,U n){U end=plus(b->n,n,SCO_MAX_FILE);reserve(b,end);memset(b->p+b->n,0,n);b->n=end;}
static B *read_file(const char *path,U *size)
{
    FILE *f=fopen(path,"rb");if(!f)fail("cannot open input",path);
    if(fseek(f,0,SEEK_END)){fclose(f);fail("cannot seek input",path);}long length=ftell(f);
    if(length<0 || (unsigned long)length>SCO_MAX_FILE){fclose(f);fail("input too large",path);}
    *size=(U)length;B *p=zero(*size+1u);if(fseek(f,0,SEEK_SET) || fread(p,1,*size,f)!=*size){fclose(f);fail("cannot read complete input",path);}
    if(fclose(f))fail("cannot close input",path);
    return p;
}
static U fnv(const char *s){U h=2166136261u;while(*s)h=(h^(B)*s++)*16777619u;return h;}
static int same_path(const char *a,const char *b)
{
    if(*a=='/')a++;
    if(*b=='/')b++;
    for(;;){U x=(B)*a++,y=(B)*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;if(!x)return 1;}
}
static int valid_symbol(const char *s)
{
    U n=0;for(;s[n];n++){U c=(B)s[n];if(n>=127u || !((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||(n&&c>='0'&&c<='9')))return 0;}return n!=0;
}
static int valid_name(const char *s,const char *suffix)
{
    U n=(U)strlen(s);if(n<5u||n>35u||strcmp(s+n-4u,suffix))return 0;
    for(U i=0;i<n-4u;i++)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'))return 0;
    return 1;
}
static void jspace(Json *j){while(j->at<j->n && (j->p[j->at]==' '||j->p[j->at]=='\n'||j->p[j->at]=='\r'||j->p[j->at]=='\t'))j->at++;}
static int take(Json *j,int c){jspace(j);if(j->at<j->n&&j->p[j->at]==c){j->at++;return 1;}return 0;}
static void need(Json *j,int c){if(!take(j,c))fail("invalid manifest punctuation",0);}
static U hex_digit(int c){if(c>='0'&&c<='9')return (U)(c-'0');if(c>='a'&&c<='f')return (U)(c-'a'+10);if(c>='A'&&c<='F')return (U)(c-'A'+10);fail("bad JSON escape",0);return 0;}
static char *jstr(Json *j)
{
    char text[128];U n=0;need(j,'"');int ended=0;
    while(j->at<j->n){U c=(B)j->p[j->at++];if(c=='"'){ended=1;break;}
        if(c=='\\'){if(j->at==j->n)fail("unfinished JSON escape",0);c=(B)j->p[j->at++];
            if(c=='u'){c=0;for(U k=0;k<4;k++){if(j->at==j->n)fail("short JSON escape",0);c=c*16u+hex_digit(j->p[j->at++]);}}
            else if(c!='"'&&c!='\\'&&c!='/')fail("control escape is not a name",0);}
        if(c<32u||c>=128u||n==127u)fail("manifest names need non-NUL ASCII within 127 bytes",0);
        text[n++]=(char)c;
        }
    if(!ended)fail("unterminated JSON string",0);
    text[n]=0;
    char *out=zero(n+1u);
    memcpy(out,text,n+1u);
    return out;
}
static U jnum(Json *j)
{
    jspace(j);U start=j->at,value=0;
    while(j->at<j->n&&j->p[j->at]>='0'&&j->p[j->at]<='9'){U d=(U)(j->p[j->at++]-'0');if(value>(0xffffffffu-d)/10u)fail("manifest integer overflows u32",0);value=value*10u+d;}
    if(start==j->at||(j->at-start>1u&&j->p[start]=='0'))fail("canonical unsigned integer required",0);
    if(j->at<j->n && j->p[j->at]!=','&&j->p[j->at]!='}'&&j->p[j->at]!=']'&&j->p[j->at]!=' '&&j->p[j->at]!='\r'&&j->p[j->at]!='\n'&&j->p[j->at]!='\t')fail("invalid integer suffix",0);
    return value;
}
static char *nullable(Json *j){jspace(j);if(j->n-j->at>=4u&&!memcmp(j->p+j->at,"null",4)){j->at+=4;return 0;}return jstr(j);}
static U kind(Json *j){char *s=jstr(j);U k=!strcmp(s,"function")?1u:!strcmp(s,"data")?2u:0u;free(s);if(!k)fail("invalid symbol kind",0);return k;}
static void seen(U *mask,U bit){if(*mask&bit)fail("duplicate JSON member",0);*mask|=bit;}
static int last_member(Json *j){if(take(j,'}'))return 1;need(j,',');return 0;}
static void dependencies(Json *j)
{
    need(j,'[');if(take(j,']'))return;
    do{Dep *d=slot((void **)&mf.dep,mf.nd++,&mf.cd,sizeof(Dep));U mask=0;need(j,'{');
        if(!take(j,'}'))do{char *f=jstr(j);need(j,':');
            if(!strcmp(f,"name")){seen(&mask,1);d->name=jstr(j);}else if(!strcmp(f,"abi_major")){seen(&mask,2);d->major=jnum(j);}
            else if(!strcmp(f,"min_minor")){seen(&mask,4);d->minor=jnum(j);}else if(!strcmp(f,"min_version")){seen(&mask,8);d->version=jnum(j);}else fail("unknown dependency member",f);
            free(f);if(last_member(j))break;}while(1);
        if(mask!=15u||!d->major||!valid_name(d->name,".SCL"))fail("invalid dependency",d->name);
    }while(take(j,','));need(j,']');
}
static void imports(Json *j)
{
    need(j,'[');if(take(j,']'))return;
    do{Import *d=slot((void **)&mf.imp,mf.ni++,&mf.ci,sizeof(Import));U mask=0;need(j,'{');
        if(!take(j,'}'))do{char *f=jstr(j);need(j,':');
            if(!strcmp(f,"name")){seen(&mask,1);d->name=jstr(j);}else if(!strcmp(f,"dependency")){seen(&mask,2);d->dependency=jstr(j);}
            else if(!strcmp(f,"kind")){seen(&mask,4);d->kind=kind(j);}else if(!strcmp(f,"size")){seen(&mask,8);d->bytes=jnum(j);}else fail("unknown import member",f);
            free(f);if(last_member(j))break;}while(1);
        if(mask!=15u||!d->bytes||!valid_symbol(d->name)||!valid_name(d->dependency,".SCL"))fail("invalid import",d->name);
    }while(take(j,','));need(j,']');
}
static void exports(Json *j)
{
    need(j,'[');if(take(j,']'))return;
    do{Export *d=slot((void **)&mf.exp,mf.ne++,&mf.ce,sizeof(Export));U mask=0;need(j,'{');
        if(!take(j,'}'))do{char *f=jstr(j);need(j,':');
            if(!strcmp(f,"name")){seen(&mask,1);d->name=jstr(j);}else if(!strcmp(f,"kind")){seen(&mask,2);d->kind=kind(j);}else fail("unknown export member",f);
            free(f);if(last_member(j))break;}while(1);
        if(mask!=3u||!valid_symbol(d->name))fail("invalid export",d->name);
    }while(take(j,','));need(j,']');
}
static int dep_cmp(const void *a,const void *b){return strcmp(((const Dep *)a)->name,((const Dep *)b)->name);}
static int exp_cmp(const void *a,const void *b){return strcmp(((const Export *)a)->name,((const Export *)b)->name);}
static int imp_cmp(const void *a,const void *b){const Import *x=a,*y=b;if(x->dep!=y->dep)return x->dep<y->dep?-1:1;return strcmp(x->name,y->name);}
static void read_manifest(const char *path,const char *output,U library)
{
    U size;B *p=read_file(path,&size);if(size>1048576u)fail("manifest exceeds 1MiB",path);Json j;U mask=0;j.p=(char *)p;j.at=0;j.n=size;mf.library=library;
    need(&j,'{');if(!take(&j,'}'))do{char *f=jstr(&j);need(&j,':');
        if(!strcmp(f,"name")){seen(&mask,1);mf.name=jstr(&j);}else if(!strcmp(f,"dependencies")){seen(&mask,2);dependencies(&j);}
        else if(!strcmp(f,"imports")){seen(&mask,4);imports(&j);}else if(!library&&!strcmp(f,"kind")){seen(&mask,8);char *k=jstr(&j);if(strcmp(k,"application"))fail("application kind required",k);free(k);}
        else if(library&&!strcmp(f,"abi_major")){seen(&mask,8);mf.major=jnum(&j);}else if(library&&!strcmp(f,"abi_minor")){seen(&mask,16);mf.minor=jnum(&j);}
        else if(library&&!strcmp(f,"library_version")){seen(&mask,32);mf.version=jnum(&j);}else if(library&&!strcmp(f,"init")){seen(&mask,64);mf.init=nullable(&j);}
        else if(library&&!strcmp(f,"fini")){seen(&mask,128);mf.fini=nullable(&j);}else if(library&&!strcmp(f,"exports")){seen(&mask,256);exports(&j);}else fail("unknown manifest member",f);
        free(f);if(last_member(&j))break;}while(1);
    jspace(&j);if(j.at!=j.n||mask!=(library?511u:15u)||!valid_name(mf.name,library?".SCL":".SCX")||(library&&!mf.major))fail("incomplete or trailing manifest",path);
    const char *base=output;for(const char *s=output;*s;s++)if(*s=='/'||*s=='\\')base=s+1;if(strcmp(base,mf.name))fail("output basename differs from manifest name",mf.name);
    if((mf.init&&!valid_symbol(mf.init))||(mf.fini&&!valid_symbol(mf.fini)))fail("invalid callback name",0);
    qsort(mf.dep,mf.nd,sizeof(Dep),dep_cmp);qsort(mf.exp,mf.ne,sizeof(Export),exp_cmp);
    for(U i=0;i<mf.nd;i++)if((i&&!strcmp(mf.dep[i-1].name,mf.dep[i].name))||!strcmp(mf.dep[i].name,mf.name))fail("duplicate or self dependency",mf.dep[i].name);
    for(U i=0;i<mf.ne;i++)if(i&&!strcmp(mf.exp[i-1].name,mf.exp[i].name))fail("duplicate export",mf.exp[i].name);
    for(U i=0;i<mf.ni;i++){Import *im=mf.imp+i;im->dep=mf.nd;for(U k=0;k<mf.nd;k++)if(!strcmp(mf.dep[k].name,im->dependency))im->dep=k;
        if(im->dep==mf.nd)fail("import has no declared dependency",im->name);
        for(U k=0;k<i;k++)if(!strcmp(mf.imp[k].name,im->name))fail("duplicate import",im->name);
        }
    qsort(mf.imp,mf.ni,sizeof(Import),imp_cmp);free(p);
}
static U table(const B *p,U size,U off,U count,U width,U expected)
{(void)p;if(off!=expected||off>size||count>(size-off)/width)fail("noncanonical or overflowing SCO table",0);return off+count*width;}
static const char *ostr(Object *o,U at,int symbol)
{
    if(at>=o->string_bytes||(at&&o->blob[o->strings+at-1u]))fail("SCO string is not a start",0);
    const char *s=(char *)o->blob+o->strings+at;U i=at;while(i<o->string_bytes&&o->blob[o->strings+i])i++;
    if(i==o->string_bytes||!s[0]||(symbol&&!valid_symbol(s)))fail("invalid SCO string",0);
    if(!symbol){for(U k=at;k<i;k++)if(o->blob[o->strings+k]<32u)fail("invalid source path",0);}return s;
}
typedef struct { U section,offset; } Patch;
static int patch_cmp(const void *a,const void *b){const Patch *x=a,*y=b;if(x->section!=y->section)return x->section<y->section?-1:1;return x->offset<y->offset?-1:x->offset!=y->offset;}
static void read_object(Object *o,const char *path,U index)
{
    context=path;o->path=path;o->index=index;o->blob=read_file(path,&o->bytes);B *p=o->blob;U size=o->bytes;
    if(size<SCO_HEADER||memcmp(p,"SCO1MIO\0",8)||rd(p+8)!=1u||rd(p+12)!=96u||rd(p+16)!=3u||rd(p+20)||rd(p+24)!=size)fail("unsupported SCO header",0);
    for(U i=76;i<96;i+=4)if(rd(p+i))fail("nonzero SCO reserved word",0);
    U ns=rd(p+32),syms=rd(p+36),rels=rd(p+44),files=rd(p+52),lines=rd(p+60),cursor=96u;
    if(ns!=4u)fail("SCO requires four logical sections",0);
    cursor=table(p,size,rd(p+28),ns,24,cursor);
    o->nsym=rd(p+40);cursor=table(p,size,syms,o->nsym,32,cursor);o->nrel=rd(p+48);cursor=table(p,size,rels,o->nrel,24,cursor);
    o->nfile=rd(p+56);cursor=table(p,size,files,o->nfile,16,cursor);o->nline=rd(p+64);cursor=table(p,size,lines,o->nline,16,cursor);
    o->strings=rd(p+68);o->string_bytes=rd(p+72);o->rels=rels;o->files=files;o->lines=lines;
    if(o->strings!=cursor||!o->string_bytes||o->string_bytes>size-cursor||p[cursor+o->string_bytes-1u])fail("invalid SCO strings",0);
    cursor+=o->string_bytes;
    U total=0;
    for(U i=0;i<4;i++){B *r=p+96u+i*24u;Section *s=o->sec+i;
        s->offset=rd(r+4);s->bytes=rd(r+8);s->memory=rd(r+12);s->align=rd(r+16);s->access=rd(r+20);
        if(rd(r)!=i+1u||!power(s->align)||s->access!=(i==0?5u:i==1?1u:3u)||s->memory>SCO_MAX_MEMORY-total)fail("invalid SCO section",0);
        total+=s->memory;
        if(i==3u){if(s->bytes||s->offset)fail("BSS must have no payload",0);}
        else if(s->bytes!=s->memory)fail("initialized SCO section needs exact payload",0);
        if(s->bytes){if(s->offset<cursor||s->offset>size||s->bytes>size-s->offset||s->offset%4u)fail("SCO payload overlaps tables",0);
            while(cursor<s->offset)if(p[cursor++])fail("nonzero SCO padding",0);
            cursor=s->offset+s->bytes;
            }
        else if(s->offset)fail("empty SCO section has payload offset",0);
    }
    if(cursor!=size)fail("SCO trailing bytes",0);
    o->symbols=zero(o->nsym*(U)sizeof(Symbol));
    for(U i=0;i<o->nsym;i++){B *r=p+syms+i*32u;Symbol *s=o->symbols+i;s->name=ostr(o,rd(r),1);s->owner=o;s->imported=-1;
        s->binding=rd(r+4);s->kind=rd(r+8);s->section=rd(r+12);s->value=rd(r+16);s->bytes=rd(r+20);s->abi=rd(r+24);s->flags=rd(r+28);U alignment=s->flags>>8;
        if(s->binding>1u||(s->kind!=1u&&s->kind!=2u)||s->section>4u||!s->bytes||!power(alignment)||(s->flags&252u)||((s->flags&3u)==3u))fail("invalid SCO symbol",s->name);
        if(s->kind==2u && (s->abi||(s->flags&3u)))fail("data symbol has function ABI flags",s->name);
        if(s->section){Section *sc=o->sec+s->section-1u;if(s->value>=sc->memory||s->bytes>sc->memory-s->value||s->value%alignment||sc->align<alignment||(s->kind==1u)!=(s->section==1u))fail("SCO definition exceeds section or alignment",s->name);}
        else if(!s->binding||s->value)fail("undefined SCO symbol must have global binding",s->name);
    }
    for(U i=0;i<o->nfile;i++){B *r=p+files+i*16u;ostr(o,rd(r),0);if(rd(r+12))fail("nonzero SCO source reserved",0);}
    U previous=0;for(U i=0;i<o->nline;i++){B *r=p+lines+i*16u;U offset=rd(r+4);
        if(rd(r)!=1u||offset>=o->sec[0].bytes||rd(r+8)>=o->nfile||!rd(r+12)||(i&&offset<=previous))fail("invalid SCO source line",0);
        previous=offset;
        }
    Patch *patches=zero(o->nrel*(U)sizeof(Patch));
    for(U i=0;i<o->nrel;i++){B *r=p+rels+i*24u;U sec=rd(r),off=rd(r+4),type=rd(r+8),tk=rd(r+12),target=rd(r+16);
        if(sec<1u||sec>3u||o->sec[sec-1u].bytes<4u||off>o->sec[sec-1u].bytes-4u||(type!=1u&&type!=2u)||tk>1u)fail("invalid SCO relocation patch",0);
        if((tk==0u&&target>=o->nsym)||(tk==1u&&(target<1u||target>4u||!o->sec[target-1u].memory)))fail("invalid SCO relocation target",0);
        if(rd(p+o->sec[sec-1u].offset+off))fail("SCO relocation patch must be zero",0);
        patches[i].section=sec;
        patches[i].offset=off;
    }
    qsort(patches,o->nrel,sizeof(Patch),patch_cmp);for(U i=1;i<o->nrel;i++)if(patches[i].section==patches[i-1u].section&&patches[i].offset-patches[i-1u].offset<4u)fail("overlapping SCO relocations",0);free(patches);
}
static Global *global(const char *name)
{
    U at=fnv(name)&(global_cap-1u);while(globals[at].name&&strcmp(globals[at].name,name))at=(at+1u)&(global_cap-1u);
    return globals+at;
}
static void check_types(Symbol *a,Symbol *b)
{
    if(a->kind!=b->kind||(a->kind==1u&&a->abi!=b->abi))fail("cross-object kind or cdecl ABI conflict",a->name);
    if(a->kind==2u&&b->section&&(b->bytes<a->bytes||(b->flags>>8)<(a->flags>>8)))fail("external data is smaller or less aligned than declaration",a->name);
}
static void resolve(void)
{
    U count=0;for(U i=0;i<nobj;i++)count=plus(count,objects[i].nsym,SCO_MAX_MEMORY/(U)sizeof(Global)/2u);
    global_cap=8u;while(global_cap<count*2u)global_cap*=2u;globals=zero(global_cap*(U)sizeof(Global));
    for(U i=0;i<nobj;i++)for(U k=0;k<objects[i].nsym;k++){Symbol *s=objects[i].symbols+k;if(!s->binding)continue;context=s->owner->path;Global *g=global(s->name);
        if(!g->name)g->name=(char *)s->name;
        if(g->declaration)check_types(s,g->declaration);else g->declaration=s;
        if(s->section){if(g->definition){fprintf(stderr,"SCLD duplicate %s: %s and %s\n",s->name,g->definition->owner->path,s->owner->path);fail("duplicate external definition",s->name);}g->definition=s;}
    }
    for(U i=0;i<nobj;i++)for(U k=0;k<objects[i].nsym;k++){Symbol *s=objects[i].symbols+k;context=s->owner->path;
        if(s->section){s->definition=s;continue;}Global *g=global(s->name);if(g->definition){check_types(s,g->definition);s->definition=g->definition;continue;}
        for(U m=0;m<mf.ni;m++)if(!strcmp(mf.imp[m].name,s->name)){Import *im=mf.imp+m;if(im->kind!=s->kind||im->bytes<s->bytes)fail("dynamic import kind or minimum size mismatch",s->name);s->imported=(int)m;break;}
        if(s->imported<0)fail("unresolved external symbol",s->name);
    }
    for(U i=0;i<mf.ne;i++){Export *e=mf.exp+i;Global *g=global(e->name);e->symbol=g->definition;if(!e->symbol||e->symbol->kind!=e->kind)fail("export is missing, static or has wrong kind",e->name);}
}
static U symbol_rva(Symbol *s){return s->owner->sec[s->section-1u].rva+s->value;}
static U callback(const char *name,U flag)
{
    if(!name)return 0xffffffffu;
    Global *g=global(name);
    Symbol *s=g->definition;
    if(!s||s->kind!=1u||!(s->flags&flag))fail("callback needs an external int(void) or void(void) definition",name);
    return symbol_rva(s);
}
static void layout(void)
{
    U cursor=mf.library?0u:64u;
    for(U group=0;group<4u;group++){
        U a=1;for(U i=0;i<nobj;i++)if(objects[i].sec[group].memory&&objects[i].sec[group].align>a)a=objects[i].sec[group].align;
        group_align[group]=a;cursor=align_to(cursor,mf.library?4096u:a);group_start[group]=cursor;
        for(U i=0;i<nobj;i++){Section *s=objects[i].sec+group;if(!s->memory){s->rva=cursor;continue;}
            cursor=align_to(cursor,s->align);s->rva=cursor;cursor=plus(cursor,s->memory,SCO_MAX_MEMORY);}
        group_size[group]=cursor-group_start[group];
        if(mf.library&&group_size[group])cursor=align_to(cursor,4096u);
    }
    image_memory=cursor;image_load=group_start[3];
}
static int rel_cmp(const void *a,const void *b){U x=((const Reloc *)a)->patch,y=((const Reloc *)b)->patch;return x<y?-1:x!=y;}
static void collect_relocs(void)
{
    for(U i=0;i<nobj;i++){Object *o=objects+i;context=o->path;for(U k=0;k<o->nrel;k++){
        B *r=o->blob+o->rels+k*24u;Reloc *out=slot((void **)&relocs,nrel++,&crel,sizeof(Reloc));U sec=rd(r),target=rd(r+16);
        out->patch=o->sec[sec-1u].rva+rd(r+4);out->type=rd(r+8);out->addend=rd(r+20);
        if(rd(r+12)==1u)out->target=o->sec[target-1u].rva;
        else{Symbol *s=o->symbols+target;if(s->definition)out->target=symbol_rva(s->definition);
            else{out->external=1;out->target=(U)s->imported;mf.imp[out->target].used=1;}}
    }}
    qsort(relocs,nrel,sizeof(Reloc),rel_cmp);for(U i=1;i<nrel;i++)if(relocs[i].patch-relocs[i-1u].patch<4u)fail("linked relocations overlap",0);
    for(U i=0;i<mf.ni;i++)if(!mf.imp[i].used)fail("unused manifest import",mf.imp[i].name);
}
static void copy_sections(B *payload)
{for(U i=0;i<nobj;i++)for(U k=0;k<3u;k++){Section *s=objects[i].sec+k;if(s->bytes)memcpy(payload+s->rva,objects[i].blob+s->offset,s->bytes);}}
static void crt(B *p,U descriptor)
{
    const B code[64]={0x83,0xec,0x20,0x89,0xe3,0xb9,0,0,0,0,0xb8,0x83,2,0,0,0xcd,0x7c,0x85,0xc0,0x78,0x1c,0x8b,0x44,0x24,0x0c,0x89,0xe2,0x83,0xec,8,0x52,0x68,0,0,0,0,0xff,0xd0,0x83,0xc4,0x30,0x89,0xc3,0x31,0xc0,0xcd,0x7c,0xeb,0xfe,0x83,0xc4,0x20,0xbb,1,0,0,0,0x31,0xc0,0xcd,0x7c,0xeb,0xfe,0x90};
    memcpy(p,code,64);wr(p+6,descriptor);wr(p+32,descriptor);
}
static Buf scx_image(void)
{
    U main_rva=callback("main",1u),external=0;for(U i=0;i<nrel;i++)if(relocs[i].external)external++;
    U deps=64u,imports=plus(deps,mf.nd*20u,SCO_MAX_FILE),rs=plus(imports,mf.ni*16u,SCO_MAX_FILE),ss=plus(rs,external*16u,SCO_MAX_FILE);
    Buf strings={0};zeros(&strings,1);for(U i=0;i<mf.nd;i++)mf.dep[i].so=string(&strings,mf.dep[i].name);for(U i=0;i<mf.ni;i++)mf.imp[i].so=string(&strings,mf.imp[i].name);
    U end=plus(image_load,plus(ss,strings.n,SCO_MAX_FILE),SCO_MAX_MEMORY);U load=align_to(end,group_align[3]);zeros(&strings,load-end);
    U bss=group_size[3];if(load>262108u||bss>0x3d0000u-load)fail("SCX old load/private image budget exceeded",0);
    /* BSS位于全部可变metadata之后。先确定它的真实对齐基址，再修内部
     * 地址；不能只修map或把未对齐u32强转当作容器满足了C对齐。 */
    U shift=load-group_start[3];for(U i=0;i<nobj;i++)objects[i].sec[3].rva+=shift;
    for(U i=0;i<nrel;i++)if(!relocs[i].external&&relocs[i].target>=group_start[3])relocs[i].target+=shift;
    group_start[3]=load;image_memory=load+bss;
    Buf out={0};zeros(&out,36u+load);B *p=out.p,*payload=p+36,*h=payload+image_load;copy_sections(payload);crt(payload,0x400000u+image_load);
    memcpy(p,"SCX1MIO\0",8);wr(p+12,load);wr(p+16,bss);wr(p+20,131072);wr(p+28,0x400000u);wr(p+32,0x004f494du);
    memcpy(h,"SCD1MIO\0",8);wr(h+8,1);wr(h+12,64);wr(h+16,ss+strings.n);wr(h+20,load);wr(h+24,bss);wr(h+28,main_rva);
    wr(h+32,deps);wr(h+36,mf.nd);wr(h+40,imports);wr(h+44,mf.ni);wr(h+48,rs);wr(h+52,external);wr(h+56,ss);wr(h+60,strings.n);
    for(U i=0;i<mf.nd;i++){Dep *d=mf.dep+i;B *r=h+deps+i*20u;wr(r,d->so);wr(r+4,d->major);wr(r+8,d->minor);wr(r+12,d->version);}
    for(U i=0;i<mf.ni;i++){Import *im=mf.imp+i;B *r=h+imports+i*16u;wr(r,im->so);wr(r+4,im->dep);wr(r+8,im->kind);wr(r+12,im->bytes);}
    U at=0;for(U i=0;i<nrel;i++){Reloc *r=relocs+i;if(r->external){B *d=h+rs+at++*16u;wr(d,r->patch);wr(d+4,r->type);wr(d+8,r->target);wr(d+12,r->addend);}
        else{U value=0x400000u+r->target+r->addend;if(r->type==2u)value-=0x400000u+r->patch;wr(payload+r->patch,value);}}
    memcpy(h+ss,strings.p,strings.n);free(strings.p);if(scd_validate(h,ss+rd(h+60),image_load))fail("linked SCD failed common validator",0);return out;
}
static Buf scl_image(void)
{
    U init=callback(mf.init,1u),fini=callback(mf.fini,2u),segments=0;for(U i=0;i<4;i++)if(group_size[i])segments++;
    U seg=128u,ex=plus(seg,segments*24u,SCO_MAX_FILE),im=plus(ex,mf.ne*20u,SCO_MAX_FILE),re=plus(im,mf.ni*16u,SCO_MAX_FILE),de=plus(re,nrel*20u,SCO_MAX_FILE),st=plus(de,mf.nd*20u,SCO_MAX_FILE);
    Buf strings={0};string(&strings,mf.name);for(U i=0;i<mf.ne;i++)mf.exp[i].so=string(&strings,mf.exp[i].name);for(U i=0;i<mf.ni;i++)mf.imp[i].so=string(&strings,mf.imp[i].name);for(U i=0;i<mf.nd;i++)mf.dep[i].so=string(&strings,mf.dep[i].name);
    U cursor=plus(st,strings.n,SCO_MAX_FILE),payload[4]={0};for(U i=0;i<3;i++)if(group_size[i]){cursor=align_to(cursor,4);payload[i]=cursor;cursor=plus(cursor,group_size[i],SCO_MAX_FILE);}
    Buf out={0};zeros(&out,cursor);B *p=out.p;memcpy(p,"SCL1MIO\0",8);wr(p+8,128);wr(p+12,1);wr(p+16,3);wr(p+24,out.n);wr(p+28,image_memory);wr(p+32,mf.major);wr(p+36,mf.minor);wr(p+40,mf.version);
    wr(p+48,seg);wr(p+52,segments);wr(p+56,ex);wr(p+60,mf.ne);wr(p+64,im);wr(p+68,mf.ni);wr(p+72,re);wr(p+76,nrel);wr(p+80,de);wr(p+84,mf.nd);wr(p+88,st);wr(p+92,strings.n);wr(p+96,init);wr(p+100,fini);
    U at=0;for(U g=0;g<4;g++)if(group_size[g]){B *r=p+seg+at++*24u;wr(r,group_start[g]);wr(r+4,align_to(group_size[g],4096));wr(r+8,payload[g]);wr(r+12,g==3?0:group_size[g]);wr(r+16,g==0?5u:g==1?1u:3u);wr(r+20,4096);}
    for(U i=0;i<nobj;i++)for(U g=0;g<3;g++){Section *s=objects[i].sec+g;if(s->bytes)memcpy(p+payload[g]+s->rva-group_start[g],objects[i].blob+s->offset,s->bytes);}
    for(U i=0;i<mf.ne;i++){Export *e=mf.exp+i;B *r=p+ex+i*20u;wr(r,e->so);wr(r+4,symbol_rva(e->symbol));wr(r+8,e->symbol->bytes);wr(r+12,e->kind);}
    for(U i=0;i<mf.ni;i++){Import *e=mf.imp+i;B *r=p+im+i*16u;wr(r,e->so);wr(r+4,e->dep);wr(r+8,e->kind);wr(r+12,e->bytes);}
    for(U i=0;i<nrel;i++){Reloc *e=relocs+i;B *r=p+re+i*20u;wr(r,e->patch);wr(r+4,e->type);wr(r+8,e->external);wr(r+12,e->target);wr(r+16,e->addend);}
    for(U i=0;i<mf.nd;i++){Dep *e=mf.dep+i;B *r=p+de+i*20u;wr(r,e->so);wr(r+4,e->major);wr(r+8,e->minor);wr(r+12,e->version);}
    memcpy(p+st,strings.p,strings.n);free(strings.p);if(scl_validate(p,out.n))fail("linked SCL failed common validator",0);return out;
}
/* SHA-256仅绑定最终image，自己实现整数压缩，不依赖宿主命令或第三方代码。 */
static U rotate(U v,U n){return (v>>n)|(v<<(32u-n));}
static void sha256(const B *p,U size,B digest[32])
{
    const U k[64]={0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    U h[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    U total=(size+9u+63u)&~63u;B block[64];U w[64];
    for(U offset=0;offset<total;offset+=64u){memset(block,0,64);for(U i=0;i<64u;i++){U at=offset+i;if(at<size)block[i]=p[at];else if(at==size)block[i]=128u;}
        if(offset+64u==total){U bits=size*8u;block[60]=(B)(bits>>24);block[61]=(B)(bits>>16);block[62]=(B)(bits>>8);block[63]=(B)bits;}
        for(U i=0;i<16u;i++)w[i]=((U)block[i*4u]<<24)|((U)block[i*4u+1u]<<16)|((U)block[i*4u+2u]<<8)|block[i*4u+3u];
        for(U i=16;i<64u;i++){U x=w[i-15u],y=w[i-2u];w[i]=w[i-16u]+(rotate(x,7)^rotate(x,18)^(x>>3))+w[i-7u]+(rotate(y,17)^rotate(y,19)^(y>>10));}
        U a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for(U i=0;i<64u;i++){U t=v+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];U z=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));v=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+z;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    for(U i=0;i<8u;i++){digest[i*4u]=(B)(h[i]>>24);digest[i*4u+1u]=(B)(h[i]>>16);digest[i*4u+2u]=(B)(h[i]>>8);digest[i*4u+3u]=(B)h[i];}
}
static Buf debug_file(Buf *image,Buf *map)
{
    U syms=0,files=0,lines=0;for(U i=0;i<nobj;i++){Object *o=objects+i;o->file_base=files;files=plus(files,o->nfile,SCO_MAX_FILE/20u);lines=plus(lines,o->nline,SCO_MAX_FILE/16u);for(U k=0;k<o->nsym;k++)if(o->symbols[k].section)syms++;}
    U sf=96u,ff=plus(sf,syms*24u,SCO_MAX_FILE),lf=plus(ff,files*20u,SCO_MAX_FILE),st=plus(lf,lines*16u,SCO_MAX_FILE);Buf out={0},strings={0};zeros(&out,st);zeros(&strings,1);
    B *p=out.p;memcpy(p,"SCDB1MIO",8);wr(p+8,1);wr(p+12,96);wr(p+16,3);wr(p+20,mf.library?2u:1u);wr(p+24,image->n);wr(p+28,nobj);sha256(image->p,image->n,p+32);
    wr(p+64,sf);wr(p+68,syms);wr(p+72,ff);wr(p+76,files);wr(p+80,lf);wr(p+84,lines);wr(p+88,st);
    const char *heading=mf.library?"SCDBMAP1MIO RVA\n":"SCDBMAP1MIO VA\n";bytes(map,heading,(U)strlen(heading));
    U si=0,fi=0,li=0,base=mf.library?0u:0x400000u;char row[512];
    for(U i=0;i<nobj;i++){Object *o=objects+i;for(U k=0;k<o->nsym;k++){Symbol *s=o->symbols+k;if(!s->section)continue;
            U address=base+symbol_rva(s);B *r=p+sf+si++*24u;wr(r,string(&strings,s->name));wr(r+4,address);wr(r+8,s->bytes);wr(r+12,s->kind);wr(r+16,s->binding);wr(r+20,i);
            int n=snprintf(row,sizeof(row),"%s %08X %u %s object=%u binding=%s\n",s->kind==1u?"FUNCTION":"OBJECT",address,s->bytes,s->name,i,s->binding?"global":"local");if(n<0||(U)n>=sizeof(row))fail("map row overflow",0);bytes(map,row,(U)n);
        }
        for(U k=0;k<o->nfile;k++){B *f=o->blob+o->files+k*16u,*r=p+ff+fi++*20u;wr(r,i);wr(r+4,string(&strings,ostr(o,rd(f),0)));wr(r+8,rd(f+4));wr(r+12,rd(f+8));}
        for(U k=0;k<o->nline;k++){B *f=o->blob+o->lines+k*16u,*r=p+lf+li++*16u;U address=base+o->sec[0].rva+rd(f+4),file=rd(f+8);wr(r,address);wr(r+4,o->file_base+file);wr(r+8,rd(f+12));wr(r+12,i);
            const char *path=ostr(o,rd(o->blob+o->files+file*16u),0);int n=snprintf(row,sizeof(row),"LINE %08X %s:%u object=%u\n",address,path,rd(f+12),i);if(n<0||(U)n>=sizeof(row))fail("map row overflow",0);bytes(map,row,(U)n);}
    }
    wr(p+92,strings.n);bytes(&out,strings.p,strings.n);free(strings.p);return out;
}
static void write_file(const char *path,const B *p,U n)
{
    FILE *f=fopen(path,"wb");if(!f)fail("cannot open output",path);if(fwrite(p,1,n,f)!=n){fclose(f);fail("cannot write complete output",path);}if(fclose(f))fail("cannot close output",path);
}
static char **parse_words(char *p,U *count,U *capacity,char **words)
{
    while(*p){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;if(!*p)break;
        char *start=p,*out=p;int quote=0;if(*p=='"'){quote=1;start=out=++p;}
        while(*p && (quote?*p!='"':*p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n'))*out++=*p++;
        if(quote){if(*p!='"')fail("unfinished quoted argument",0);p++;if(*p&&*p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n')fail("characters after quoted argument",0);}
        if(*p)p++;
        *out=0;
        if(!*start)fail("empty argument",0);
        char **entry=slot((void **)&words,*count,capacity,sizeof(char *));
        *entry=start;
        (*count)++;
    }
    return words;
}
static char *resolved_path(const char *path)
{
#ifdef SCLD_HOST
    U n=(U)strlen(path);char *p=zero(n+1u);memcpy(p,path,n+1u);return p;
#else
    char canonical[128];ScU32 n=0;if(ScPathResolve(path,canonical,sizeof(canonical),&n)<0||n>=sizeof(canonical))fail("cannot resolve path",path);char *p=zero(n+2u);if(canonical[0]=='/')memcpy(p,canonical,n+1u);else{p[0]='/';memcpy(p+1,canonical,n+1u);}return p;
#endif
}
static int link_main(U argc,char **argv)
{
    if(argc<4u){fprintf(stderr,"usage: scld OUTPUT --dynamic APP.json A.SCO B.SCO ...\n       scld OUTPUT --scl LIB.json @objects.rsp\n");return 2;}
    U library=!strcmp(argv[1],"--scl");if(!library&&strcmp(argv[1],"--dynamic"))fail("expected --dynamic or --scl",argv[1]);
    char *output=resolved_path(argv[0]),*manifest=resolved_path(argv[2]);context=manifest;read_manifest(manifest,output,library);
    char map_path[256],debug_path[256];if(strlen(output)>240u)fail("output path too long for sidecars",output);snprintf(map_path,sizeof(map_path),"%s.map",output);snprintf(debug_path,sizeof(debug_path),"%s.sdbg",output);
    if(same_path(output,manifest)||same_path(map_path,manifest)||same_path(debug_path,manifest))fail("output overlaps manifest",manifest);
    for(U i=3;i<argc;i++){
        if(argv[i][0]=='@'){U n,c=0,cap=0;char *rp=resolved_path(argv[i]+1);if(same_path(rp,output)||same_path(rp,map_path)||same_path(rp,debug_path))fail("output overlaps response file",rp);B *response=read_file(rp,&n);if(n>65536u)fail("response file exceeds 64KiB",rp);char **words=parse_words((char *)response,&c,&cap,0);
            for(U k=0;k<c;k++){if(words[k][0]=='@')fail("nested response files are unsupported",rp);Object *o=slot((void **)&objects,nobj,&cobj,sizeof(Object));o->path=resolved_path(words[k]);nobj++;}free(words);
        }else{Object *o=slot((void **)&objects,nobj,&cobj,sizeof(Object));o->path=resolved_path(argv[i]);nobj++;}
    }
    if(!nobj)fail("no objects",0);
    /* 先固定对象数组，再建立owner指针；动态增长不能使符号owner指向旧块。 */
    for(U i=0;i<nobj;i++){const char *path=objects[i].path;if(same_path(path,output)||same_path(path,map_path)||same_path(path,debug_path))fail("output overlaps input object",path);read_object(objects+i,path,i);}
    context=output;resolve();layout();collect_relocs();Buf image=mf.library?scl_image():scx_image(),map={0};Buf debug=debug_file(&image,&map);
    /* 全部输入、符号、容量和共同格式校验完成前不打开任何输出。
     * SandFS没有跨三个文件的事务；SHA绑定能识别磁盘失败留下的旧副文件。 */
    context=output;write_file(debug_path,debug.p,debug.n);write_file(map_path,map.p,map.n);write_file(output,image.p,image.n);
    printf("SCLD OK %s objects=%u image=%u symbols=%u relocs=%u debug=%u\n",mf.name,nobj,image.n,rd(debug.p+68),nrel,debug.n);
    free(image.p);free(debug.p);free(map.p);return 0;
}
#include "runtime/scl_format.c"
#include "runtime/scd_format.c"
#ifdef SCLD_HOST
int main(int argc,char **argv){return link_main((U)(argc-1),argv+1);}
#else
int main(void)
{
    ScU32 length=0;char text[4096];if(ScProcessArguments(text,sizeof(text),&length)<0||length>=sizeof(text))fail("cannot read process arguments",0);
    U argc=0,capacity=0;char **argv=parse_words(text,&argc,&capacity,0);return link_main(argc,argv);
}
#endif
