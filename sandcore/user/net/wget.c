#include "HTTP.inc"
int main(void)
{
    const char *usage="[-O FILE|-] [-T SECONDS] [--max-size BYTES] [-q] [-S] URL  (HTTP; redirects<=5; HTTPS unsupported)";
    int start=net_start("wget",usage);if(start)return start>0?0:2;
    const char *url=0,*output_name=0;u32 seconds=30,maximum=16777216;int quiet=0,show=0;
    for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(equal(p,"-q"))quiet=1;else if(equal(p,"-S"))show=1;
        else if(equal(p,"-O")||equal(p,"-T")||equal(p,"--max-size")){if(++i>=cli_argc)return 2;if(equal(p,"-O"))output_name=cli_argv[i];
            else{u32 *v=equal(p,"-T")?&seconds:&maximum;if(net_u32(cli_argv[i],v==&seconds?86400u:268435456u,v)<0||!*v)return 2;}}
        else if(!url)url=p;else return net_usage("wget",usage);}
    if(!url)return net_usage("wget",usage);char current[1024],derived[64];if(length(url)>=1024)return 2;net_copy(current,url,(u32)length(url)+1);
    http_ticks=seconds*100u;int result=0,output=-1;u32 written=0;http_url_t parsed;http_response_t response;
    for(u32 redirects=0;;redirects++){
        result=http_url(current,&parsed);if(result<0){cli_text(2,"wget supports http:// IPv4 URLs; HTTPS is not implemented\n");goto done;}
        u32 target;result=sc_net_resolve(parsed.host,&target,http_ticks);if(result<0)goto done;
        result=sc_net_connect(target,parsed.port,http_ticks);if(result<0)goto done;http_handle=(u32)result;http_at=http_count=0;
        result=http_request(&parsed);if(result>=0)result=http_headers(&response,show);
        if(result<0){sc_socket_close(http_handle);goto done;}
        if(response.status==301||response.status==302||response.status==303||response.status==307||response.status==308){
            sc_socket_close(http_handle);if(redirects>=5){result=-40;goto done;}result=http_redirect(&parsed,response.location,current);if(result<0)goto done;continue;}
        if(response.status<200||response.status>=300){cli_text(2,"HTTP status ");net_uint(2,response.status);cli_text(2,"\n");result=-71;sc_socket_close(http_handle);goto done;}
        break;
    }
    if(!output_name){u32 end=0,last=0;while(parsed.path[end]&&parsed.path[end]!='?'){if(parsed.path[end]=='/')last=end+1;end++;}
        if(end==last)output_name="index.html";else{if(end-last>=64){result=-36;goto close_socket;}net_copy(derived,parsed.path+last,end-last);derived[end-last]=0;output_name=derived;}}
    if(!response.no_body&&response.has_length&&response.length>maximum){result=-27;goto close_socket;}
    if(equal(output_name,"-"))output=1;else{
        u32 capacity=response.no_body?0:response.has_length?response.length:maximum;
        output=sc_stream_open(output_name,2,capacity);if(output<0){result=output;goto close_socket;}}
    if(!response.no_body)result=response.chunked?http_chunked(output,&written,maximum):http_body_piece(output,response.length,&written,maximum,response.has_length!=0);
    if(output!=1){if(result>=0){int committed=sc_stream_close(output,1);if(committed<0){result=committed;sc_stream_close(output,0);}}else sc_stream_close(output,0);output=-1;}
    if(result>=0&&!quiet){cli_text(2,"Saved ");net_uint(2,written);cli_text(2," bytes\n");}
close_socket:
    sc_socket_close(http_handle);
done:
    return result<0?cli_error("wget",result):0;
}
