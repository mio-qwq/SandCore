#include "../SCTEXT.H"
#define PATCH_LIMIT 0x01000000u
#define PATCH_LINES 262144u
typedef struct {u32 at,size;int newline;} patch_line;
typedef struct {char *text;patch_line *lines;u32 bytes,count;} patch_file;
static patch_file source_file,patch_file_data;
static u8 *result_text;static u32 result_size,result_capacity,result_lines;
static int reverse_patch,dry_run,result_ended;static u32 strip;
static int load(int fd,patch_file *f)
{
    f->text=cli_slurp(fd,PATCH_LIMIT,&f->bytes);if(!f->text)return -1;u32 lines=0;
    for(u32 i=0;i<f->bytes;i++)if(f->text[i]=='\n')lines++;if(f->bytes && f->text[f->bytes-1]!='\n')lines++;
    if(lines>PATCH_LINES)return -2;f->lines=sc_alloc((lines?lines:1)*sizeof(patch_line));if(!f->lines)return -4;
    for(u32 at=0;at<f->bytes;){patch_line *line=f->lines+f->count++;line->at=at;while(at<f->bytes && f->text[at]!='\n')at++;line->size=at-line->at;line->newline=at<f->bytes;if(line->newline)at++;}return 0;
}
static int prefix(patch_file *f,u32 row,const char *text)
{u32 size=(u32)length(text);return row<f->count && size<=f->lines[row].size && !text_compare(f->text+f->lines[row].at,size,text,size,0);}
static int emit(const void *text,u32 size,int newline)
{
    if(result_ended)return -1;u32 bytes=size+(u32)newline;if(bytes>PATCH_LIMIT-result_size)return -2;
    if(result_size+bytes>result_capacity){u32 capacity=result_capacity?result_capacity:8192;while(capacity<result_size+bytes)capacity*=2;
        u8 *fresh=sc_alloc(capacity);if(!fresh)return -4;for(u32 i=0;i<result_size;i++)fresh[i]=result_text[i];if(result_text)sc_free(result_text);result_text=fresh;result_capacity=capacity;}
    for(u32 i=0;i<size;i++)result_text[result_size++]=((const u8 *)text)[i];if(newline)result_text[result_size++]='\n';else result_ended=1;result_lines++;return 0;
}
static int copy_line(u32 row)
{patch_line *line=source_file.lines+row;return emit(source_file.text+line->at,line->size,line->newline);}
static int number(const char **cursor,const char *end,u32 *out)
{const char *p=*cursor;u32 n=0;int seen=0;while(p<end && *p>='0' && *p<='9'){u32 d=(u32)(*p++-'0');if(n>(0xFFFFFFFFu-d)/10)return -1;n=n*10+d;seen=1;}if(!seen)return -1;*cursor=p;*out=n;return 0;}
static int range(const char **cursor,const char *end,u32 *first,u32 *count)
{
    if(number(cursor,end,first)<0)return -1;*count=1;if(*cursor<end && **cursor==','){(*cursor)++;if(number(cursor,end,count)<0)return -1;}
    if(*count && !*first)return -1;return 0;
}
static int header(u32 row,u32 *old_at,u32 *old_count,u32 *new_at,u32 *new_count)
{
    patch_line *line=patch_file_data.lines+row;const char *p=patch_file_data.text+line->at,*end=p+line->size;
    if(end-p<9 || p[0]!='@'||p[1]!='@'||p[2]!=' '||p[3]!='-')return -1;p+=4;
    u32 a,an,b,bn;if(range(&p,end,&a,&an)<0 || end-p<2 || *p++!=' ' || *p++!='+')return -1;
    if(range(&p,end,&b,&bn)<0 || end-p<3 || p[0]!=' '||p[1]!='@'||p[2]!='@')return -1;
    if(reverse_patch){u32 n=a;a=b;b=n;n=an;an=bn;bn=n;}
    *old_at=an?a-1:a;*old_count=an;*new_at=bn?b-1:b;*new_count=bn;return 0;
}
static int path_header(u32 row,const char *prefix_text,char out[64])
{
    if(!prefix(&patch_file_data,row,prefix_text))return -1;patch_line *line=patch_file_data.lines+row;
    const char *p=patch_file_data.text+line->at+4;u32 size=line->size-4;for(u32 i=0;i<size;i++)if(p[i]=='\t' || p[i]=='\r'){size=i;break;}
    if(size==9 && !text_compare(p,size,"/dev/null",9,0))return -1;
    u32 at=0;for(u32 i=0;i<strip;i++){while(at<size && p[at]!='/')at++;if(at==size)return -1;at++;}
    if(at==size || size-at>=64)return -1;for(u32 i=at;i<size;i++){if((u8)p[i]<32)return -1;out[i-at]=p[i];}out[size-at]=0;return 0;
}
static int apply(u32 first)
{
    u32 cursor=0,row=first,hunks=0;
    /* 精确核对坐标、上下文、删除正文与末行LF；不作fuzz/猜偏移。
     * 所有hunk先在私有候选中完成，任何格式/上下文错误不改磁盘。 */
    while(row<patch_file_data.count){if(!prefix(&patch_file_data,row,"@@ "))return -1;
        u32 at,removed,to,added;if(header(row,&at,&removed,&to,&added)<0 || at<cursor || at>source_file.count || removed>source_file.count-at)return -1;
        while(cursor<at){int r=copy_line(cursor++);if(r<0)return r;}if(to!=result_lines)return -1;row++;
        u32 old_seen=0,new_seen=0;
        while(old_seen<removed || new_seen<added){if(row==patch_file_data.count)return -1;patch_line line=patch_file_data.lines[row++];if(!line.size)return -1;
            char sign=patch_file_data.text[line.at];if(sign!=' ' && sign!='-' && sign!='+')return -1;if(reverse_patch && sign!=' ')sign=sign=='+'?'-':'+';
            line.at++;line.size--;int newline=1;if(prefix(&patch_file_data,row,"\\ No newline at end of file")){newline=0;row++;}
            if(sign!='+'){if(old_seen==removed || cursor==source_file.count)return -1;patch_line *old=source_file.lines+cursor;
                if(old->newline!=newline || text_compare(source_file.text+old->at,old->size,patch_file_data.text+line.at,line.size,0))return -1;old_seen++;cursor++;}
            if(sign!='-'){if(new_seen==added)return -1;int r=emit(patch_file_data.text+line.at,line.size,newline);if(r<0)return r;new_seen++;}
        }
        hunks++;sc_yield();
    }
    if(!hunks)return -1;while(cursor<source_file.count){int r=copy_line(cursor++);if(r<0)return r;}return 0;
}
static void release(patch_file *f){if(f->text)sc_free(f->text);if(f->lines)sc_free(f->lines);}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;
    for(;at<cli_argc;at++){const char *arg=cli_argv[at];if(equal(arg,"--")){at++;break;}if(equal(arg,"-R"))reverse_patch=1;
        else if(equal(arg,"--dry-run"))dry_run=1;else if(equal(arg,"-p")){if(++at==cli_argc || text_number(cli_argv[at],&strip)<0 || strip>32)return 2;}
        else if(arg[0]=='-' && arg[1]=='p' && arg[2]){if(text_number(arg+2,&strip)<0 || strip>32)return 2;}
        else if(arg[0]=='-')return 2;else break;}
    if(cli_argc-at>2)return 2;const char *named=at<cli_argc?cli_argv[at]:0;int input=at+1<cli_argc?cli_input(at+1):0;
    if(input<0)return cli_error("patch: input",input);int r=load(input,&patch_file_data);if(input)sc_stream_close(input,0);int source=-1,output=-1,result=1;
    if(r<0)goto done;u32 row=0;while(row<patch_file_data.count && !prefix(&patch_file_data,row,"--- "))row++;
    if(row+1>=patch_file_data.count || !prefix(&patch_file_data,row+1,"+++ "))goto done;
    char left[64],right[64];if(path_header(row,"--- ",left)<0 || path_header(row+1,"+++ ",right)<0)goto done;
    if(equal(left,"/dev/null")||equal(right,"/dev/null"))goto done;
    const char *target=named?named:reverse_patch?right:left;source=sc_stream_open(target,1,0);if(source<0)goto done;
    if(load(source,&source_file)<0 || apply(row+2)<0)goto done;
    if(!dry_run){output=sc_stream_open(target,2,result_size);if(output<0)goto done;
        /* 事务已绑定当前目标代数，再核对原输入流代数；夹在两个调用
         * 间的改写也会被seek或commit拒绝，不能用陈旧正文覆盖新文件。 */
        if(sc_stream_seek(source,0)<0 || cli_write(output,result_text,result_size)<0)goto done;
        r=sc_stream_close(output,1);if(r<0)goto done;output=-1;}
    cli_text(1,dry_run?"patch: applicable\n":"patch: applied\n");result=0;
done:
    if(output>=0)sc_stream_close(output,0);if(source>=0)sc_stream_close(source,0);release(&patch_file_data);release(&source_file);if(result_text)sc_free(result_text);
    if(result)cli_text(2,"patch: format/context/read/write failure; original preserved\n");return result;
}
