#ifndef SANDCORE_HTTP_INC
#define SANDCORE_HTTP_INC
/* wget原解码器共用，新增curl只通过三个私有钩子接收原始头和总期限。
 * wget的默认宏均为常量，原收发、长度、分块和文件提交语义不变。 */
#ifndef HTTP_EXPIRED
#define HTTP_EXPIRED() 0
#endif
#ifndef HTTP_WAIT_TICKS
#define HTTP_WAIT_TICKS http_ticks
#endif
#ifndef HTTP_HEADER_LINE
#define HTTP_HEADER_LINE(line,bytes) 0
#endif
#include "NETCLI.inc"
typedef struct {char host[256],path[1024];u32 port;} http_url_t;
typedef struct {u32 status,has_length,length,chunked,no_body;char location[1024];} http_response_t;
static u8 http_input[4096];static u32 http_at,http_count,http_handle,http_ticks;
static int http_byte(void)
{if(http_at==http_count){if(HTTP_EXPIRED())return -110;int n=net_receive(http_handle,http_input,sizeof(http_input),0,HTTP_WAIT_TICKS);if(n<0)return n;if(!n)return -200;http_count=(u32)n;http_at=0;}return http_input[http_at++];}
static int http_line(char *out,u32 capacity)
{u32 n=0;for(;;){int c=http_byte();if(c<0)return c;if(c=='\r'){c=http_byte();if(c!='\n')return c<0?c:-71;out[n]=0;return (int)n;}
    if(c=='\n'||c==0 || n+1>=capacity)return -71;out[n++]=(char)c;}}
