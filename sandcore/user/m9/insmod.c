#include "../SCIO.H"
/* 独立常驻入口；rmmod/热重载按用户合同不提供。 */
int main(void)
{if(cli_parse()!=1)return 2;char path[64];if(cli_path(cli_argv[0],path)<0)return 2;int result=sc_module_exec(path,1);return result<0?cli_error("insmod",result):result;}
