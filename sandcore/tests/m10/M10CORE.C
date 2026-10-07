#include "SCIO.H"
/* 只用既有公开模块ABI核对32B边界；密码验签和内部池归属由真实
 * 启动日志及外部COM1读内存交叉验证，不增加客体测试后门。 */
int main(void)
{
    int arguments=cli_parse();
    if(arguments==1 && equal(cli_argv[0],"snapshot")){
        /* 服务每秒更新原文件，管理传输跨多个帧会正确拒绝代数变化。
         * 单次文件调用读齐24B后另存不可变样本，保留传输一致性边界。 */
        u32 service[8];service[0]=0xA10C0004;service[7]=0xA10C0005;
        if(sc_read("/TMP/M10SERVICE",service+1,24)!=24
            || service[0]!=0xA10C0004 || service[7]!=0xA10C0005)return 4;
        return sc_write("/TMP/M10SNAP",service+1,24)==24?0:5;
    }
    if(arguments!=0)return 6;
    u32 guarded[10];guarded[0]=0xA10C0001;guarded[9]=0xA10C0002;
    for(int i=1;i<9;i++)guarded[i]=0xA10C0003;
    if(sc_module_info(guarded+1)<0 || guarded[0]!=0xA10C0001 || guarded[9]!=0xA10C0002
        || guarded[1]!=1 || guarded[7] || guarded[8])return 1;
    if(sc_write("/TMP/M10MODULE",guarded+1,32)!=32)return 2;
    if(sc_module_exec("/SYS/CORE/CORE.SKM",0)!=-5
        || sc_module_exec("/SYS/CORE/A20-SERVICE.SKM",1)!=-5
        || sc_module_exec("/SYS/RECOVERY/CORE.SKM",0)!=-5)return 3;
    cli_text(1,"PASS MODULEINFO exact 32 bytes and old reserved words\n");
    cli_text(1,"PASS CORE and RECOVERY cannot be manually initialized\n");
    return 0;
}
