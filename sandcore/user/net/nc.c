#include "NETCLI.inc"
int main(void)
{
    const char *usage="[-u] [-l -p PORT] [-s IPv4] [-w SECONDS] [-q SECONDS] [-v] [-z] [-e PROGRAM] [HOST PORT]  (quote PROGRAM with its arguments)";
    int start=net_start("nc",usage);if(start)return start>0?0:2;
    int udp=0,listen=0,verbose=0,scan=0;u32 local=0,local_port=0,port=0,seconds=30,quit=0;const char *host=0,*program=0;
    for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];
        if(equal(p,"-u"))udp=1;else if(equal(p,"-l"))listen=1;else if(equal(p,"-v"))verbose=1;else if(equal(p,"-z"))scan=1;
        else if(equal(p,"-p")||equal(p,"-w")||equal(p,"-q")||equal(p,"-s")||equal(p,"-e")){
            if(++i>=cli_argc)return 2;if(equal(p,"-e")){if(program || !*cli_argv[i])return 2;program=cli_argv[i];}
            else if(equal(p,"-s")){if(sc_net_parse_ip(cli_argv[i],&local)<0)return 2;}
            else{u32 *v=equal(p,"-p")?&local_port:equal(p,"-q")?&quit:&seconds;if(net_u32(cli_argv[i],v==&local_port?65535u:86400u,v)<0 || (v==&local_port&&!*v))return 2;}
        }else if(!host)host=p;else if(!port){if(net_u32(p,65535u,&port)<0||!port)return 2;}else return net_usage("nc",usage);
    }
    if((listen&&(!local_port||host||port||scan)) || (!listen&&(!host||!port)) || (scan&&(udp||program)))return net_usage("nc",usage);
    u32 ticks=seconds?seconds*100u:0xFFFFFFFFu,target=0,address[4],peer[4];int result=0,handle=sc_socket(udp?SC_NET_UDP:SC_NET_TCP,0),listener=-1;
    int input_fd=0,output_fd=1,program_job=0,program_code=0;int incoming[2]={-1,-1},outgoing[2]={-1,-1};
    if(handle<0)return cli_error("nc",handle);
    /* 短服务主动FIN后可能留下真实TIME_WAIT。只为TCP监听设置地址
     * 重用，使下一次启动可绑定原端口；旧四元组继续按TCP规则保留，
     * 不清掉TIME_WAIT、不改序号，也不靠等待或放宽超时掩盖失败。 */
    if(listen&&!udp){result=sc_socket_option((u32)handle,SC_NET_OPT_REUSEADDR,1);if(result<0)goto done;}
    if(local||local_port){sc_net_address(address,local,local_port);result=sc_socket_bind((u32)handle,address);if(result<0)goto done;}
    if(listen&&!udp){
        listener=handle;result=sc_socket_listen((u32)listener,16);if(result<0)goto done;
        if(verbose){cli_text(2,"Listening on ");net_uint(2,local_port);cli_text(2,"\n");}
        result=sc_net_wait((u32)listener,SC_NET_ACCEPT,ticks);if(result<0)goto done;
        handle=sc_socket_accept((u32)listener,peer);if(handle<0){result=handle;handle=listener;listener=-1;goto done;}
        sc_socket_close((u32)listener);listener=-1;
    }else if(!listen){
        result=sc_net_resolve(host,&target,ticks);if(result<0)goto done;sc_net_address(address,target,port);
        result=sc_socket_connect((u32)handle,address);if(result==-115)result=sc_net_wait((u32)handle,SC_NET_WRITE,ticks);if(result<0)goto done;
        sc_net_address(peer,target,port);
    }
    if(verbose){cli_text(2,listen&&udp?"UDP bound\n":"Connected\n");}if(scan){result=0;goto done;}
    if(program){
        result=sc_stream_pipe(incoming);if(result<0)goto done;result=sc_stream_pipe(outgoing);if(result<0)goto done;
        int fds[3]={incoming[0],outgoing[1],outgoing[1]};program_job=sc_net_exec(program,fds);if(program_job<0){result=program_job;program_job=0;goto done;}
        sc_stream_close(incoming[0],0);incoming[0]=-1;sc_stream_close(outgoing[1],0);outgoing[1]=-1;
        input_fd=outgoing[0];output_fd=incoming[1];
    }
    {
        u8 input[4096],output[4096];u32 input_count=0,input_at=0,output_count=0,output_at=0;
        u32 last_progress=(u32)sc_tick(),input_ended=0,peer_ended=0,write_ended=0,input_end_tick=0;
        int peer_known=!listen || !udp,program_finished=0;
        for(;;){int progress=0;
            /* 两个方向各保留一块，先推进已有字节，再读取下一块。stdout
             * 管道满时不能进入cli_write的独占等待，否则stdin/反向TCP
             * 不能前进。所有阻塞都合成一次事件等待，保留真正背压。 */
            if(output_at<output_count){int n=sc_stream_write(output_fd,output+output_at,output_count-output_at);
                if(n>0){output_at+=(u32)n;progress=1;}
                else if(program&&n==-7){
                    /* 子程序可先关闭stdin再输出最后一段。破管只停止
                     * 这个方向，仍须读尽stdout/stderr并发出尾部字节；
                     * 不能因对端又发了数据而丢掉已生成的程序输出。 */
                    sc_stream_close(output_fd,0);incoming[1]=-1;output_fd=-1;
                    output_at=output_count;peer_ended=1;progress=1;
                }else if(n!=-6){result=n?n:-5;break;}}
            if(input_at<input_count&&peer_known){int n=sc_socket_send((u32)handle,input+input_at,input_count-input_at,udp?peer:0);
                if(n>=0){if(udp && (u32)n!=input_count-input_at){result=-5;break;}input_at+=(u32)n;progress=1;}
                else if(n!=-11){result=n;break;}}
            if(!input_ended&&input_at==input_count){int n=sc_stream_read(input_fd,input,sizeof(input));
                if(n>0){input_count=(u32)n;input_at=0;progress=1;}else if(!n){input_ended=1;input_end_tick=(u32)sc_tick();progress=1;}
                else if(n!=-6){result=n;break;}}
            if(!peer_ended&&output_at==output_count){int n=sc_socket_receive((u32)handle,output,sizeof(output),address);
                if(n>0 || (!n&&udp)){if(udp&&listen){if(!peer_known){net_copy(peer,address,sizeof(peer));peer_known=1;}
                        else if(address[1]!=peer[1]||address[2]!=peer[2])continue;}
                    output_count=(u32)n;output_at=0;progress=1;}
                else if(!n){peer_ended=1;progress=1;}else if(n!=-11){result=n;break;}}
            if(input_ended&&input_at==input_count&&!write_ended){
                if(udp)write_ended=1;else{int n=sc_socket_shutdown((u32)handle,1);if(n==0 || n==-108 || n==-107)write_ended=1;else if(n!=-11){result=n;break;}}
            }
            if(program_job){u32 state[8];int n=sc_job_info2((u32)program_job,state);
                if(n<0){result=n;break;}if(!state[1]){program_code=(int)state[2];if(!program_finished)progress=1;program_finished=1;}}
            if(peer_ended&&output_at==output_count){
                if(program&&output_fd>=0){sc_stream_close(output_fd,0);incoming[1]=-1;output_fd=-1;progress=1;}
                if(!program || (program_finished&&input_ended&&input_at==input_count&&write_ended)){result=0;break;}
            }
            /* -e的输出管道EOF意味着所有写端已关闭。尾部发送和半关闭
             * 完成后即可收尾，不要求网络对端也主动EOF，否则echo一类
             * 短程序会在保持连接的客户端上无谓等待到-w超时。 */
            /* STREAM清理可能先于作业结果发布，必须等JOB真正结束才取
             * 退出码；不能把管道EOF误当作exit(0)。 */
            if(program&&program_finished&&input_ended&&input_at==input_count&&write_ended&&output_at==output_count){result=0;break;}
            if(input_ended&&input_at==input_count&&output_at==output_count){u32 after=quit?quit:(udp&&!program)?1u:0u;
                if(after&&(u32)((u32)sc_tick()-input_end_tick)>=after*100u){result=program&&!program_finished?-110:0;break;}}
            if(progress)last_progress=(u32)sc_tick();
            if(!net_remaining(last_progress,ticks)){result=-110;break;}
            if(!progress){u32 streams[8];sc_stream_ready(input_fd,streams);if(output_fd>=0)sc_stream_ready(output_fd,streams);
                /* PF耗尽或协议发送队列限制可能保留WRITE提示；每次停滞
                 * 最多20tick并至少经过一次等待，不能轮询所有对象。 */
                sc_event_wait(SC_EVENT_NETWORK|SC_EVENT_STREAM|SC_EVENT_INPUT|SC_EVENT_JOB,20);}
        }
    }
done:
    for(int i=0;i<2;i++){if(incoming[i]>=0)sc_stream_close(incoming[i],0);if(outgoing[i]>=0)sc_stream_close(outgoing[i],0);}
    if(program_job){u32 state[8];if(sc_job_info2((u32)program_job,state)==0){program_code=(int)state[2];sc_wait2((u32)program_job,state);}}
    /* 连接断开/超时导致仍活着的程序，由父任务退出的拥有者作业链
     * 按代数撤销；不以旧PID杀可能已复用的进程。 */
    if(listener>=0&&listener!=handle)sc_socket_close((u32)listener);sc_socket_close((u32)handle);
    return result<0?cli_error("nc",result):program_code>=0&&program_code<=255?program_code:1;
}
