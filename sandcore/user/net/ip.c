#include "NETCLI.inc"
int main(void)
{const char *usage="link|addr|route|neigh [show]; link set en0 up|down; addr add IP/PREFIX dev en0; route add|del CIDR [via IP] [metric N] [dev en0]; neigh add IP lladdr MAC dev en0 | del IP dev en0 | flush";
    int start=net_start("ip",usage);if(start)return start>0?0:2;if(!cli_argc)return net_usage("ip",usage);int result=-22;const char *family=cli_argv[0];
    if(equal(family,"link")||equal(family,"addr")){
        if(cli_argc==1 || (cli_argc==2&&equal(cli_argv[1],"show")))result=net_show_interface();
        else if(equal(family,"link")&&cli_argc==5&&equal(cli_argv[1],"set")&&net_interface(cli_argv[2])&&equal(cli_argv[3],"state")){
            if(equal(cli_argv[4],"up")||equal(cli_argv[4],"down"))result=net_simple_control(equal(cli_argv[4],"up")?SC_NET_UP:SC_NET_DOWN);}
        else if(equal(family,"link")&&cli_argc==4&&equal(cli_argv[1],"set")&&net_interface(cli_argv[2])){
            if(equal(cli_argv[3],"up")||equal(cli_argv[3],"down"))result=net_simple_control(equal(cli_argv[3],"up")?SC_NET_UP:SC_NET_DOWN);}
        else if(equal(family,"addr")&&cli_argc==5&&equal(cli_argv[1],"add")&&equal(cli_argv[3],"dev")&&net_interface(cli_argv[4])){
            u32 address,prefix;if(net_cidr(cli_argv[2],&address,&prefix,1)==0)result=net_static(address,net_mask(prefix),0,1500);}
    }else if(equal(family,"route"))result=net_route_command(1,1);
    else if(equal(family,"neigh")){
        if(cli_argc==1 || (cli_argc==2&&equal(cli_argv[1],"show")))result=net_show_arp();
        else if(cli_argc==2&&equal(cli_argv[1],"flush"))result=net_simple_control(SC_NET_ARP_FLUSH);
        else if(cli_argc==7&&(equal(cli_argv[1],"add")||equal(cli_argv[1],"replace"))&&equal(cli_argv[3],"lladdr")&&equal(cli_argv[5],"dev")&&net_interface(cli_argv[6]))result=net_arp_set(cli_argv[2],cli_argv[4],0);
        else if(cli_argc==5&&equal(cli_argv[1],"del")&&equal(cli_argv[3],"dev")&&net_interface(cli_argv[4]))result=net_arp_set(cli_argv[2],0,1);
    }
    return result==-22?net_usage("ip",usage):result<0?cli_error("ip",result):0;}
