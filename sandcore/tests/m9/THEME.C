#include "SCIO.H"
/* 只调用正式主题API，以客体回读证明截图确实切换了主题。 */
int main(void)
{
    if(cli_parse()!=1)return 2;int classic=equal(cli_argv[0],"classic");
    if(!classic && !equal(cli_argv[0],"aurora"))return 2;
    const char *path=classic?"/SYS/THEMES/CLASSIC.CFG":"/SYS/THEMES/AURORA.CFG";
    int r=sc_theme_load(path,0);if(r<0)return cli_error("theme",r);
    u32 info[32];if(sc_theme(info)<0 || info[1]!=(u32)classic)return 1;
    cli_text(1,classic?"PASS Classic\n":"PASS Aurora\n");return 0;
}
