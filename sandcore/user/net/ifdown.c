#include "NETCLI.inc"
int main(void)
{const char *usage="[en0]  (stop interface and DHCP; --help)";int start=net_start("ifdown",usage);if(start)return start>0?0:2;
    if(cli_argc>1 || (cli_argc&&!net_interface(cli_argv[0])))return net_usage("ifdown",usage);
    int result=net_simple_control(SC_NET_DOWN);return result<0?cli_error("ifdown",result):0;}
