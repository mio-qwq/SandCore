#ifndef SCEXEC_H
#define SCEXEC_H
#include "SCIO.H"
/* 完整参数文本的统一编码；程序名由内核PATH解析，参数逐项保留
 * 空串/空格/转义。容量失败不得执行被截断的程序或参数。 */
static int exec_command(char out[1024],const char *program,char **args,int count)
{
    int used=length(program);if(!used || used>63)return -1;for(int i=0;i<used;i++)if((u8)program[i]<=32 || program[i]=='"' || program[i]=='\'')return -1;
    copy(out,program,1024);for(int i=0;i<count;i++){if(used+3>=1024)return -1;out[used++]=' ';out[used++]='"';
        for(const char *p=args[i];*p;p++){if(*p=='"'||*p=='\\'){if(used==1023)return -1;out[used++]='\\';}if(used==1023)return -1;out[used++]=*p;}
        if(used==1023)return -1;out[used++]='"';}out[used]=0;return used;
}
static int exec_wait(u32 ticket)
{u32 state[8];for(;;){int n=sc_wait2(ticket,state);if(n<0)return 1;if(!n)return (int)state[2];sc_yield();}}
#endif
