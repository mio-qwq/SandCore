/* mio：普通三环编译事务驱动，专为确定性的并发编辑验收。
 * 先读取源的完整快照写独立HOME/SNAP.C，由实际历史G2生成输出；
 * 子编译器实际退出后仍等待本次提交满1200个PITtick再返回原码。
 * 这样CPU很快时也保留真实未完成事务供鼠标/保存/Open操作，而
 * 不修改Studio版本/任务状态/时钟，不拿驱动等待时间称G2速度。
 * 它只用于独立测试盘BIN/S3C.SCX，正式编译器和默认盘不改。 */
#include "SCAPI.H"
static char snapshot[65536];
volatile u32 driver_state[8];
int main(void)
{
    int started=sc_tick(),win=sc_open("Build transaction / mio",250,110);
    if(win<0)return 1;
    sc_text(win,8,12,"Actual G2 / delayed return",PAL_UI_TEXT);
    char args[128],command[128];sc_args(args,sizeof(args));char *p=args;
    char *input=token(&p),*output=token(&p);
    u32 info[2];if(sc_stat(input,info)||info[0]!=1||info[1]>=sizeof(snapshot))return 2;
    int n=sc_read(input,snapshot,info[1]);if(n!=(int)info[1])return 3;
    if(sc_write("HOME/SNAP.C",snapshot,n)!=n)return 4;
    driver_state[1]=info[1];driver_state[0]=1;
    copy(command,"BIN/G2.SCX HOME/SNAP.C ",sizeof(command));append(command,output,sizeof(command));
    int pid=sc_exec(command);if(pid<0)return 5;driver_state[2]=pid;
    int code;
    do{code=sc_status(pid);sc_yield();}while(code==0x40000000||code==0x40000001);
    driver_state[3]=code;driver_state[0]=2;
    while(sc_tick()-started<1200)sc_yield();
    driver_state[0]=3;return code;
}
