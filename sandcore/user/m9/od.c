#include "../SCTEXT.H"
/* 自写流式二进制查看器；连续输入文件视作一个字节流。仅按本次
 * skip/length/行宽请求输入，-N不会预读掉调用者后续要用的stdin。 */
typedef struct {char kind;u32 bytes;} od_format;
static od_format formats[8];static u32 format_count,width=16;
static char address_kind='o';static int verbose,input_index,input_fd=-1,input_done;
static char output[4096];static u32 output_size;
static int od_put(char c)
{if(output_size==sizeof(output))return -1;output[output_size++]=c;return 0;}
static int od_text(const char *s){while(*s)if(od_put(*s++)<0)return -1;return 0;}
static int od_value(u32 n,u32 radix,u32 digits,int negative)
{
    const char *alphabet="0123456789abcdef";char reversed[33];u32 count=0;do{reversed[count++]=alphabet[n%radix];n/=radix;}while(n);
    if(negative && od_put('-')<0)return -1;if(count>digits)digits=count;for(u32 i=count;i<digits;i++)if(od_put('0')<0)return -1;
    while(count)if(od_put(reversed[--count])<0)return -1;return 0;
}
static int od_address(u32 value)
{if(address_kind=='n')return 0;return od_value(value,address_kind=='x'?16:address_kind=='d'?10:8,address_kind=='x'?8:7,0);}
static int od_add_format(const char *text)
{
    while(*text){if(format_count==8)return -1;char kind=*text++;u32 bytes=kind=='c'||kind=='a'?1:4;
        if(kind!='a'&&kind!='c'&&kind!='d'&&kind!='u'&&kind!='o'&&kind!='x')return -1;
        if(*text>='0'&&*text<='9'){bytes=(u32)(*text++-'0');if(bytes!=1&&bytes!=2&&bytes!=4)return -1;}
        if((kind=='a'||kind=='c') && bytes!=1)return -1;formats[format_count++]=(od_format){kind,bytes};}
    return 0;
}
static int od_count(const char *text,u32 *out)
{
    u32 n=0,base=10;int digits=0;if(text[0]=='0' && (text[1]=='x'||text[1]=='X')){base=16;text+=2;}
    while(*text){u32 d=*text>='0'&&*text<='9'?(u32)(*text-'0'):*text>='a'&&*text<='f'?(u32)(*text-'a'+10):*text>='A'&&*text<='F'?(u32)(*text-'A'+10):base;
        if(d>=base)break;if(n>(0xFFFFFFFFu-d)/base)return -1;n=n*base+d;digits=1;text++;}
    u32 multiplier=1;if(*text){char c=*text++;if(c=='b')multiplier=512;else if(c=='k'||c=='K')multiplier=1024;else if(c=='m'||c=='M')multiplier=1048576;else return -1;}
    if(!digits || *text || n>0xFFFFFFFFu/multiplier)return -1;*out=n*multiplier;return 0;
}
static int od_pull(u8 *bytes,u32 wanted)
{
    u32 used=0;while(used<wanted && !input_done){if(input_fd<0){if(input_index==cli_argc){input_done=1;break;}input_fd=cli_input(input_index++);if(input_fd<0)return -1;}
        int n=cli_read(input_fd,bytes+used,wanted-used);if(n<0)return -1;if(!n){if(input_fd)sc_stream_close(input_fd,0);input_fd=-1;continue;}used+=(u32)n;}
    return (int)used;
}
static int od_char(u8 c,char mode)
{
    static const char *names[33]={"nul","soh","stx","etx","eot","enq","ack","bel","bs","ht","nl","vt","ff","cr","so","si","dle","dc1","dc2","dc3","dc4","nak","syn","etb","can","em","sub","esc","fs","gs","rs","us","sp"};
    if(mode=='a'){c&=127;if(c<=32)return od_text(names[c]);if(c==127)return od_text("del");return od_put((char)c);}
    if(c==0)return od_text("\\0");if(c=='\n')return od_text("\\n");if(c=='\r')return od_text("\\r");if(c=='\t')return od_text("\\t");if(c=='\b')return od_text("\\b");
    if(c>=32 && c<=126)return od_put((char)c);if(od_put('\\')<0)return -1;return od_value(c,8,3,0);
}
static int od_row(const u8 *bytes,u32 count,u32 offset)
{
    output_size=0;for(u32 row=0;row<format_count;row++){if(row){if(address_kind!='n')for(int i=0;i<8;i++)if(od_put(' ')<0)return -1;}else if(od_address(offset)<0)return -1;
        od_format f=formats[row];for(u32 at=0;at<count;at+=f.bytes){if(od_put(' ')<0)return -1;
            if(f.kind=='c'||f.kind=='a'){if(od_char(bytes[at],f.kind)<0)return -1;continue;}u32 value=0;for(u32 i=0;i<f.bytes;i++)if(at+i<count)value|=(u32)bytes[at+i]<<(8*i);
            int negative=0;u32 digits=0,radix=f.kind=='x'?16:f.kind=='o'?8:10;if(f.kind=='d'){u32 sign=1u<<(f.bytes*8-1);negative=(value&sign)!=0;if(negative){u32 mask=f.bytes==4?0xFFFFFFFFu:(1u<<(f.bytes*8))-1;value=(0u-value)&mask;}}
            if(f.kind=='x')digits=f.bytes*2;else if(f.kind=='o')digits=(f.bytes*8+2)/3;if(od_value(value,radix,digits,negative)<0)return -1;}
        if(od_put('\n')<0)return -1;}
    return cli_write(1,output,output_size)<0?-1:0;
}
int main(void)
{
    if(cli_parse()<0)return 2;u32 skip=0,limit=0xFFFFFFFFu;int at=0,limited=0;
    for(;at<cli_argc;at++){const char *option=cli_argv[at];if(equal(option,"--")){at++;break;}if(option[0]!='-' || !option[1])break;
        if(equal(option,"-v")){verbose=1;continue;}
        if(equal(option,"-b") || equal(option,"-c") || equal(option,"-d") || equal(option,"-o") || equal(option,"-x") || equal(option,"-s")){
            const char *format=option[1]=='b'?"o1":option[1]=='c'?"c":option[1]=='d'?"u2":option[1]=='o'?"o2":option[1]=='s'?"d2":"x2";if(od_add_format(format)<0)return 2;continue;}
        char kind=option[1];if(kind!='A'&&kind!='j'&&kind!='N'&&kind!='t'&&kind!='w')return 2;const char *value=option+2;if(!*value){if(++at==cli_argc)return 2;value=cli_argv[at];}
        if(kind=='A'){if(value[1] || (*value!='d'&&*value!='o'&&*value!='x'&&*value!='n'))return 2;address_kind=*value;}
        else if(kind=='t'){if(!*value || od_add_format(value)<0)return 2;}
        else {u32 number;if(od_count(value,&number)<0)return 2;if(kind=='j')skip=number;else if(kind=='N'){limit=number;limited=1;}else {if(!number || number>64)return 2;width=number;}}}
    if(!format_count && od_add_format("o2")<0)return 2;input_index=at;if(at==cli_argc)input_fd=0;
    u8 bytes[4096],previous[64];u32 offset=0,previous_size=0;int repeated=0,result=0;
    while(skip){u32 wanted=skip>sizeof(bytes)?sizeof(bytes):skip;int n=od_pull(bytes,wanted);if(n<=0){result=cli_error("od: skip/read",-1);goto done;}skip-=(u32)n;offset+=(u32)n;}
    while(limit){u32 wanted=limit<width?limit:width;int n=od_pull(bytes,wanted);if(n<0){result=cli_error("od: read",n);goto done;}if(!n)break;
        if((u32)n>0xFFFFFFFFu-offset){result=cli_error("od: address overflow",-1);goto done;}int same=!verbose && (u32)n==width && previous_size==width;
        for(u32 i=0;i<(u32)n && same;i++)if(bytes[i]!=previous[i])same=0;
        if(same){if(!repeated && cli_text(1,"*\n")<0){result=1;goto done;}repeated=1;}
        else {if(od_row(bytes,(u32)n,offset)<0){result=1;goto done;}for(u32 i=0;i<(u32)n;i++)previous[i]=bytes[i];previous_size=(u32)n;repeated=0;}
        offset+=(u32)n;if(limited)limit-=(u32)n;
    }
    /* -An既关闭行首地址也关闭末尾地址行，不能额外输出空白行。 */
    output_size=0;if(address_kind!='n' && (od_address(offset)<0 || od_put('\n')<0 || cli_write(1,output,output_size)<0))result=1;
done:
    if(input_fd>0)sc_stream_close(input_fd,0);return result;
}
