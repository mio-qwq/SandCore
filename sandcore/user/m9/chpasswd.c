#include "../SCIO.H"
#include "../SCPROMPT.H"
int main(void)
{
    if(cli_parse()!=0)return 2;u32 identity[8];if(sc_auth_info(identity)<0 || !((int)identity[1]==0 || identity[4]))return cli_error("chpasswd: administrator required",-5);
    char line[164];u32 used=0;int result=0;
    for(;;){u8 byte;int read=cli_read(0,&byte,1);if(read<0){result=1;break;}
        if(read && byte!='\n'){if(byte==3){result=130;break;}if(!byte || used==sizeof(line)-1){result=2;break;}line[used++]=(char)byte;continue;}
        if(!used){if(!read)break;continue;}if(line[used-1]=='\r')used--;line[used]=0;int separator=-1;
        for(u32 i=0;i<used;i++)if(line[i]==':'){separator=(int)i;break;}
        if(result || separator<1 || separator>31 || used-(u32)separator-1>128){result=2;break;}line[separator]=0;
        /* 直接调用内核异步改密票据；不生成自造passwd散列，不用
         * SUID或从环境指定UID。stdin和票据始终归真实调用者。 */
        int ticket=sc_auth_password(line,line+separator+1,0);prompt_wipe(line,sizeof(line));used=0;
        if(ticket<=0){result=1;break;}int done=prompt_ticket((u32)ticket);sc_auth_cancel((u32)ticket);if(done<0){result=1;break;}if(!read)break;
    }
    prompt_wipe(line,sizeof(line));return result?cli_error("chpasswd: input/account/credential",result):0;
}
