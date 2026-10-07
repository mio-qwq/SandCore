#include "SCIO.H"
#include "SCNET.H"
/* 原创隔离线缆探针只调用正式公开ABI。16384/4097是本次数据预算，
 * 不改内核任何数量上限；对端另外核真实报文、丢包与原始字节。 */
static u8 outgoing[16384],incoming[4097];
static int failures;
static void check(int okay,const char *message)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,message);cli_text(1,"\n");if(!okay)failures++;}
static int receive(u32 handle,void *body,u32 count,u32 timeout)
{
    u32 began=(u32)sc_tick();
    for(;;){
        int result=sc_socket_receive(handle,body,count,0);if(result!=-11)return result;
        u32 elapsed=(u32)sc_tick()-began;if(elapsed>=timeout)return -110;
        result=sc_net_wait(handle,SC_NET_READ,timeout-elapsed);if(result<0)return result;
    }
}
static int exchange(void)
{
    int handle=sc_net_connect(0x0A170001u,7010,1500);
    check(handle>=0,"TCP connects after independent peer drops first SYN");
    if(handle<0)return 1;
    for(u32 i=0;i<16384;i++)outgoing[i]=(u8)(i*73u+(i>>7));
    int result=sc_net_send_all((u32)handle,outgoing,16384,3000);
    check(result==16384,"TCP accepts complete original stream across window backpressure");
    if(result==16384){
        check(sc_socket_shutdown((u32)handle,1)==0,"TCP transmit half shutdown retains receive side");
        check(sc_socket_send((u32)handle,outgoing,1,0)==-108,"TCP transmit after half shutdown is rejected");
        u32 count=0,began=(u32)sc_tick();
        while(count<4097){
            u32 elapsed=(u32)sc_tick()-began;if(elapsed>=3000){result=-110;break;}
            result=receive((u32)handle,incoming+count,4097-count,3000-elapsed);
            if(result<=0)break;count+=(u32)result;
        }
        int matched=count==4097;
        if(matched)for(u32 i=0;i<4097;i++)if(incoming[i]!=(u8)(i*29u+(i>>5)+0x53u)){matched=0;break;}
        check(matched,"TCP reordered and duplicated peer segments produce exact once ordered bytes");
        if(matched)check(receive((u32)handle,incoming,1,500)==0,"TCP peer FIN becomes EOF after all original bytes");
        cli_text(1,"tcp_received=");cli_number(1,(int)count);cli_text(1,"\n");
    }
    check(sc_socket_close((u32)handle)==0,"TCP close invalidates public handle");
    u32 state[16];check(sc_socket_status((u32)handle,state)==-9,"TCP retired handle cannot observe another socket");
    return failures?1:0;
}
static int reset(void)
{
    int handle=sc_net_connect(0x0A170001u,7012,1500);
    check(handle>=0,"TCP connects before controlled established reset");if(handle<0)return 1;
    u8 byte=0x4D;check(sc_net_send_all((u32)handle,&byte,1,500)==1,"TCP reset trigger is real socket data");
    check(receive((u32)handle,&byte,1,1000)==-104,"TCP established reset reports actual reset error");
    u32 state[18];state[16]=0xA10C0001;state[17]=0xA10C0002;
    int result=sc_socket_status((u32)handle,state);
    check(result==0 && (int)state[4]==-104 && (state[3]&SC_NET_ERROR)
        && state[16]==0xA10C0001 && state[17]==0xA10C0002,"TCP reset status retains exact error and 64 byte boundary");
    check(sc_socket_close((u32)handle)==0,"TCP reset socket closes without repeated callback ownership");
    return failures?1:0;
}
int main(void)
{
    if(cli_parse()<0 || cli_argc!=1)return 2;
    int result;
    if(equal(cli_argv[0],"exchange"))result=exchange();
    else if(equal(cli_argv[0],"reset"))result=reset();
    else if(equal(cli_argv[0],"refused")){
        int handle=sc_net_connect(0x0A170001u,7011,1500);
        check(handle==-104,"TCP refused SYN reports controlled peer reset");
        if(handle>=0)sc_socket_close((u32)handle);result=failures?1:0;
    }else return 2;
    cli_text(1,"tcp_fault_failures=");cli_number(1,failures);cli_text(1,"\n");return result;
}
