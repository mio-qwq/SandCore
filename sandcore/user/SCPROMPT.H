#ifndef SCPROMPT_H
#define SCPROMPT_H
#include "SCIO.H"
static int prompt_line(const char *prompt,char *out,u32 capacity,int secret)
{
    if(!capacity || cli_text(1,prompt)<0)return -1;u32 used=0;
    for(;;){u8 c;int n=cli_read(0,&c,1);if(n<=0)return -1;if(c==3)return -1;
        if(c=='\r')continue;if(c=='\n'){out[used]=0;cli_text(1,"\n");return (int)used;}
        if(c==8 || c==127){if(used){used--;if(!secret)cli_text(1,"\b \b");}continue;}
        if(c<32 && c!='\t')continue;if(used+1==capacity)return -2;out[used++]=(char)c;if(!secret)cli_write(1,&c,1);
    }
}
static void prompt_wipe(void *data,u32 bytes){volatile u8 *p=data;while(bytes--)*p++=0;}
static int prompt_ticket(u32 ticket)
{
    if(!ticket)return -1;u32 info[8];int result;while((result=sc_auth_status(ticket,info))==1)sc_yield();return result;
}
#endif
