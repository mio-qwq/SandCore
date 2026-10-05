#ifndef SCLOG_H
#define SCLOG_H
#include "SCIO.H"
/* 本地用户日志是普通SandFS文件，不能冒充内核安全审计或系统日志
 * daemon。每次完整追加走COW，读取代数用于避免两个写者丢更新。 */
static int local_log_path(char path[64])
{
    /* 旧USERCONFIG返回卷根相对的HOME，ABI不能改成带斜杠的字符串。
     * 这里先显式锚定卷根，否则从其它cwd写日志会误落到cwd/HOME。 */
    char home[64],rooted[65],absolute[64];if(sc_user(1,"",home,sizeof(home))<0)return -1;
    rooted[0]='/';copy(rooted+1,home,64);if(cli_path(rooted,absolute)<0 || length(absolute)+10>=64)return -1;
    path[0]='/';copy(path+1,absolute,63);if(*absolute)append(path,"/",64);append(path,"LOG.TXT",64);return 0;
}
#endif
