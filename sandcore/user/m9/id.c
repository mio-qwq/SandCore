#include "../SCIO.H"
int main(void)
{
    if(cli_parse()<0)return 2;int select=0,names=0;
    for(int i=0;i<cli_argc;i++){
        const char *arg=cli_argv[i];if(arg[0]!='-' || !arg[1])return 2;
        for(int j=1;arg[j];j++){
            if(arg[j]=='u' || arg[j]=='g' || arg[j]=='G'){
                int next=arg[j]=='u'?1:2;if(select && select!=next)return 2;select=next;
            }else if(arg[j]=='n')names=1;
            else if(arg[j]!='r')return 2;
        }
    }
    if(names && (!select || select==2))return 2;
    u32 identity[8];if(sc_auth_info(identity)<0)return 1;
    if(select){
        /* 本版没有有效/真实身份双轨或附加组；-r不改变内核凭据。
         * 无组名数据库时明确拒绝-gn，不能忽略选项后输出另一种布局。 */
        if(names){char name[32];if(sc_user(0,"",name,32)<0)return 1;cli_text(1,name);}
        else cli_number(1,(int)identity[select]);
        cli_text(1,"\n");return 0;
    }
    cli_text(1,"uid=");cli_number(1,(int)identity[1]);cli_text(1," gid=");
    cli_number(1,(int)identity[2]);cli_text(1," realm=");cli_number(1,(int)identity[3]);cli_text(1,"\n");return 0;
}
