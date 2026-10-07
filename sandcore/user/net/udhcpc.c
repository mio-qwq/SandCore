#include "NETCLI.inc"
int main(void)
{const char *usage="[-i en0] [-t SECONDS] [-n|-r|-R]  (acquire|renew|release; kernel maintains lease)";int start=net_start("udhcpc",usage);if(start)return start>0?0:2;
    u32 seconds=30,operation=SC_NET_DHCP_START;
    for(int i=0;i<cli_argc;i++){if(equal(cli_argv[i],"-i")){if(++i>=cli_argc||!net_interface(cli_argv[i]))return 2;}
        else if(equal(cli_argv[i],"-t")){if(++i>=cli_argc||net_u32(cli_argv[i],86400,&seconds)<0||!seconds)return 2;}
        else if(equal(cli_argv[i],"-n"))operation=SC_NET_DHCP_START;else if(equal(cli_argv[i],"-r"))operation=SC_NET_DHCP_RENEW;
        else if(equal(cli_argv[i],"-R"))operation=SC_NET_DHCP_RELEASE;else return net_usage("udhcpc",usage);}
    int result=net_simple_control(operation);if(result<0)return cli_error("udhcpc",result);if(operation==SC_NET_DHCP_RELEASE)return 0;
    result=net_wait_address(seconds*100u);if(result<0)return cli_error("udhcpc",result);return net_show_interface()<0?1:0;}
