#include "../SCPROMPT.H"
int main(void)
{
    if(cli_parse()<0 || cli_argc>1)return 2;char name[32];u32 identity[8];if(sc_auth_info(identity)<0)return 1;
    if(cli_argc)copy(name,cli_argv[0],32);else if(sc_user(0,"",name,32)<0)return 1;
    u32 proof=0;char password[129],confirmation[129];int result=1;
    if((int)identity[1]!=0 && (int)identity[1]!=-1){
        if(prompt_line("Current password: ",password,sizeof(password),1)<0)goto done;
        int ticket=sc_auth_login(name,password);prompt_wipe(password,sizeof(password));if(ticket<=0)goto done;proof=(u32)ticket;
        if(prompt_ticket(proof)<0)goto done;
    }
    if(prompt_line("New password: ",password,sizeof(password),1)<=0 || prompt_line("Again: ",confirmation,sizeof(confirmation),1)<0 || !equal(password,confirmation))goto done;
    int change=sc_auth_password(name,password,proof);prompt_wipe(password,sizeof(password));prompt_wipe(confirmation,sizeof(confirmation));
    if(change<=0)goto done;proof=(u32)change;if(prompt_ticket(proof)<0)goto done;cli_text(1,"Password updated\n");result=0;
done:prompt_wipe(password,sizeof(password));prompt_wipe(confirmation,sizeof(confirmation));if(proof)sc_auth_cancel(proof);return result;
}
