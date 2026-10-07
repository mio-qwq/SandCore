#include "NETCLI.inc"
int main(void)
{const char *usage="[DOMAIN]  (runtime DNS domain; empty quoted name clears)";int start=net_start("dnsdomainname",usage);if(start)return start>0?0:2;
    if(cli_argc>1)return net_usage("dnsdomainname",usage);if(cli_argc){int result=net_text_control(SC_NET_SET_DOMAIN,cli_argv[0]);return result<0?cli_error("dnsdomainname",result):0;}
    u32 info[96];int result=sc_net_info(info);if(result<0)return cli_error("dnsdomainname",result);cli_text(1,(char *)(info+48));cli_text(1,"\n");return 0;}
