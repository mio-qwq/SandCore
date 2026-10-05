#include "SCIO.H"
#include "SCPROMPT.H"
static int wait_pid(int pid)
{
    u32 rows[520],generation=0,start=sc_tick();for(;;){if(sc_processes2(rows,520)<0)return 1;int alive=0;
        for(u32 i=0;i<rows[1];i++){u32 *row=rows+8+i*rows[2];if(row[0]==(u32)pid && row[1] && row[1]!=2){if(!generation)generation=row[2];if(generation==row[2])alive=1;}}
        if(!alive)return 0;if((u32)sc_tick()-start>30000)return 1;sc_yield();}
}
static int switch_child(const char *name,const char *mode,const char *password)
{
    /* 同一创建者的登录尝试至少相隔200tick。前一个子程序可能很快
     * 结束，测试也必须遵守真实节流，不能为验收放宽内核策略。 */
    u32 start=sc_tick();int proof;
    do{proof=sc_auth_login(name,password);if(proof!=-6)break;sc_yield();}while((u32)sc_tick()-start<400);
    if(proof<=0){cli_text(1,"FAIL login ");cli_text(1,name);cli_text(1," ");cli_number(1,proof);cli_text(1,"\n");return 1;}
    if(prompt_ticket((u32)proof)<0){cli_text(1,"FAIL ticket ");cli_text(1,name);cli_text(1,"\n");return 1;}
    char command[256];copy(command,"/TMP/M9CHECK.SCX ",sizeof(command));append(command,mode,sizeof(command));
    int pid=sc_auth_exec((u32)proof,command,0);sc_auth_cancel((u32)proof);
    if(pid<1){cli_text(1,"FAIL identity exec ");cli_text(1,name);cli_text(1,"\n");return 1;}return wait_pid(pid);
}
int main(void)
{
    /* 密码只从不回显的stdin读取，不出现在命令参数/输出或磁盘。 */
    cli_text(1,"M9 SECRET READY\n");char password[129];if(prompt_line("",password,sizeof(password),1)<1)return 1;u32 before[8],after[8];if(sc_auth_info(before)<0 || (int)before[1]!=-1 || before[3]!=2)return 1;
    int ticket=sc_auth_password("root",password,0);if(ticket<=0 || prompt_ticket((u32)ticket)<0)return 1;sc_auth_cancel((u32)ticket);
    if(sc_auth_account(0,"m9tester","/TMP",1001,1001)<0)return 1;ticket=sc_auth_password("m9tester",password,0);if(ticket<=0 || prompt_ticket((u32)ticket)<0)return 1;sc_auth_cancel((u32)ticket);
    int fd=sc_stream_open("/TMP/M9PRIVATE",2,6);if(fd<0)return 1;cli_write(fd,"secret",6);if(sc_stream_close(fd,1)<0 || sc_permissions("/TMP/M9PRIVATE",0,0,3)<0)return 1;
    int result=switch_child("root","root",password);result|=switch_child("m9tester","user",password);prompt_wipe(password,sizeof(password));
    if(sc_auth_info(after)<0 || after[1]!=before[1] || after[2]!=before[2] || after[3]!=before[3])result=1;
    cli_text(1,result?"FAIL parent identity/lifecycle\n":"PASS parent identity unchanged\n");return result;
}
