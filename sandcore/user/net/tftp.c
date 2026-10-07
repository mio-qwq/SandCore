#include "NETCLI.inc"
static int tftp_error(u32 handle,const u32 address[4],u32 code,const char *text,u32 ticks)
{u8 packet[516];u32 n=(u32)length(text);if(n>510)n=510;net_put16(packet,5);net_put16(packet+2,code);net_copy(packet+4,text,n);packet[4+n]=0;return net_send(handle,packet,n+5,address,ticks);}
static int tftp_block(int input,u8 packet[516],u32 block,u32 *total,u32 maximum,int *last)
{u32 n=0;while(n<512){int count=cli_read(input,packet+4+n,512-n);if(count<0)return count;if(!count)break;n+=(u32)count;}
    if(n>maximum-*total)return -27;*total+=n;*last=n<512;net_put16(packet,3);net_put16(packet+2,block&65535u);return (int)n+4;}
int main(void)
{
    const char *usage="-g|-p -r REMOTE [-l LOCAL|-] [-t SECONDS] [-R RETRIES] [--max-size BYTES] HOST [PORT]  (octet; 512-byte blocks)";
    int start=net_start("tftp",usage);if(start)return start>0?0:2;
    int direction=0,has_port=0;u32 seconds=2,retries=5,maximum=16777216,port=69;const char *remote=0,*local=0,*host=0;
    for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(equal(p,"-g")||equal(p,"-p")){if(direction)return 2;direction=equal(p,"-g")?1:2;}
        else if(equal(p,"-r")||equal(p,"-l")||equal(p,"-t")||equal(p,"-R")||equal(p,"--max-size")){
            if(++i>=cli_argc)return 2;if(equal(p,"-r"))remote=cli_argv[i];else if(equal(p,"-l"))local=cli_argv[i];
            else{u32 *v=equal(p,"-t")?&seconds:equal(p,"-R")?&retries:&maximum;u32 limit=v==&seconds?86400u:v==&retries?1000u:268435456u;
                if(net_u32(cli_argv[i],limit,v)<0 || (!*v&&v!=&retries))return 2;}}
        else if(!host)host=p;else if(!has_port){if(net_u32(p,65535u,&port)<0||!port)return 2;has_port=1;}else return net_usage("tftp",usage);}
    if(!direction||!remote||!*remote||!host||length(remote)>255)return net_usage("tftp",usage);if(!local)local=remote;
    u32 ticks=seconds*100u,target,address[4],sender[4];int result=sc_net_resolve(host,&target,1000);if(result<0)return cli_error("tftp",result);
    int handle=sc_socket(SC_NET_UDP,0);if(handle<0)return cli_error("tftp",handle);sc_net_address(address,0,0);result=sc_socket_bind((u32)handle,address);if(result<0)goto close_socket;
    sc_net_address(address,target,port);int input=-1,output=-1;u32 transferred=0;int upload=direction==2;
    if(upload){input=equal(local,"-")?0:sc_stream_open(local,1,0);if(input<0){result=input;goto close_socket;}}
    else{output=equal(local,"-")?1:sc_stream_open(local,2,maximum);if(output<0){result=output;goto close_socket;}}
    {
        u8 sent[516],received[2048];u32 block=upload?0:1,attempts=0,began=(u32)sc_tick(),tid=0;int final=0;
        net_put16(sent,upload?2:1);u32 name_bytes=(u32)length(remote)+1;net_copy(sent+2,remote,name_bytes);net_copy(sent+2+name_bytes,"octet",6);u32 sent_bytes=8+name_bytes;
        result=net_send((u32)handle,sent,sent_bytes,address,ticks);if(result<0)goto close_files;began=(u32)sc_tick();
        for(;;){u32 left=net_remaining(began,ticks);result=left?net_receive((u32)handle,received,sizeof(received),sender,left):-110;
            if(result==-110){if(attempts++>=retries)goto close_files;result=net_send((u32)handle,sent,sent_bytes,address,ticks);if(result<0)goto close_files;began=(u32)sc_tick();continue;}
            if(result<0)goto close_files;
            /* 只有正确来源、预期首包才锁定server TID；其它UDP端口
             * 获得Unknown transfer ID而不改变本会话/重传期限。 */
            if(sender[1]!=target || !sender[2])continue;
            if(tid&&sender[2]!=tid){tftp_error((u32)handle,sender,5,"Unknown transfer ID",ticks);continue;}
            if(result<4){if(tid)tftp_error((u32)handle,sender,4,"Malformed packet",ticks);result=-71;goto close_files;}
            u32 opcode=net_be16(received),number=net_be16(received+2);
            if(opcode==5){if(result<5){result=-71;goto close_files;}received[result-1]=0;cli_text(2,"TFTP error ");net_uint(2,number);cli_text(2,": ");cli_text(2,(char *)(received+4));cli_text(2,"\n");result=-5;goto close_files;}
            if(upload){
                if(opcode!=4||result!=4){if(!tid)continue;tftp_error((u32)handle,sender,4,"Expected ACK",ticks);result=-71;goto close_files;}
                /* 迟到/重复ACK只忽略，不能立刻重新发送DATA而形成
                 * Sorcerer's Apprentice放大链；只有期限到达重传。 */
                if(number!=(block&65535u))continue;
                if(!tid){tid=sender[2];net_copy(address,sender,sizeof(address));}
                if(final){result=0;break;}block++;sent_bytes=(u32)tftp_block(input,sent,block,&transferred,maximum,&final);
                if((int)sent_bytes<0){result=(int)sent_bytes;tftp_error((u32)handle,address,0,"Local input failed",ticks);goto close_files;}
            }else{
                if(opcode!=3||result>516){if(!tid)continue;tftp_error((u32)handle,sender,4,"Expected DATA",ticks);result=-71;goto close_files;}
                if(!tid){if(number!=1)continue;tid=sender[2];net_copy(address,sender,sizeof(address));}
                if(number!=((block)&65535u)){
                    if(number==((block-1)&65535u)&&block>1){u8 ack[4];net_put16(ack,4);net_put16(ack+2,number);net_send((u32)handle,ack,4,address,ticks);}continue;}
                u32 bytes=(u32)result-4;if(bytes>maximum-transferred){tftp_error((u32)handle,address,3,"File size limit",ticks);result=-27;goto close_files;}
                result=cli_write(output,received+4,bytes);if(result<0){tftp_error((u32)handle,address,3,"Local write failed",ticks);goto close_files;}transferred+=bytes;
                net_put16(sent,4);net_put16(sent+2,number);sent_bytes=4;final=bytes<512;
                if(final&&output!=1){int committed=sc_stream_close(output,1);if(committed<0){result=committed;tftp_error((u32)handle,address,3,"Local commit failed",ticks);goto close_files;}output=-1;}
                block++;
            }
            attempts=0;result=net_send((u32)handle,sent,sent_bytes,address,ticks);if(result<0)goto close_files;began=(u32)sc_tick();
            if(!upload&&final){
                /* 最后ACK可能丢失。短暂dally只重新ACK同一最后DATA，
                 * 不再次写/提交文件；期限不被重复包无限延长。 */
                u32 dally=ticks*2u;while(net_remaining(began,dally)){
                    int n=net_receive((u32)handle,received,sizeof(received),sender,net_remaining(began,dally));if(n<0)break;
                    if(sender[1]==target&&sender[2]==tid&&n>=4&&n<=516&&net_be16(received)==3&&net_be16(received+2)==((block-1)&65535u))net_send((u32)handle,sent,sent_bytes,address,ticks);
                    else if(sender[1]==target&&sender[2]!=tid)tftp_error((u32)handle,sender,5,"Unknown transfer ID",ticks);
                }result=0;break;
            }
        }
    }
close_files:
    if(input>0)sc_stream_close(input,0);if(output>=0&&output!=1)sc_stream_close(output,0);
    if(result>=0){cli_text(2,"Transferred ");net_uint(2,transferred);cli_text(2," bytes\n");}
close_socket:
    sc_socket_close((u32)handle);return result<0?cli_error("tftp",result):0;
}
