#include "../SCIO.H"
/* SandCore常驻SKM列表，真实装载名称，不枚举磁盘文件冒充已运行。 */
int main(void)
{if(cli_parse()!=0)return 2;char names[2049];int n=sc_module_list(names,sizeof(names));if(n<0)return cli_error("lsmod",n);return cli_write(1,names,(u32)n)<0?1:0;}