static int http_url(const char *text,http_url_t *out)
{
    const char *scheme="http://";for(u32 i=0;i<7;i++)if(text[i]!=scheme[i])return -95;text+=7;net_zero(out,sizeof(*out));out->port=80;
    u32 n=0;while(*text&&*text!=':'&&*text!='/'&&*text!='?'&&*text!='#'){
        char c=*text++;if(n>=255 || !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.'))return -22;out->host[n++]=c;}
    if(!n)return -22;
    if(*text==':'){text++;char port[6];n=0;while(*text>='0'&&*text<='9'){if(n>=5)return -22;port[n++]=*text++;}port[n]=0;if(net_u32(port,65535,&out->port)<0||!out->port)return -22;}
    if(*text&&*text!='/'&&*text!='?'&&*text!='#')return -22;n=0;
    if(*text!='/')out->path[n++]='/';while(*text&&*text!='#'){if(n>=1023 || (u8)*text<33 || (u8)*text>126)return -22;out->path[n++]=*text++;}out->path[n]=0;return 0;
}
static int http_append(char *out,u32 capacity,u32 *at,const char *text)
{while(*text){if(*at+1>=capacity)return -90;out[(*at)++]=*text++;}out[*at]=0;return 0;}
static void http_port(u32 value,char out[6])
{int n=0;do{out[n++]=(char)('0'+value%10);value/=10;}while(value);for(int i=0;i<n/2;i++){char c=out[i];out[i]=out[n-1-i];out[n-1-i]=c;}out[n]=0;}
static int http_request(http_url_t *url)
{
    char request[1600],port[6];u32 at=0;http_port(url->port,port);
    if(http_append(request,sizeof(request),&at,"GET ")<0||http_append(request,sizeof(request),&at,url->path)<0||http_append(request,sizeof(request),&at," HTTP/1.1\r\nHost: ")<0||http_append(request,sizeof(request),&at,url->host)<0)return -90;
    if(url->port!=80&&(http_append(request,sizeof(request),&at,":")<0||http_append(request,sizeof(request),&at,port)<0))return -90;
    if(http_append(request,sizeof(request),&at,"\r\nUser-Agent: SandCore-M10a1\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n")<0)return -90;
    return sc_net_send_all(http_handle,request,at,http_ticks);
}
static int http_headers(http_response_t *response,int show)
{
    char line[2048];u32 total=0;int interim=0;
    for(;;){net_zero(response,sizeof(*response));int n=http_line(line,sizeof(line));if(n<0)return n;int emitted=HTTP_HEADER_LINE(line,n);if(emitted<0)return emitted;total+=(u32)n+2;
        if(length(line)<12 || (line[0]!='H'||line[1]!='T'||line[2]!='T'||line[3]!='P'||line[4]!='/'||line[5]!='1'||line[6]!='.'||(line[7]!='0'&&line[7]!='1')||line[8]!=' '))return -71;
        if(line[9]<'1'||line[9]>'5'||line[10]<'0'||line[10]>'9'||line[11]<'0'||line[11]>'9'||(line[12]&&line[12]!=' '))return -71;
        response->status=(u32)(line[9]-'0')*100u+(u32)(line[10]-'0')*10u+(u32)(line[11]-'0');if(show){cli_text(2,line);cli_text(2,"\n");}
        for(;;){n=http_line(line,sizeof(line));if(n<0)return n;int emitted=HTTP_HEADER_LINE(line,n);if(emitted<0)return emitted;total+=(u32)n+2;if(total>16384)return -90;if(!n)break;
            if(show){cli_text(2,line);cli_text(2,"\n");}char *colon=net_find(line,':');if(!colon||colon==line)return -71;*colon++=0;while(*colon==' '||*colon=='\t')colon++;
            int end=length(colon);while(end&&(colon[end-1]==' '||colon[end-1]=='\t'))colon[--end]=0;
            if(net_ci_equal(line,"Content-Length")){u32 size;if(net_u32(colon,0xFFFFFFFFu,&size)<0 || (response->has_length&&size!=response->length))return -71;response->has_length=1;response->length=size;}
            else if(net_ci_equal(line,"Transfer-Encoding")){if(response->chunked||!net_ci_equal(colon,"chunked"))return -95;response->chunked=1;}
            else if(net_ci_equal(line,"Content-Encoding")){if(!net_ci_equal(colon,"identity"))return -95;}
            else if(net_ci_equal(line,"Location")){if(length(colon)>=1024)return -90;net_copy(response->location,colon,(u32)length(colon)+1);}
        }
        if(response->chunked&&response->has_length)return -71;
        if(response->status<200){if(response->status==101 || ++interim>4)return -95;continue;}
        response->no_body=response->status==204||response->status==304;return 0;
    }
}
static int http_redirect(http_url_t *old,const char *location,char out[1024])
{
    if(!*location)return -71;if(length(location)>=7&&location[0]=='h'&&location[1]=='t'&&location[2]=='t'&&location[3]=='p'&&location[4]==':'&&location[5]=='/'&&location[6]=='/'){
        net_copy(out,location,(u32)length(location)+1);return 0;}
    /* 相对Location沿当前目录解析，支持绝对路径/查询和同协议主机。
     * 不跟随其它scheme；HTTPS由入口清楚报告未实现。 */
    if(net_find((char *)location,':'))return -95;u32 at=0;
    if(http_append(out,1024,&at,"http:")<0)return -90;
    if(location[0]=='/'&&location[1]=='/')return http_append(out,1024,&at,location);
    char port[6];http_port(old->port,port);if(http_append(out,1024,&at,"//")<0||http_append(out,1024,&at,old->host)<0)return -90;
    if(old->port!=80&&(http_append(out,1024,&at,":")<0||http_append(out,1024,&at,port)<0))return -90;
    if(location[0]!='/'){
        u32 end=0,last=0;while(old->path[end]&&old->path[end]!='?'){if(old->path[end]=='/')last=end;end++;}
        if(location[0]=='?')last=end?end-1:0;
        for(u32 i=0;i<=last;i++){if(at+1>=1024)return -90;out[at++]=old->path[i];}out[at]=0;
    }
    return http_append(out,1024,&at,location);
}
static int http_body_piece(int output,u32 bytes,u32 *written,u32 maximum,int exact)
{
    u8 chunk[4096];u32 remain=bytes;
    while(remain || !exact){u32 used=0,take=exact&&remain<sizeof(chunk)?remain:sizeof(chunk);
        while(used<take){int c=http_byte();if(c<0){if(c==-200&&!exact){if(used){if(used>maximum-*written)return -27;int r=cli_write(output,chunk,used);if(r<0)return r;*written+=used;}return 0;}return c==-200?-5:c;}chunk[used++]=(u8)c;}
        if(used>maximum-*written)return -27;int result=cli_write(output,chunk,used);if(result<0)return result;*written+=used;if(exact)remain-=used;
    }return 0;
}
static int http_chunked(int output,u32 *written,u32 maximum)
{
    char line[512];for(;;){int n=http_line(line,sizeof(line));if(n<0)return n;u32 size=0,at=0;
        while(line[at]&&line[at]!=';'){int digit=net_hex(line[at++]);if(digit<0||size>(0xFFFFFFFFu-(u32)digit)/16)return -71;size=size*16+(u32)digit;}if(!at)return -71;
        if(!size){u32 total=0;for(;;){n=http_line(line,sizeof(line));if(n<0)return n;int emitted=HTTP_HEADER_LINE(line,n);if(emitted<0)return emitted;total+=(u32)n+2;if(total>16384)return -90;if(!n)return 0;if(!net_find(line,':'))return -71;}}
        if(size>maximum-*written)return -27;int result=http_body_piece(output,size,written,maximum,1);if(result<0)return result;
        int a=http_byte();if(a<0)return a;int b=http_byte();if(b<0)return b;if(a!='\r'||b!='\n')return -71;
    }
}

#endif
