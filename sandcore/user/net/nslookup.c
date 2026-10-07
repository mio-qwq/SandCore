#include "NETCLI.inc"
int main(void)
{const char *usage="[-t SECONDS] NAME  (IPv4 A query through configured DNS/cache)";int start=net_start("nslookup",usage);if(start)return start>0?0:2;
    u32 seconds=10;const char *name=0;for(int i=0;i<cli_argc;i++){
        if(equal(cli_argv[i],"-t")){if(++i>=cli_argc||net_u32(cli_argv[i],86400,&seconds)<0||!seconds)return 2;}
        else if(!name)name=cli_argv[i];else return net_usage("nslookup",usage);}
    if(!name)return net_usage("nslookup",usage);u32 info[96],address;int result=sc_net_info(info);if(result<0)return cli_error("nslookup",result);
    cli_text(1,"Server: ");net_ip(1,info[7]);cli_text(1,"\n");result=sc_net_resolve(name,&address,seconds*100u);if(result<0)return cli_error("nslookup",result);
    cli_text(1,"Name:   ");cli_text(1,name);cli_text(1,"\nAddress: ");net_ip(1,address);cli_text(1,"\n");return 0;}
