#include "../SCFILE.H"
int main(void)
{
    if(cli_parse()!=4)return cli_error("adduser NAME UID GID HOME",-1);int uid,gid;if(cli_integer(cli_argv[1],&uid)<0 || cli_integer(cli_argv[2],&gid)<0)return 2;
    char path[64];if(cli_path(cli_argv[3],path)<0)return 2;u32 meta[8];int created=0;
    if(sc_fsmeta(path,meta)<0){int r=sc_mkdir(path);if(r<0)return cli_error("adduser: home",r);created=1;}
    else if(meta[1]!=2)return 1;
    int r=sc_auth_account(0,cli_argv[0],path,uid,gid);if(r<0){if(created)sc_remove(path);return cli_error("adduser",r);}
    r=sc_permissions(path,uid,gid,3);if(r<0)return cli_error("adduser: home permissions",r);
    cli_text(1,"Account created; set its password with passwd ");cli_text(1,cli_argv[0]);cli_text(1,"\n");return 0;
}
