#include "SCIO.H"
#include "SCNET.H"
/* 独立三环探针只用公开ABI。统一阶段在客体内编译；固定用例规模
 * 是证据预算，不能成为内核创建限制或测试专用后门。 */
static int failures;
static void check(int okay,const char *message)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,message);cli_text(1,"\n");if(!okay)failures++;}
static int remaining(u32 began,u32 timeout)
{u32 elapsed=(u32)sc_tick()-began;return elapsed>=timeout?0:(int)(timeout-elapsed);}
static int udp_send(u32 handle,const void *body,u32 bytes,const u32 *address)
{u32 began=(u32)sc_tick();for(;;){int result=sc_socket_send(handle,body,bytes,address);if(result!=-11)return result;
    if(!remaining(began,500))return -110;sc_event_wait(SC_EVENT_NETWORK,1);}}
static int udp_read(u32 handle,void *body,u32 bytes,u32 *address)
{u32 began=(u32)sc_tick();for(;;){int result=sc_socket_receive(handle,body,bytes,address);if(result!=-11)return result;
    int ticks=remaining(began,500);if(!ticks)return -110;result=sc_net_wait(handle,SC_NET_READ,(u32)ticks);if(result<0)return result;}}
static int many(void)
{
    int *handles=(int *)sc_alloc(131u*sizeof(int));if(!handles)return cli_error("fixture allocation",-12);
    int count=0;for(;count<131;count++){handles[count]=sc_socket(SC_NET_UDP,0);if(handles[count]<0)break;}
    check(count==131,"131 simultaneous socket objects");
    if(count){u32 status[18];status[16]=0xFACE0001;status[17]=0xFACE0002;
        check(sc_socket_status((u32)handles[0],status)==0 && status[0]==1 && status[16]==0xFACE0001 && status[17]==0xFACE0002,"status exact 64B boundary");
        u32 page[34];page[32]=0xFACE0003;page[33]=0xFACE0004;u32 cursor=0,seen=0,steps=0;
        do{int n=sc_socket_page(cursor,page,32);if(n<0){failures++;break;}seen+=(u32)n;cursor=page[3];
            if(++steps>512){failures++;break;}}while(cursor);
        check(seen>=131 && page[32]==0xFACE0003 && page[33]==0xFACE0004,"socket pagination and buffer boundary");
        u32 address[4];sc_net_address(address,0,0);
        check(sc_socket_bind((u32)handles[0],(u32 *)0xFFFFFFFCu)==-14,"bind pointer checked");
        check(sc_socket_receive((u32)handles[0],(void *)0xFFFFFFFCu,8,0)==-14,"receive pointer checked");
        check(sc_socket_send((u32)handles[0],address,65537,address)<0,"send count rejected");
    }
    for(int i=0;i<count;i++)sc_socket_close((u32)handles[i]);
    if(count)check(sc_socket_status((u32)handles[0],(u32 *)handles)==-9,"closed socket is immediately invalid");
    sc_free(handles);return failures?1:0;
}
static int echo(const char *host,const char *size)
{
    u32 target;int amount;if(sc_net_parse_ip(host,&target)<0 || cli_integer(size,&amount)<0 || amount<0 || amount>65507)return 2;
    u32 count=(u32)amount;u8 *sent=(u8 *)sc_alloc(count?count:1),*received=(u8 *)sc_alloc(count?count:1);
    if(!sent || !received){if(sent)sc_free(sent);if(received)sc_free(received);return 1;}
    for(u32 i=0;i<count;i++)sent[i]=(u8)(i*73u+(i>>7));
    int handle=sc_socket(SC_NET_UDP,0);if(handle<0){sc_free(sent);sc_free(received);return cli_error("udp",handle);}
    u32 address[4],sender[4];sc_net_address(address,target,7007);
    int result=udp_send((u32)handle,sent,count,address);check(result==amount,"UDP sent exact datagram");
    if(result==amount){result=udp_read((u32)handle,received,count?count:1,sender);int matched=result==amount && sender[1]==target && sender[2]==7007;
        if(matched)for(u32 i=0;i<count;i++)if(sent[i]!=received[i]){matched=0;break;}
        check(matched,"UDP reassembly exact original bytes");}
    sc_socket_close((u32)handle);sc_free(sent);sc_free(received);return failures?1:0;
}
int main(void)
{
    if(cli_parse()<0 || !cli_argc)return 2;
    if(equal(cli_argv[0],"many"))return many();
    if(equal(cli_argv[0],"none")){u32 info[96];check(sc_net_info(info)==0 && !(info[2]&1u) && info[3]==1,"no NIC is reported without invented address");
        check(sc_socket(SC_NET_UDP,0)==-100,"socket fails clearly without NIC");return failures?1:0;}
    if(equal(cli_argv[0],"udp") && cli_argc==3)return echo(cli_argv[1],cli_argv[2]);
    if(equal(cli_argv[0],"identity")){u32 self[8],stream[8];int n=sc_process_self(self);
        check(n==0 && self[3]==0 && self[4]==0 && self[5]==0,"network SYSTEM child is root NORMAL");
        check(sc_terminal_info2(0,stream)==0 && stream[0]==1 && stream[1]==3 && !stream[4],"network child stdin is a pipe without UART capabilities");
        check(sc_stream_ready(0,stream)==0,"network child has readable pipe descriptor");return failures?1:0;}
    if(equal(cli_argv[0],"info")){u32 info[96];int n=sc_net_info(info);if(n<0)return cli_error("netinfo",n);
        cli_text(1,"flags=");cli_number(1,(int)info[2]);cli_text(1," address=");char text[16];sc_net_format_ip(info[4],text);cli_text(1,text);
        cli_text(1," pages=");cli_number(1,(int)info[18]);cli_text(1," sockets=");cli_number(1,(int)info[16]);cli_text(1,"\n");return 0;}
    if(equal(cli_argv[0],"diagnostics")){u32 info[96];int n=sc_net_info(info);if(n<0)return cli_error("netinfo",n);
        /* 完整公开快照供宿主判别初始化/链路/收发故障，不增加内核
         * 测试入口，也不把无报文超时直接归因于协议或宿主速度。 */
        for(int i=0;i<96;i++){cli_text(1,"NET[");cli_number(1,i);cli_text(1,"]=");
            char digits[11];u32 value=info[i];int used=0;do{digits[used++]=(char)('0'+value%10);value/=10;}while(value);
            while(used){used--;cli_write(1,digits+used,1);}cli_text(1,"\n");}return 0;}
    return 2;
}
