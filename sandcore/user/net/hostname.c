#include "NETCLI.inc"
int main(void)
{const char *usage="[-s] | NAME  (runtime hostname)";int start=net_start("hostname",usage);if(start)return start>0?0:2;
    if(cli_argc>1)return net_usage("hostname",usage);
    if(cli_argc && !equal(cli_argv[0],"-s")){int result=net_text_control(SC_NET_SET_HOSTNAME,cli_argv[0]);return result<0?cli_error("hostname",result):0;}
    u32 info[96];int result=sc_net_info(info);if(result<0)return cli_error("hostname",result);cli_text(1,(char *)(info+32));cli_text(1,"\n");return 0;}
