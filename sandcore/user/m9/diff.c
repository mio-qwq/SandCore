#include "../SCTEXT.H"
typedef struct {u32 at,size,hash;int newline;} diff_line;
typedef struct {char *bytes;diff_line *lines;u32 count;} diff_file;
typedef struct {int kind;u32 a,b;} diff_edit;
static diff_file files[2];static int fold,space,brief,report_same,unified;static u32 context=3;
static int load(int fd,diff_file *f)
{
    u32 bytes;f->bytes=cli_slurp(fd,16777216,&bytes);f->lines=sc_alloc(8192*sizeof(diff_line));if(!f->bytes || !f->lines)return -1;
    for(u32 at=0;at<bytes;){if(f->count==8192)return -1;diff_line *line=&f->lines[f->count++];line->at=at;u32 h=2166136261u;
        while(at<bytes && f->bytes[at]!='\n'){u8 c=(u8)f->bytes[at++];if(space && (c==' '||c=='\t'||c=='\r'||c=='\v'||c=='\f'))continue;if(fold && c>='A'&&c<='Z')c+=32;h=(h^c)*16777619u;}
        line->size=at-line->at;line->newline=at<bytes;line->hash=h;if(line->newline)at++;}
    return 0;
}
static int same(u32 a,u32 b)
{
    const diff_line *x=&files[0].lines[a],*y=&files[1].lines[b];if(x->hash!=y->hash || x->newline!=y->newline)return 0;
    const char *left=files[0].bytes+x->at,*right=files[1].bytes+y->at;u32 i=0,j=0;
    for(;;){if(space){while(i<x->size && (left[i]==' '||left[i]=='\t'||left[i]=='\r'||left[i]=='\v'||left[i]=='\f'))i++;while(j<y->size && (right[j]==' '||right[j]=='\t'||right[j]=='\r'||right[j]=='\v'||right[j]=='\f'))j++;}
        if(i==x->size || j==y->size)return i==x->size && j==y->size;u8 c=(u8)left[i++],d=(u8)right[j++];if(fold){if(c>='A'&&c<='Z')c+=32;if(d>='A'&&d<='Z')d+=32;}if(c!=d)return 0;}
}
static int diagonal(const int *trace,int d,int k){return trace[d*(d+1)/2+(k+d)/2];}
static int compare(diff_edit **out,u32 *amount)
{
    int n=(int)files[0].count,m=(int)files[1].count,limit=n+m;if(limit>2048)limit=2048;
    int *trace=sc_alloc((u32)(limit+1)*(u32)(limit+2)/2*sizeof(int));diff_edit *edits=sc_alloc((u32)(n+m+1)*sizeof(diff_edit));
    if(!trace || !edits){if(trace)sc_free(trace);if(edits)sc_free(edits);return -1;}int final=-1;
    /* Myers最短编辑路径；最多8192行/文件、2048项编辑差异。轨迹仅
     * 存每层实际奇偶对角线，约8MiB上界；容量超出明确返回2，不把
     * 启发式相似度或截断结果当成准确diff。相同行哈希后仍比正文。 */
    for(int d=0;d<=limit && final<0;d++){for(int k=-d;k<=d;k+=2){int x;
            if(!d)x=0;else if(k==-d || (k!=d && diagonal(trace,d-1,k-1)<diagonal(trace,d-1,k+1)))x=diagonal(trace,d-1,k+1);
            else x=diagonal(trace,d-1,k-1)+1;int y=x-k;
            while(x<n && y<m && x>=0 && y>=0 && same((u32)x,(u32)y)){x++;y++;}trace[d*(d+1)/2+(k+d)/2]=x;
            if(x==n && y==m){final=d;break;}}sc_yield();}
    if(final<0){sc_free(trace);sc_free(edits);return -1;}u32 used=0;int x=n,y=m;
    for(int d=final;d>0;d--){int k=x-y,previous;
        if(k==-d || (k!=d && diagonal(trace,d-1,k-1)<diagonal(trace,d-1,k+1)))previous=k+1;else previous=k-1;
        int oldx=diagonal(trace,d-1,previous),oldy=oldx-previous;
        while(x>oldx && y>oldy){x--;y--;edits[used++]=(diff_edit){0,(u32)x,(u32)y};}
        if(x==oldx){y--;edits[used++]=(diff_edit){1,(u32)x,(u32)y};}else {x--;edits[used++]=(diff_edit){-1,(u32)x,(u32)y};}
    }
    while(x>0 && y>0){x--;y--;edits[used++]=(diff_edit){0,(u32)x,(u32)y};}
    for(u32 i=0;i<used/2;i++){diff_edit e=edits[i];edits[i]=edits[used-1-i];edits[used-1-i]=e;}sc_free(trace);*out=edits;*amount=used;return final!=0;
}
static int line_print(int file,u32 row,const char *prefix)
{diff_line *line=&files[file].lines[row];if(cli_text(1,prefix)<0 || cli_write(1,files[file].bytes+line->at,line->size)<0 || cli_text(1,"\n")<0)return -1;
    return !line->newline && cli_text(1,"\\ No newline at end of file\n")<0?-1:0;}
