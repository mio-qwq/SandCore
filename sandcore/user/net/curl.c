#include "NETCLI.inc"
/* 复用已验过的HTTP分块/长度解码；请求选项与上传只属于三环工具，
 * 不另建协议栈，也不把HTTP子集包装成上游libcurl或TLS能力。 */
static int curl_header_line(const char *line,int bytes);
static int curl_expired(void);
static u32 curl_left(void);
#define HTTP_HEADER_LINE(line,bytes) curl_header_line(line,bytes)
#define HTTP_EXPIRED() curl_expired()
#define HTTP_WAIT_TICKS curl_left()
#include "HTTP.inc"

typedef struct {
    const char *url,*output,*dump,*method,*agent,*data;
    const char *headers[CLI_ARGS];u32 headers_count;
    u32 seconds,connect_seconds,maximum,redirects;
    int follow,head,include,fail,silent,show_error,verbose,body_kind,explicit_method;
} curl_options_t;
static curl_options_t curl_options;
static u32 curl_began,curl_budget,curl_head_bytes,curl_head_capacity;
static int curl_head_fd=-1;
static char *curl_head_text;

static int curl_expired(void)
{return curl_budget!=0xFFFFFFFFu&&(u32)((u32)sc_tick()-curl_began)>=curl_budget;}
static u32 curl_left(void)
{return net_remaining(curl_began,curl_budget);}
static int curl_header_line(const char *line,int bytes)
{
    if(curl_expired())return -110;
    if(curl_options.verbose){cli_text(2,"< ");cli_write(2,line,(u32)bytes);cli_text(2,"\n");}
    if(curl_head_fd>=0){int n=cli_write(curl_head_fd,line,(u32)bytes);if(n<0)return n;n=cli_text(curl_head_fd,"\r\n");if(n<0)return n;}
    if(curl_options.include||curl_options.head){
        u32 needed=curl_head_bytes+(u32)bytes+2;if(needed>65536)return -90;
        /* 普通GET不占头缓存；需要包含头时按4KiB起步增长，最多64KiB。
         * 期限只在4KiB网络缓存补充时检查，不为每个正文字符陷入内核。 */
        if(needed>curl_head_capacity){u32 capacity=curl_head_capacity?curl_head_capacity*2:4096;while(capacity<needed)capacity*=2;
            char *grown=sc_alloc(capacity);if(!grown)return -12;if(curl_head_bytes)net_copy(grown,curl_head_text,curl_head_bytes);
            if(curl_head_text)sc_free(curl_head_text);curl_head_text=grown;curl_head_capacity=capacity;}
        net_copy(curl_head_text+curl_head_bytes,line,(u32)bytes);curl_head_bytes+=(u32)bytes;
        curl_head_text[curl_head_bytes++]='\r';curl_head_text[curl_head_bytes++]='\n';
    }return 0;
}
static int curl_token(char c)
{return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='!'||c=='#'||c=='$'||c=='%'||c=='&'||c=='\''||c=='*'||c=='+'||c=='-'||c=='.'||c=='^'||c=='_'||c=='`'||c=='|'||c=='~';}
static int curl_header_name(const char *header,const char *name)
{
    u32 i=0;while(name[i]){char a=header[i],b=name[i];if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)return 0;i++;}
    return header[i]==':';
}
static int curl_has_header(const char *name)
{for(u32 i=0;i<curl_options.headers_count;i++)if(curl_header_name(curl_options.headers[i],name))return 1;return 0;}
static int curl_header_valid(const char *header)
{
    u32 i=0;while(header[i]&&header[i]!=':'){if(!curl_token(header[i]))return 0;i++;}if(!i||header[i]!=':')return 0;
    for(i++;header[i];i++)if((u8)header[i]<32&&header[i]!='\t')return 0;
    if(curl_header_name(header,"Content-Length")||curl_header_name(header,"Transfer-Encoding")||curl_header_name(header,"Connection")||curl_header_name(header,"Expect"))return 0;
    return 1;
}
static void curl_decimal(u32 value,char out[11])
{u32 n=0;do{out[n++]=(char)('0'+value%10);value/=10;}while(value);for(u32 i=0;i<n/2;i++){char c=out[i];out[i]=out[n-1-i];out[n-1-i]=c;}out[n]=0;}
static int curl_value(char option,const char *value)
{
    if(option=='o')curl_options.output=value;else if(option=='D')curl_options.dump=value;
    else if(option=='X'){if(!*value||length(value)>31)return -1;for(u32 i=0;value[i];i++)if(!curl_token(value[i]))return -1;curl_options.method=value;curl_options.explicit_method=1;}
    else if(option=='A'){for(u32 i=0;value[i];i++)if((u8)value[i]<32)return -1;curl_options.agent=value;}
    else if(option=='H'){if(curl_options.headers_count==CLI_ARGS||!curl_header_valid(value))return -1;curl_options.headers[curl_options.headers_count++]=value;}
    else if(option=='d'||option=='b'||option=='r'||option=='T'){if(curl_options.body_kind)return -1;curl_options.body_kind=option=='d'?1:option=='b'?2:option=='r'?3:4;curl_options.data=value;}
    else if(option=='m'){if(net_u32(value,86400,&curl_options.seconds)<0)return -1;}
    else if(option=='c'){if(net_u32(value,86400,&curl_options.connect_seconds)<0)return -1;}
    else if(option=='z'){if(net_u32(value,268435456,&curl_options.maximum)<0||!curl_options.maximum)return -1;}
    else if(option=='R'){if(net_u32(value,20,&curl_options.redirects)<0)return -1;}
    else if(option=='u')curl_options.url=value;else return -1;return 0;
}
static int curl_boolean(char option)
{
    if(option=='s')curl_options.silent=1;else if(option=='S')curl_options.show_error=1;
    else if(option=='L')curl_options.follow=1;else if(option=='I')curl_options.head=1;
    else if(option=='i')curl_options.include=1;else if(option=='f')curl_options.fail=1;
    else if(option=='v')curl_options.verbose=1;else if(option!='4')return 0;return 1;
}
static int curl_request(http_url_t *url,const char *method,u32 bytes,int body,int same_origin)
{
    char request[4096],port[6],number[11];u32 at=0;http_port(url->port,port);curl_decimal(bytes,number);
    if(http_append(request,sizeof(request),&at,method)<0||http_append(request,sizeof(request),&at," ")<0||http_append(request,sizeof(request),&at,url->path)<0||http_append(request,sizeof(request),&at," HTTP/1.1\r\n")<0)return -90;
    if(!curl_has_header("Host")){if(http_append(request,sizeof(request),&at,"Host: ")<0||http_append(request,sizeof(request),&at,url->host)<0)return -90;
        if(url->port!=80&&(http_append(request,sizeof(request),&at,":")<0||http_append(request,sizeof(request),&at,port)<0))return -90;if(http_append(request,sizeof(request),&at,"\r\n")<0)return -90;}
    if(!curl_has_header("User-Agent")){if(http_append(request,sizeof(request),&at,"User-Agent: ")<0||http_append(request,sizeof(request),&at,curl_options.agent)<0||http_append(request,sizeof(request),&at,"\r\n")<0)return -90;}
    if(!curl_has_header("Accept")&&http_append(request,sizeof(request),&at,"Accept: */*\r\n")<0)return -90;
    if(!curl_has_header("Accept-Encoding")&&http_append(request,sizeof(request),&at,"Accept-Encoding: identity\r\n")<0)return -90;
    if(body){if(http_append(request,sizeof(request),&at,"Content-Length: ")<0||http_append(request,sizeof(request),&at,number)<0||http_append(request,sizeof(request),&at,"\r\n")<0)return -90;
        if(curl_options.body_kind!=4&&!curl_has_header("Content-Type")&&http_append(request,sizeof(request),&at,"Content-Type: application/x-www-form-urlencoded\r\n")<0)return -90;}
    for(u32 i=0;i<curl_options.headers_count;i++){
        const char *header=curl_options.headers[i];
        /* 跳转离开原主机/端口时不转交认证和Cookie，普通自定义头继续。 */
        if(!same_origin&&(curl_header_name(header,"Authorization")||curl_header_name(header,"Proxy-Authorization")||curl_header_name(header,"Cookie")))continue;
        char *colon=net_find((char *)header,':');if(!colon[1])continue;
        if(http_append(request,sizeof(request),&at,header)<0||http_append(request,sizeof(request),&at,"\r\n")<0)return -90;
    }
    if(http_append(request,sizeof(request),&at,"Connection: close\r\n\r\n")<0)return -90;
    if(curl_options.verbose){cli_text(2,"> ");cli_write(2,request,at);}
    return sc_net_send_all(http_handle,request,at,curl_left());
}
static int curl_upload(int input,const u8 *literal,u32 bytes)
{
    u8 block[4096];u32 at=0;while(at<bytes){if(curl_expired())return -110;u32 take=bytes-at;if(take>sizeof(block))take=sizeof(block);const u8 *source=literal?literal+at:block;
        if(input>=0){int n=cli_read(input,block,take);if(n<=0)return n?n:-5;take=(u32)n;}
        int n=sc_net_send_all(http_handle,source,take,curl_left());if(n<0)return n;at+=take;
    }return 0;
}
static int curl_exit(int error)
{
    if(error==-222)return 22;if(error==-110)return 28;if(error==-40)return 47;if(error==-27)return 63;
    if(error==-95)return 1;if(error==-22||error==-90)return 3;return 56;
}
int main(void)
{
    const char *usage="[-fsSLivI4] [-X METHOD] [-H HEADER] [-d DATA|--data-binary DATA|-T FILE] [-o FILE|-] [-D FILE|-] [-A AGENT] [-m SECONDS] [--connect-timeout SECONDS] [--max-size BYTES] [--max-redirs N] URL (HTTP subset; HTTPS unsupported)";
    int start=net_start("curl",usage);if(start)return start>0?0:2;
    net_zero(&curl_options,sizeof(curl_options));curl_options.agent="SandCore-curl/M10a1";curl_options.seconds=30;curl_options.connect_seconds=15;curl_options.maximum=16777216;curl_options.redirects=5;
    int options=1;
    for(int i=0;i<cli_argc;i++){
        const char *p=cli_argv[i];if(options&&equal(p,"--")){options=0;continue;}
        if(options&&equal(p,"--version")){cli_text(1,"SandCore curl M10a1 (self-written HTTP subset)\nProtocols: http; IPv4; TLS unavailable\n");return 0;}
        if(options&&(equal(p,"--help")||equal(p,"-h"))){cli_text(1,"curl: ");cli_text(1,usage);cli_text(1,"\n");return 0;}
        if(options&&p[0]=='-'&&p[1]&&p[1]!='-'){
            for(u32 j=1;p[j];j++){if(curl_boolean(p[j]))continue;char option=p[j];const char *value=p[j+1]?p+j+1:++i<cli_argc?cli_argv[i]:0;
                if(!value||curl_value(option,value)<0)return net_usage("curl",usage);break;}continue;
        }
        if(options&&p[0]=='-'&&p[1]=='-'){
            char option=0;
            if(equal(p,"--silent")){curl_options.silent=1;continue;}if(equal(p,"--show-error")){curl_options.show_error=1;continue;}
            if(equal(p,"--location")){curl_options.follow=1;continue;}if(equal(p,"--head")){curl_options.head=1;continue;}
            if(equal(p,"--include")){curl_options.include=1;continue;}if(equal(p,"--fail")){curl_options.fail=1;continue;}
            if(equal(p,"--verbose")){curl_options.verbose=1;continue;}if(equal(p,"--ipv4")||equal(p,"--http1.1"))continue;
            if(equal(p,"--request"))option='X';else if(equal(p,"--header"))option='H';else if(equal(p,"--data"))option='d';else if(equal(p,"--data-binary"))option='b';else if(equal(p,"--data-raw"))option='r';
            else if(equal(p,"--upload-file"))option='T';else if(equal(p,"--output"))option='o';else if(equal(p,"--dump-header"))option='D';else if(equal(p,"--user-agent"))option='A';
            else if(equal(p,"--max-time"))option='m';else if(equal(p,"--connect-timeout"))option='c';else if(equal(p,"--max-size"))option='z';else if(equal(p,"--max-redirs"))option='R';else if(equal(p,"--url"))option='u';
            if(!option||++i>=cli_argc||curl_value(option,cli_argv[i])<0)return net_usage("curl",usage);continue;
        }
        if(curl_options.url)return net_usage("curl",usage);curl_options.url=p;
    }
    if(!curl_options.url||(curl_options.head&&curl_options.body_kind))return net_usage("curl",usage);
    char current[1024];if(length(curl_options.url)>=1024)return 3;net_copy(current,curl_options.url,(u32)length(curl_options.url)+1);
    http_url_t original,parsed;http_response_t response;int result=http_url(current,&original),exit_code=0,input=-1,output=-1,connected=0,headers_ok=0;u32 body_bytes=0,written=0;char *allocated=0;const u8 *literal=0;
    if(result<0)goto done;
    if(curl_options.output&&curl_options.dump&&!equal(curl_options.output,"-")&&equal(curl_options.output,curl_options.dump)){result=-22;goto done;}
    if(curl_options.body_kind){
        const char *data=curl_options.data;int from_file=curl_options.body_kind==4||(curl_options.body_kind!=3&&data[0]=='@');
        if(from_file){const char *path=curl_options.body_kind==4?data:data+1;input=equal(path,"-")?0:sc_stream_open(path,1,0);if(input<0){result=input;exit_code=26;goto done;}
            if(input==0||curl_options.body_kind==1){allocated=cli_slurp(input,curl_options.maximum,&body_bytes);if(!allocated){result=-12;exit_code=26;goto done;}
                if(curl_options.body_kind==1){u32 to=0;for(u32 i=0;i<body_bytes;i++)if(allocated[i]&&allocated[i]!='\r'&&allocated[i]!='\n')allocated[to++]=allocated[i];body_bytes=to;}literal=(const u8 *)allocated;
                if(input!=0)sc_stream_close(input,0);input=-1;
            }else{char path_name[64];u32 info[2];if(cli_path(path,path_name)<0||sc_stat(path_name,info)<0||info[0]!=1){result=-22;exit_code=26;goto done;}body_bytes=info[1];}
        }else{literal=(const u8 *)data;body_bytes=(u32)length(data);}
    }
    const char *method=curl_options.method?curl_options.method:curl_options.head?"HEAD":curl_options.body_kind==4?"PUT":curl_options.body_kind?"POST":"GET";
    int active_body=curl_options.body_kind!=0;curl_began=(u32)sc_tick();curl_budget=curl_options.seconds?curl_options.seconds*100u:0xFFFFFFFFu;http_ticks=curl_budget;
    if(curl_options.dump){curl_head_fd=equal(curl_options.dump,"-")?1:sc_stream_open(curl_options.dump,2,(curl_options.redirects+1u)*16384u);if(curl_head_fd<0){result=curl_head_fd;exit_code=23;goto done;}}
    for(u32 redirect=0;;redirect++){
        result=http_url(current,&parsed);if(result<0)goto done;if(curl_expired()){result=-110;goto done;}
        u32 target;result=sc_net_resolve(parsed.host,&target,curl_left());if(result<0){exit_code=result==-110?28:6;goto done;}
        u32 timeout=curl_options.connect_seconds?curl_options.connect_seconds*100u:0xFFFFFFFFu;if(curl_left()<timeout)timeout=curl_left();
        result=sc_net_connect(target,parsed.port,timeout);if(result<0){exit_code=result==-110?28:7;goto done;}http_handle=(u32)result;connected=1;http_at=http_count=0;
        int same_origin=net_ci_equal(parsed.host,original.host)&&parsed.port==original.port;
        result=curl_request(&parsed,method,active_body?body_bytes:0,active_body,same_origin);
        if(result>=0&&active_body){if(input>=0&&redirect){result=sc_stream_seek(input,0);if(result<0){exit_code=26;goto done;}}if(result>=0)result=curl_upload(input,literal,body_bytes);}
        if(result>=0)result=http_headers(&response,0);if(result<0)goto done;headers_ok=1;
        if(curl_options.follow&&(response.status==301||response.status==302||response.status==303||response.status==307||response.status==308)){
            sc_socket_close(http_handle);connected=0;if(redirect>=curl_options.redirects){result=-40;goto done;}
            if(!curl_options.head&&(response.status==303||((response.status==301||response.status==302)&&net_ci_equal(method,"POST")))){active_body=0;if(!curl_options.explicit_method)method="GET";}
            result=http_redirect(&parsed,response.location,current);if(result<0)goto done;continue;
        }break;
    }
    if(curl_options.fail&&response.status>=400){result=-222;goto done;}
    if(curl_options.head)response.no_body=1;
    if(!response.no_body&&response.has_length&&response.length>curl_options.maximum){result=-27;goto done;}
    if(!curl_options.output||equal(curl_options.output,"-"))output=1;
    else{u32 capacity=(response.no_body?0:response.has_length?response.length:curl_options.maximum)+curl_head_bytes;output=sc_stream_open(curl_options.output,2,capacity);if(output<0){result=output;exit_code=23;goto done;}}
    if(curl_head_bytes){result=cli_write(output,curl_head_text,curl_head_bytes);if(result<0){exit_code=23;goto done;}}
    if(!response.no_body)result=response.chunked?http_chunked(output,&written,curl_options.maximum):http_body_piece(output,response.length,&written,curl_options.maximum,response.has_length!=0);
done:
    if(connected)sc_socket_close(http_handle);if(input>=0&&input!=0)sc_stream_close(input,0);if(allocated)sc_free(allocated);if(curl_head_text)sc_free(curl_head_text);
    if(output>=0&&output!=1){int n=sc_stream_close(output,result>=0);if(n<0){sc_stream_close(output,0);if(result>=0){result=n;exit_code=23;}}}
    if(curl_head_fd>=0&&curl_head_fd!=1){int n=sc_stream_close(curl_head_fd,headers_ok);if(n<0){sc_stream_close(curl_head_fd,0);if(result>=0){result=n;exit_code=23;}}}
    if(result<0){if(!exit_code)exit_code=curl_exit(result);if(!curl_options.silent||curl_options.show_error){cli_text(2,"curl: (");net_uint(2,(u32)exit_code);cli_text(2,") ");
        if(result==-95)cli_text(2,"HTTP only; HTTPS/TLS is not implemented");else if(result==-222){cli_text(2,"HTTP status ");net_uint(2,response.status);}else cli_number(2,result);cli_text(2,"\n");}return exit_code;}
    return 0;
}
