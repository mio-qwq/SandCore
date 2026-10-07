#include "NETCLI.inc"
int main(void)
{const char *usage="[en0 [up|down|IP [netmask MASK] [gw IP] [mtu N]]]";int start=net_start("ifconfig",usage);if(start)return start>0?0:2;
    int result;if(!cli_argc || (cli_argc==1 && net_interface(cli_argv[0])))result=net_show_interface();
    else if(cli_argc<2 || !net_interface(cli_argv[0]))return net_usage("ifconfig",usage);
    else if(cli_argc==2 && (equal(cli_argv[1],"up")||equal(cli_argv[1],"down")))result=net_simple_control(equal(cli_argv[1],"up")?SC_NET_UP:SC_NET_DOWN);
    else{u32 address,mask=0xFFFFFF00u,gateway=0,mtu=1500;if(sc_net_parse_ip(cli_argv[1],&address)<0)return net_usage("ifconfig",usage);
        for(int i=2;i<cli_argc;i+=2){if(i+1>=cli_argc)return net_usage("ifconfig",usage);
            if(equal(cli_argv[i],"netmask")){if(sc_net_parse_ip(cli_argv[i+1],&mask)<0)return 2;}
            else if(equal(cli_argv[i],"gw")){if(sc_net_parse_ip(cli_argv[i+1],&gateway)<0)return 2;}
            else if(equal(cli_argv[i],"mtu")){if(net_u32(cli_argv[i+1],1500,&mtu)<0 || mtu<68)return 2;}else return net_usage("ifconfig",usage);}
        result=net_static(address,mask,gateway,mtu);}
    return result<0?cli_error("ifconfig",result):0;}
