#include "../SCTEXT.H"
typedef struct {char *bytes;u32 length;} row;
static int reverse,numeric,fold,unique;
static int comparison(row a,row b)
{
    int value=0;if(numeric){int x=0,y=0;char left[32],right[32];u32 n=a.length<31?a.length:31,m=b.length<31?b.length:31;
        for(u32 i=0;i<n;i++)left[i]=a.bytes[i];left[n]=0;for(u32 i=0;i<m;i++)right[i]=b.bytes[i];right[m]=0;
        if(cli_integer(left,&x)<0)x=0;if(cli_integer(right,&y)<0)y=0;value=x<y?-1:x>y?1:0;}
    if(!value)value=text_compare(a.bytes,a.length,b.bytes,b.length,fold);return reverse?-value:value;
}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){
        for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];if(c=='r')reverse=1;else if(c=='n')numeric=1;else if(c=='f')fold=1;else if(c=='u')unique=1;else return 2;}at++;}
    /* 有界内存稳定归并：8192行/正文16MiB，O(n log n)比较。达到
     * 容量直接失败，不能只排序前半文件；外排扩展可沿事务流增加。 */
    row *rows=sc_alloc(8192*sizeof(row)),*spare=sc_alloc(8192*sizeof(row));if(!rows || !spare)return 1;
    u32 count=0,total=0;int result=0;
    for(int argument=at;argument<cli_argc || (argument==at && at==cli_argc);argument++){
        int fd=cli_input(argument);if(fd<0){result=1;break;}text_reader r={fd,{0},0,0};text_line l={0,0,0,0};int n;
        while((n=text_line_read(&r,&l))>0){if(count==8192 || l.size>0x1000000u-total){result=1;break;}
            char *bytes=sc_alloc(l.size+1);if(!bytes){result=1;break;}for(u32 i=0;i<l.size;i++)bytes[i]=l.bytes[i];bytes[l.size]=0;
            rows[count++]=(row){bytes,l.size};total+=l.size;}
        if(n<0)result=1;text_line_free(&l);if(fd)sc_stream_close(fd,0);if(result)break;
    }
    for(u32 width=1;width<count && !result;width*=2){
        for(u32 base=0;base<count;base+=width*2){u32 middle=base+width;if(middle>count)middle=count;u32 end=base+width*2;if(end>count)end=count;
            u32 left=base,right=middle;for(u32 i=base;i<end;i++)spare[i]=left<middle && (right==end || comparison(rows[left],rows[right])<=0)?rows[left++]:rows[right++];}
        row *swap=rows;rows=spare;spare=swap;sc_yield();
    }
    for(u32 i=0;i<count && !result;i++){if(unique && i && !comparison(rows[i-1],rows[i]))continue;
        if(cli_write(1,rows[i].bytes,rows[i].length)<0 || cli_text(1,"\n")<0)result=1;}
    for(u32 i=0;i<count;i++)sc_free(rows[i].bytes);sc_free(rows);sc_free(spare);return result;
}
