#include "NETCLI.inc"
int main(void)
{const char *usage="IP/PREFIX | IP MASK  (IPv4 address calculations)";int start=net_start("ipcalc",usage);if(start)return start>0?0:2;u32 address,prefix,mask;
    if(cli_argc==1){if(net_cidr(cli_argv[0],&address,&prefix,1)<0)return net_usage("ipcalc",usage);mask=net_mask(prefix);}
    else if(cli_argc==2){if(sc_net_parse_ip(cli_argv[0],&address)<0 || sc_net_parse_ip(cli_argv[1],&mask)<0)return net_usage("ipcalc",usage);int p=net_prefix(mask);if(p<0)return 2;prefix=(u32)p;}
    else return net_usage("ipcalc",usage);
    cli_text(1,"ADDRESS=");net_ip(1,address);cli_text(1,"\nNETMASK=");net_ip(1,mask);cli_text(1,"\nPREFIX=");net_uint(1,prefix);
    cli_text(1,"\nNETWORK=");net_ip(1,address&mask);cli_text(1,"\nBROADCAST=");net_ip(1,(address&mask)|~mask);
    cli_text(1,"\nHOSTMIN=");net_ip(1,(address&mask)+(prefix<31?1u:0u));cli_text(1,"\nHOSTMAX=");net_ip(1,((address&mask)|~mask)-(prefix<31?1u:0u));cli_text(1,"\n");return 0;}
