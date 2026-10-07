#include "NETCLI.inc"
int main(void)
{const char *usage="[en0]  (bring the interface up; --help)";int start=net_start("ifup",usage);if(start)return start>0?0:2;
    if(cli_argc>1 || (cli_argc&&!net_interface(cli_argv[0])))return net_usage("ifup",usage);
    int result=net_simple_control(SC_NET_UP);return result<0?cli_error("ifup",result):0;}
