#include "NETCLI.inc"
int main(void)
{const char *usage="[-n|-a] | -s IP MAC | -d IP | -f (flush)";int start=net_start("arp",usage);if(start)return start>0?0:2;int result;
    if(!cli_argc || (cli_argc==1&&(equal(cli_argv[0],"-n")||equal(cli_argv[0],"-a"))))result=net_show_arp();
    else if(cli_argc==3 && equal(cli_argv[0],"-s"))result=net_arp_set(cli_argv[1],cli_argv[2],0);
    else if(cli_argc==2 && equal(cli_argv[0],"-d"))result=net_arp_set(cli_argv[1],0,1);
    else if(cli_argc==1 && equal(cli_argv[0],"-f"))result=net_simple_control(SC_NET_ARP_FLUSH);else return net_usage("arp",usage);
    return result<0?cli_error("arp",result):0;}
