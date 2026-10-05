#include "SCPROMPT.H"
int main(void)
{
    if(cli_parse()<0 || cli_argc>1)return 2;char name[32],password[129];
    if(cli_argc){if(length(cli_argv[0])>=32)return 2;copy(name,cli_argv[0],32);}
    else if(LOGIN_ASK_NAME){if(prompt_line("User: ",name,32,0)<=0)return 1;}
    else copy(name,"root",32);
    /* 所有身份切换由内核验证票据并创建新任务；不修改父Shell的UID。
     * SYSTEM在此路径始终拒绝，即使父进程来自串口也不能普通登录。 */
    if(prompt_line("Password: ",password,129,1)<0){prompt_wipe(password,129);return 1;}
    int ticket=sc_auth_login(name,password);prompt_wipe(password,129);if(ticket<=0 || prompt_ticket((u32)ticket)<0){if(ticket>0)sc_auth_cancel((u32)ticket);return cli_error("login",-5);}
    int child=sc_auth_exec((u32)ticket,"/BIN/SH.SCX",0);sc_auth_cancel((u32)ticket);if(child<1)return cli_error("login",child);
    /* AUTHEXEC的历史返回是PID而非SPAWN2作业票据，用进程代数确认
     * 终止，不能把随后复用同一PID的任务误当此登录Shell。 */
    u32 table[520],generation=0;for(;;){if(sc_processes2(table,520)<0)return 1;int alive=0;
        for(u32 i=0;i<table[1];i++){u32 *row=table+8+i*table[2];if(row[0]==(u32)child && row[1] && row[1]!=2){
            if(!generation)generation=row[2];if(generation==row[2])alive=1;}}
        if(!alive)break;sc_yield();}return 0;
}
