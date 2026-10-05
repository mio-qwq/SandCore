#ifndef SCTEXT_H
#define SCTEXT_H
#include "SCIO.H"
/* 文本工具按流读行；单行超过64KiB明确失败，不能截短后给出似乎
 * 完整的排序/过滤结果。长度显式保存，支持行内NUL和末行无LF。 */
typedef struct {int fd;u8 bytes[4096];u32 position,size;} text_reader;
typedef struct {char *bytes;u32 size,capacity;int newline;} text_line;
static int text_next(text_reader *r)
{
    if(r->position==r->size){int n=cli_read(r->fd,r->bytes,sizeof(r->bytes));if(n<=0)return n? n:-256;r->position=0;r->size=(u32)n;}
    return r->bytes[r->position++];
}
static int text_line_read(text_reader *r,text_line *line)
{
    line->size=0;line->newline=0;
    for(;;){int c=text_next(r);if(c<0)return c==-256?(line->size?1:0):-1;
        if(c=='\n'){line->newline=1;return 1;}
        if(line->size==65536)return -2;
        if(line->size==line->capacity){u32 next=line->capacity?line->capacity*2:256;char *fresh=sc_alloc(next);
            if(!fresh)return -4;for(u32 i=0;i<line->size;i++)fresh[i]=line->bytes[i];if(line->bytes)sc_free(line->bytes);line->bytes=fresh;line->capacity=next;}
        line->bytes[line->size++]=(char)c;
    }
}
static int text_line_write(int fd,const text_line *line)
{if(cli_write(fd,line->bytes,line->size)<0)return -1;return line->newline?cli_text(fd,"\n")<0?-1:0:0;}
static void text_line_free(text_line *line){if(line->bytes)sc_free(line->bytes);line->bytes=0;line->capacity=0;}
static int text_compare(const char *a,u32 an,const char *b,u32 bn,int fold)
{
    u32 n=an<bn?an:bn;for(u32 i=0;i<n;i++){u8 x=(u8)a[i],y=(u8)b[i];
        if(fold){if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;}if(x!=y)return x<y?-1:1;}return an<bn?-1:an>bn?1:0;
}
static int text_contains(const char *s,u32 n,const char *find,int fold)
{
    u32 m=(u32)length(find);if(m>n)return 0;
    for(u32 i=0;i<=n-m;i++)if(!text_compare(s+i,m,find,m,fold))return 1;return 0;
}
static void text_unsigned(int fd,u32 n)
{char digits[11];u32 used=0;do{digits[used++]=(char)('0'+n%10);n/=10;}while(n);for(u32 i=0;i<used/2;i++){char c=digits[i];digits[i]=digits[used-1-i];digits[used-1-i]=c;}digits[used]=0;cli_text(fd,digits);}
static void text_hex(int fd,u32 value,int width)
{const char *digits="0123456789abcdef";char out[9];for(int i=width-1;i>=0;i--){out[i]=digits[value&15];value>>=4;}out[width]=0;cli_text(fd,out);}
static int text_number(const char *s,u32 *out)
{if(!*s)return -1;u32 n=0;while(*s){if(*s<'0'||*s>'9')return -1;u32 d=(u32)(*s++-'0');if(n>(0xFFFFFFFFu-d)/10)return -1;n=n*10+d;}*out=n;return 0;}
static int text_files(int at,int (*run)(int,const char *))
{
    if(at==cli_argc)return run(0,"-");int result=0;
    for(int i=at;i<cli_argc;i++){int fd=cli_input(i);if(fd<0){cli_error(cli_argv[i],fd);result=1;continue;}
        if(run(fd,cli_argv[i]))result=1;if(fd)sc_stream_close(fd,0);}return result;
}
/* 选择表达式1,4-7,9-解析为闭区间，避免按65536列分配位图。 */
static int text_selected(const char *spec,u32 index)
{
    const char *p=spec;while(*p){u32 lo=0,hi=0;int digits=0;
        while(*p>='0'&&*p<='9'){if(lo>65536)return -1;lo=lo*10+(u32)(*p++-'0');digits++;}
        if(!digits || !lo)return -1;hi=lo;
        if(*p=='-'){p++;hi=0xFFFFFFFFu;if(*p>='0'&&*p<='9'){hi=0;while(*p>='0'&&*p<='9'){if(hi>65536)return -1;hi=hi*10+(u32)(*p++-'0');}if(hi<lo)return -1;}}
        if(*p && *p!=',')return -1;if(index>=lo && index<=hi)return 1;if(*p){p++;if(!*p)return -1;}
    }return 0;
}
#endif
