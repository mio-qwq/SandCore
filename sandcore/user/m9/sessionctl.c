#include "../SCTEXT.H"
static int list(void)
{
    u32 page[SC_PROCESS_PAGE_WORDS],cursor=0xFFFFFFFFu;
    cli_text(1,"ID  UID  GID  TYPE  ACTIVE  TASKS  NAME\n");
    do{int r=sc_session_page(page,SC_PROCESS_PAGE_WORDS,cursor);if(r<0)return cli_error("sessionctl",r);
        for(u32 i=0;i<page[3];i++){u32 *row=page+16+i*16;
            text_unsigned(1,row[0]);cli_text(1,"  ");cli_number(1,(int)row[1]);cli_text(1,"  ");cli_number(1,(int)row[2]);cli_text(1,"  ");
            cli_text(1,row[3]==1?"SYSTEM":row[3]==2?"LOCKED":"user");cli_text(1,"  ");cli_text(1,row[4]?"yes":"no");cli_text(1,"  ");
            text_unsigned(1,row[6]);cli_text(1,"  ");cli_text(1,(char *)(row+8));cli_text(1,"\n");}
        cursor=page[4];
    }while(cursor!=0xFFFFFFFFu);return 0;
}
int main(void)
{
    if(cli_parse()<0)return 2;
    if(cli_argc==1 && (equal(cli_argv[0],"--help") || equal(cli_argv[0],"-h"))){
        cli_text(1,"sessionctl [list | show ID | logout ID]\nSYSTEM desktop requires the external management session.\n");return 0;}
    if(!cli_argc || (cli_argc==1 && equal(cli_argv[0],"list")))return list();
    u32 id;if(cli_argc!=2 || text_number(cli_argv[1],&id)<0 || !id || id>0x7FFFFFFFu)return 2;
    int action=equal(cli_argv[0],"show")?0:equal(cli_argv[0],"logout")?1:-1;if(action<0)return 2;
    int r=sc_session_control(id,(u32)action);return r<0?cli_error("sessionctl",r):0;
}