static void range(u32 first,u32 count,int unified_range)
{if(unified_range){text_unsigned(1,count?first+1:first);cli_text(1,",");text_unsigned(1,count);}else {text_unsigned(1,count?first+1:first);if(count>1){cli_text(1,",");text_unsigned(1,first+count);}}}
static int normal(diff_edit *edits,u32 amount)
{
    u32 a=0,b=0;for(u32 i=0;i<amount;){if(!edits[i].kind){a++;b++;i++;continue;}u32 begin=i,old_a=a,old_b=b;
        while(i<amount && edits[i].kind){if(edits[i].kind<0)a++;else b++;i++;}range(old_a,a-old_a,0);cli_text(1,a==old_a?"a":b==old_b?"d":"c");range(old_b,b-old_b,0);cli_text(1,"\n");
        for(u32 j=begin;j<i;j++)if(edits[j].kind<0 && line_print(0,edits[j].a,"< ")<0)return -1;if(a>old_a && b>old_b)cli_text(1,"---\n");
        for(u32 j=begin;j<i;j++)if(edits[j].kind>0 && line_print(1,edits[j].b,"> ")<0)return -1;
    }return 0;
}
static int unified_print(diff_edit *edits,u32 amount,const char *left,const char *right)
{
    cli_text(1,"--- ");cli_text(1,left);cli_text(1,"\n+++ ");cli_text(1,right);cli_text(1,"\n");
    u32 scan=0,a=0,b=0;while(scan<amount){u32 changed=scan;while(changed<amount && !edits[changed].kind)changed++;if(changed==amount)break;
        u32 begin=changed-scan>context?changed-context:scan;for(u32 i=scan;i<begin;i++){if(edits[i].kind<=0)a++;if(edits[i].kind>=0)b++;}
        u32 end=changed,last=changed;while(end<amount){if(edits[end].kind)last=end;
            if(end>last && end-last>context){u32 next=end;while(next<amount && !edits[next].kind)next++;if(next==amount || next-last>context*2+1)break;}end++;}
        u32 old_a=a,old_b=b;for(u32 i=begin;i<end;i++){if(edits[i].kind<=0)a++;if(edits[i].kind>=0)b++;}
        cli_text(1,"@@ -");range(old_a,a-old_a,1);cli_text(1," +");range(old_b,b-old_b,1);cli_text(1," @@\n");
        for(u32 i=begin;i<end;i++){diff_edit e=edits[i];if(line_print(e.kind>0?1:0,e.kind>0?e.b:e.a,e.kind<0?"-":e.kind>0?"+":" ")<0)return -1;}scan=end;
    }return 0;
}
int main(void)
{
    if(cli_parse()<2)return 2;int at=0;for(;at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1];at++){
        if(equal(cli_argv[at],"--")){at++;break;}if(equal(cli_argv[at],"-U")){if(++at==cli_argc || text_number(cli_argv[at],&context)<0 || context>8192)return 2;unified=1;continue;}
        for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];if(c=='i')fold=1;else if(c=='w')space=1;else if(c=='q')brief=1;else if(c=='s')report_same=1;else if(c=='u')unified=1;else return 2;}}
    if(cli_argc-at!=2 || (equal(cli_argv[at],"-") && equal(cli_argv[at+1],"-")))return 2;int result=2;
    for(int i=0;i<2;i++){int fd=cli_input(at+i);if(fd<0)goto done;int r=load(fd,&files[i]);if(fd)sc_stream_close(fd,0);if(r<0)goto done;}
    diff_edit *edits=0;u32 amount;int r=compare(&edits,&amount);if(r<0){cli_error("diff: capacity/read",-1);goto done;}result=r;
    if(!r && report_same){cli_text(1,"Files are identical\n");}
    else if(r){if(brief)cli_text(1,"Files differ\n");else if((unified?unified_print(edits,amount,cli_argv[at],cli_argv[at+1]):normal(edits,amount))<0)result=2;}
    sc_free(edits);
done:for(int i=0;i<2;i++){if(files[i].bytes)sc_free(files[i].bytes);if(files[i].lines)sc_free(files[i].lines);}return result;
}
