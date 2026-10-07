#include "../SCTEXT.H"
typedef struct {u32 row[16],total,delta,denominator,valid,score;} top_row;
typedef struct {top_row *rows;u32 *order,count,capacity;} top_snapshot;
static top_snapshot previous,current;
static u32 old_global[5];
static int old_valid;

static u32 ratio(u32 numerator,u32 denominator,u32 scale)
{
    if(!denominator)return 0;
    /* 二进制逐位乘除，保留精确整数结果。不要先n*10000再除：
     * PIT长时间运行后32位乘积可能回绕，也不依赖用户态64位除法库。 */
    u32 whole=numerator/denominator,part=numerator%denominator,q=0,r=0;
    for(int bit=13;bit>=0;bit--){
        q*=2;if(r>=denominator-r){r-=denominator-r;q++;}else r*=2;
        if(scale&(1u<<bit)){q+=whole;if(r>=denominator-part){r-=denominator-part;q++;}else r+=part;}
    }
    return q;
}
static int reserve(top_snapshot *snapshot)
{
    if(snapshot->count<snapshot->capacity)return 0;
    u32 capacity=snapshot->capacity?snapshot->capacity*2:32;
    if(capacity<snapshot->capacity || capacity>0x7FFFFFFFu/(sizeof(top_row)+4))return -1;
    top_row *rows=sc_alloc(capacity*(sizeof(top_row)+4));if(!rows)return -1;
    for(u32 i=0;i<snapshot->count;i++)rows[i]=snapshot->rows[i];
    if(snapshot->rows)sc_free(snapshot->rows);
    snapshot->rows=rows;snapshot->order=(u32 *)(rows+capacity);snapshot->capacity=capacity;return 0;
}
static int better(u32 a,u32 b)
{
    top_row *left=current.rows+a,*right=current.rows+b;
    return left->score>right->score || (left->score==right->score && left->row[0]<right->row[0]);
}
static void sift(u32 root,u32 count)
{
    u32 value=current.order[root];
    while(root<count/2){u32 child=root*2+1;
        if(child+1<count && better(current.order[child+1],current.order[child]))child++;
        if(!better(current.order[child],value))break;
        current.order[root]=current.order[child];root=child;
    }
    current.order[root]=value;
}
static void sort(void)
{
    /* 快照正文保持PID顺序以线性匹配上一代；只排序独立索引，
     * 数千任务不再用32项插入排序，也不把旧PID计数算到复用者上。 */
    for(u32 i=0;i<current.count;i++)current.order[i]=i;
    for(u32 i=current.count/2;i>0;i--)sift(i-1,current.count);
    for(u32 end=current.count;end>1;end--){
        u32 value=current.order[0];current.order[0]=current.order[end-1];current.order[end-1]=value;sift(0,end-1);
    }
}
static int collect(u32 global[5])
{
    u32 page[SC_PROCESS_PAGE_WORDS],cursor=0xFFFFFFFFu,old=0;int first=1;
    current.count=0;
    do{
        if(sc_process_page(page,SC_PROCESS_PAGE_WORDS,cursor)<0)return -1;
        if(first){for(u32 i=0;i<5;i++)global[i]=page[8+i];first=0;}
        for(u32 i=0;i<page[3];i++){
            u32 *source=page+16+i*16;if(!source[0] || !source[1] || source[1]==2)continue;
            if(reserve(&current))return -1;top_row *row=current.rows+current.count++;
            for(u32 j=0;j<16;j++)row->row[j]=source[j];row->total=page[8];
            row->delta=row->denominator=row->valid=row->score=0;
            while(old<previous.count && previous.rows[old].row[0]<source[0])old++;
            if(old<previous.count){top_row *before=previous.rows+old;
                if(before->row[0]==source[0] && before->row[2]==source[2] && before->row[11] && source[11]){
                    row->delta=source[5]-before->row[5];row->denominator=row->total-before->total;
                    row->valid=row->denominator!=0;if(row->valid)row->score=ratio(row->delta,row->denominator,10000);
                }
            }
        }
        cursor=page[4];
    }while(cursor!=0xFFFFFFFFu);
    sort();return 0;
}
static int draw(int batch,int terminal)
{
    u32 global[5],memory[48];if(collect(global)<0 || sc_monitor(memory)<0)return -1;
    if(!batch){int r;while((r=sc_terminal_clear(terminal))==-6)sc_yield();if(r<0)return -1;}
    cli_text(1,"SandCore tasks   uptime ");text_unsigned(1,(u32)sc_tick()/100);
    cli_text(1,"s\nMemory KiB: used ");text_unsigned(1,memory[3]/1024);cli_text(1," / ");text_unsigned(1,memory[2]/1024);
    cli_text(1,"   free ");text_unsigned(1,memory[4]/1024);cli_text(1,"\nCPU PIT samples: ");
    u32 total=old_valid?global[0]-old_global[0]:0;
    if(total){cli_text(1,"user ");text_unsigned(1,ratio(global[3]-old_global[3],total,100));
        cli_text(1,"%  kernel ");text_unsigned(1,ratio(global[2]-old_global[2],total,100));
        cli_text(1,"%  idle ");text_unsigned(1,ratio(global[1]-old_global[1],total,100));cli_text(1,"%\n");}
    else cli_text(1,"waiting for interval\n");
    u32 info[8],rows=current.count;
    if(!batch && sc_terminal_info2(terminal,info)>=0 && info[3]>6 && rows>info[3]-6)rows=info[3]-6;
    cli_text(1,"Showing ");text_unsigned(1,rows);cli_text(1," / ");text_unsigned(1,current.count);
    cli_text(1," tasks; -b prints all\nPID  UID  GID  STATE  CPU%  SAMPLES  NAME\n");
    for(u32 k=0;k<rows;k++){
        top_row *entry=current.rows+current.order[current.count-1-k];u32 *row=entry->row;
        text_unsigned(1,row[0]);cli_text(1,"  ");cli_number(1,(int)row[3]);cli_text(1,"  ");cli_number(1,(int)row[4]);
        cli_text(1,"  ");cli_text(1,row[1]==3?"pause":"run");cli_text(1,"  ");
        if(row[11]){if(entry->valid)text_unsigned(1,entry->score/100);else cli_text(1,"-");cli_text(1,"  ");text_unsigned(1,row[5]);}
        else cli_text(1,"-  -");
        cli_text(1,"  ");cli_text(1,((char *)(row+8))[0]?(char *)(row+8):"(private)");cli_text(1,"\n");
    }
    for(u32 i=0;i<5;i++)old_global[i]=global[i];old_valid=1;
    top_snapshot swap=previous;previous=current;current=swap;return 0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int batch=0,count=-1;u32 interval=100;
    for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(equal(p,"-b"))batch=1;else if(equal(p,"-n")){if(++i==cli_argc || cli_integer(cli_argv[i],&count)<0 || count<1)return 2;}
        else if(equal(p,"-d")){u32 seconds;if(++i==cli_argc || text_number(cli_argv[i],&seconds)<0 || !seconds || seconds>86400)return 2;interval=seconds*100;}else return 2;}
    u32 info[8];int terminal=-1;if(!batch){if(sc_terminal_info2(1,info)<0 || (info[1]!=4 && info[1]!=5))return 2;terminal=sc_stream_open("",8,0);if(terminal<0)return 1;}
    if(batch && count<0)count=1;int result=0,frames=0;for(;;){u32 start=(u32)sc_tick();if(draw(batch,terminal)<0){result=1;break;}if(++frames==count)break;
        while((u32)sc_tick()-start<interval){if(terminal>=0){u8 c;int n=sc_stream_read(terminal,&c,1);if(n==0 || (n>0 && (c=='q' || c=='Q' || c==27)))goto done;if(n<0 && n!=-6){result=1;goto done;}}sc_yield();}}
done:
    if(terminal>=0)sc_stream_close(terminal,0);
    if(previous.rows)sc_free(previous.rows);if(current.rows)sc_free(current.rows);return result;
}
