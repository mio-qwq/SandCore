#include "NETCLI.inc"
int main(void)
{const char *usage="[-n|show] | add|del default|CIDR|-host IP [gw IP] [metric N] [dev en0]";int start=net_start("route",usage);if(start)return start>0?0:2;
    int result=net_route_command(0,0);return result==-22?net_usage("route",usage):result<0?cli_error("route",result):0;}
