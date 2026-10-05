#include "../SCFILE.H"
int main(void){if(cli_parse()<2)return 2;char *who=cli_argv[0],*colon=who;while(*colon && *colon!=':')colon++;int uid,gid=-2;
    if(*colon){*colon++=0;if(cli_integer(colon,&gid)<0)return 2;}if(cli_integer(who,&uid)<0)return 2;
    for(int i=1;i<cli_argc;i++){char path[64];u32 meta[8];if(cli_path(cli_argv[i],path)<0 || sc_fsmeta(path,meta)<0)return 1;int r=sc_permissions(path,uid,gid,meta[5]);if(r<0)return cli_error("chown",r);}return 0;}
