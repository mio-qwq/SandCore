#include "FILES2.H"
#include "files2_model.inc"

static void f2_status(const char *message){(void)message;abort();}
static ScStatus f2_places_refresh(void){abort();return 0;}
static ScStatus f2_history_refresh(void){abort();return 0;}
static void f2_gesture_cancel(ScU32 value){(void)value;abort();}

/* 独立宿主检查只调用实际排序步进器。期望由libc qsort和另一份直接
 * 字段比较产生，不模拟控件成功、不把它当成目录/文件事务客体验收。 */
static F2Entry *expected_rows;
static ScU32 expected_field,expected_descending,assertions;
static void check(ScU32 condition,const char *message)
{assertions++;if(!condition){printf("FAIL %s\n",message);exit(1);}}
static int expected_text(const char *a,const char *b)
{
    ScU32 i=0;
    while(a[i] || b[i]){unsigned char x=(unsigned char)a[i],y=(unsigned char)b[i];
        if(x>='a' && x<='z')x-=32;
        if(y>='a' && y<='z')y-=32;
        if(x!=y)return x<y?-1:1;
        i++;}return 0;
}
static int expected_compare(const void *a,const void *b)
{
    const F2Entry *x=expected_rows+*(const ScU32 *)a,*y=expected_rows+*(const ScU32 *)b;int s=0;
    if(x->kind!=y->kind)return x->kind==SC32_PATH_DIRECTORY?-1:1;
    switch(expected_field){
        case 0:s=expected_text(strrchr(x->path,'/')+1,strrchr(y->path,'/')+1);break;
        case 1:s=expected_text(x->type,y->type);break;
        case 2:s=x->bytes<y->bytes?-1:x->bytes!=y->bytes;break;
        case 3:s=x->uid<y->uid?-1:x->uid!=y->uid;if(!s)s=x->gid<y->gid?-1:x->gid!=y->gid;break;
        case 4:s=x->rw<y->rw?-1:x->rw!=y->rw;break;
        case 5:s=x->revision<y->revision?-1:x->revision!=y->revision;break;
        case 6:s=expected_text(x->path,y->path);break;
    }
    if(!s)s=expected_text(x->path,y->path);
    return expected_descending?-s:s;
}
static ScU32 expected_match(const char *path,const char *query)
{
    if(!*query)return 1;
    for(ScU32 start=0;path[start];start++){
        ScU32 i=0;while(query[i] && path[start+i]){
            char a=path[start+i],b=query[i];if(a>='A' && a<='Z')a+=32;if(b>='A' && b<='Z')b+=32;
            if(a!=b)break;
            i++;}
        if(!query[i])return 1;
    }
    return 0;
}
static void one_case(ScU32 count,ScU32 field,ScU32 descending,const char *query)
{
    F2Snapshot snapshot;memset(&snapshot,0,sizeof(snapshot));snapshot.count=count;snapshot.search=1;
    snapshot.rows=calloc(count?count:1,sizeof(F2Entry));check(snapshot.rows!=0,"rows allocation");
    ScU32 *expected=calloc(count?count:1,4),*a=calloc(count+4,4),*b=calloc(count+4,4),*r=calloc(count+4,4);
    check(expected && a && b && r,"index allocation");ScU32 seed=173,expected_count=0;
    for(ScU32 i=0;i<count;i++){F2Entry *row=snapshot.rows+i;seed=seed*1664525u+1013904223u;
        row->id=i+1;row->kind=i%7==0?SC32_PATH_DIRECTORY:SC32_PATH_FILE;
        row->bytes=seed;row->revision=seed^0xaaaaaaaa;row->rw=seed&63;
        row->uid=(ScI32)(seed^0x80000000u);row->gid=(ScI32)(seed<<1);
        snprintf(row->path,sizeof(row->path),"/HOME/项目%u/报告%08u.%s",i%13,count-i,i%3?"TXT":"C");
        strcpy(row->type,row->kind==SC32_PATH_DIRECTORY?"文件夹":i%3?"文本":"源代码");
        if(expected_match(row->path,query))expected[expected_count++]=i;}
    a[0]=a[count+3]=b[0]=b[count+3]=r[0]=r[count+3]=0x136975ab;
    memset(&f2.order,0,sizeof(f2.order));f2.order.active=1;f2.order.snapshot=&snapshot;
    f2.order.order=a+2;f2.order.scratch=b+2;f2.order.rank=r+2;f2.order.width=1;
    f2.order.field=field;f2.order.descending=descending;strcpy(f2.order.filter,query);
    ScU32 steps=0;ScStatus status=0;
    while(status==0 && steps<count*64+64){status=f2_order_advance();steps++;}
    check(status==SC_UI_END,"bounded completion");check(f2.order.count==expected_count,"filter count");
    expected_rows=snapshot.rows;expected_field=field;expected_descending=descending;
    qsort(expected,expected_count,4,expected_compare);
    for(ScU32 i=0;i<expected_count;i++){check(f2.order.order[i]==expected[i],"independent sorted index");
        check(f2.order.rank[expected[i]]==i,"inverse rank");}
    for(ScU32 i=0;i<count;i++)if(!expected_match(snapshot.rows[i].path,query))check(f2.order.rank[i]==F2_NONE,"hidden row rank");
    check(a[0]==0x136975ab && a[count+3]==0x136975ab && b[0]==0x136975ab
        && b[count+3]==0x136975ab && r[0]==0x136975ab && r[count+3]==0x136975ab,"guard words");
    free(snapshot.rows);free(expected);free(a);free(b);free(r);memset(&f2.order,0,sizeof(f2.order));
}
int main(void)
{
    const ScU32 counts[]={0,1,513,10007};const char *queries[]={"","txt","报告0000","absent"};
    ScU32 cases=0;
    for(ScU32 c=0;c<4;c++)for(ScU32 field=0;field<7;field++)for(ScU32 desc=0;desc<2;desc++){
        one_case(counts[c],field,desc,queries[(field+desc)%4]);cases++;}
    printf("FILES2_MODEL_HOST cases=%u assertions=%u max_rows=10007 guest_run=false\n",cases,assertions);return 0;
}
