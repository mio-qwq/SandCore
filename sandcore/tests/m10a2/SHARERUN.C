#include "SC32API.H"

/* 旧SandShell只有文件重定向，没有2>&1语法。父Shell先真正>OUT，
 * 本程序用正式Spawn把同一端点传给子stdout/stderr，不自写syscall、
 * 不在库内模拟共享游标，也不复制或链接任何库实现。 */
int main(void)
{
    char command[256];ScU32 bytes=0;
    if(ScProcessArguments(command,sizeof(command),&bytes) || !bytes || bytes>248u)return 80;
    for(ScU32 n=0;n<bytes;n++)if(command[n]==' ' || command[n]=='\t'
        || command[n]=='\n' || command[n]=='\'' || command[n]=='"')return 81;
    const char *suffix=" shared";
    for(ScU32 n=0;n<8u;n++)command[bytes+n]=suffix[n];
    ScI32 streams[3]={0,1,1};ScJob job=0;
    if(ScProcessSpawn(command,streams,0,&job))return 82;
    ScJobInfo result={sizeof(ScJobInfo),1,0,0,0,0,0,0,{0,0,0,0}};
    ScStatus status=ScJobWait(job,SC32_INFINITE,&result);
    if(status){ScJobCancel(job);return 83;}
    /* Wait成功已领取完成票据，不能再Close而把正确子退出码覆成STALE。 */
    return result.exit_code;
}
