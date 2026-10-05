#include "../SCFILE.H"
static const char *pattern;static int desired;
static int glob(const char *s,const char *p)
{const char *star=0,*back=0;while(*s){if(*p=='?' || *p==*s){p++;s++;}else if(*p=='*'){star=++p;back=s;}else if(star){p=star;s=++back;}else return 0;}while(*p=='*')p++;return !*p;}
static int visit(const char *path,int kind,u32 bytes,int after,void *context)
{(void)bytes;(void)context;if(after || (desired && desired!=kind) || (pattern && !glob(file_basename(path),pattern)))return 0;
    if(cli_text(1,"/")<0 || cli_text(1,path)<0 || cli_text(1,"\n")<0)return -1;return 0;}
int main(void){if(cli_parse()<0)return 2;int at=0;const char *root=".";if(cli_argc && cli_argv[0][0]!='-')root=cli_argv[at++];
    while(at<cli_argc){char *option=cli_argv[at++];if(equal(option,"-print"))continue;if(at==cli_argc)return 2;
        if(equal(option,"-name"))pattern=cli_argv[at++];else if(equal(option,"-type")){char *type=cli_argv[at++];if(equal(type,"f"))desired=1;else if(equal(type,"d"))desired=2;else return 2;}else return 2;}
    int r=file_walk(root,0,visit,0);return r<0?cli_error("find",r):0;}
