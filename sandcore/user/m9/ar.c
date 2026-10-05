#include "../SCFILE.H"
#define AR_LIMIT 16777216u
typedef struct {char name[16];const u8 *bytes;u32 size;int removed,allocated;} ar_member;
static ar_member members[1024];static int count,verbose;static u8 *archive;
static int decimal_field(const u8 *p,u32 bytes,u32 *out)
{u32 n=0;int found=0,spaces=0;for(u32 i=0;i<bytes;i++){char c=(char)p[i];if(c==' '){spaces=1;continue;}if(spaces || c<'0'||c>'9')return -1;u32 d=(u32)(c-'0');if(n>(0xFFFFFFFFu-d)/10)return -1;n=n*10+d;found=1;}*out=n;return found?0:-1;}
static int parse(u32 size)
{
    if(size<8 || text_compare((char *)archive,8,"!<arch>\n",8,0))return -1;u32 at=8;
    while(at<size){if(count==1024 || size-at<60 || archive[at+58]!='`' || archive[at+59]!='\n')return -1;u32 bytes;if(decimal_field(archive+at+48,10,&bytes)<0 || bytes>size-at-60)return -1;
        ar_member *m=members+count++;int n=0;while(n<16 && archive[at+n]!=' ' && archive[at+n]!='/')n++;
        if(!n || n>15 || archive[at]=='#')return -1;for(int i=0;i<n;i++){u8 c=archive[at+i];if(c<=32 || c=='\\')return -1;m->name[i]=(char)c;}m->name[n]=0;
        if(equal(m->name,".") || equal(m->name,".."))return -1;m->bytes=archive+at+60;m->size=bytes;at+=60+bytes;
        if(bytes&1){if(at==size || archive[at]!='\n')return -1;at++;}}
    return at==size?0:-1;
}
static int selected(const char *name,int first)
{if(first==cli_argc)return 1;for(int i=first;i<cli_argc;i++)if(equal(name,file_basename(cli_argv[i])))return 1;return 0;}
static void field(u8 *p,u32 bytes,u32 value,int base)
{char text[12];u32 n=0;do{text[n++]=(char)('0'+value%(u32)base);value/=(u32)base;}while(value);for(u32 i=0;i<bytes;i++)p[i]=' ';for(u32 i=0;i<n && i<bytes;i++)p[i]=(u8)text[n-1-i];}
static int publish(const char *path,int input)
{
    u32 bytes=8;for(int i=0;i<count;i++)if(!members[i].removed){u32 n=60+members[i].size+(members[i].size&1);if(n>AR_LIMIT-bytes)return -1;bytes+=n;}
    int out=sc_stream_open(path,2,bytes);if(out<0)return -1;int result=1;if(input>=0 && sc_stream_seek(input,0)<0)goto done;
    if(cli_text(out,"!<arch>\n")<0)goto done;
    for(int i=0;i<count;i++)if(!members[i].removed){ar_member *m=members+i;u8 header[60];for(int j=0;j<60;j++)header[j]=' ';int n=length(m->name);for(int j=0;j<n;j++)header[j]=(u8)m->name[j];header[n]='/';
        field(header+16,12,0,10);field(header+28,6,0,10);field(header+34,6,0,10);field(header+40,8,0100644,8);field(header+48,10,m->size,10);header[58]='`';header[59]='\n';
        if(cli_write(out,header,60)<0 || cli_write(out,m->bytes,m->size)<0 || ((m->size&1) && cli_text(out,"\n")<0))goto done;sc_yield();}
    if(sc_stream_close(out,1)<0)goto done;out=-1;result=0;
done:
    if(out>=0)sc_stream_close(out,0);return result;
}
int main(void)
{
    if(cli_parse()<2)return 2;const char *flags=cli_argv[0];if(*flags=='-')flags++;int op=0;
    for(int i=0;flags[i];i++){char c=flags[i];if(c=='t'||c=='p'||c=='x'||c=='q'||c=='r'||c=='d'){if(op)return 2;op=c;}else if(c=='v')verbose=1;else if(c!='c')return 2;}if(!op)return 2;
    int input=sc_stream_open(cli_argv[1],1,0),result=1;u32 bytes=0;
    if(input<0){char path[64];u32 info[2];if((op!='q' && op!='r') || cli_path(cli_argv[1],path)<0 || sc_stat(path,info)!=-1)goto done;}
    else {archive=(u8 *)cli_slurp(input,AR_LIMIT,&bytes);if(!archive || parse(bytes)<0)goto done;}
    if(op=='q'||op=='r'){if(cli_argc==2)goto done;u32 owned_bytes=0,projected=archive?bytes:8;for(int a=2;a<cli_argc;a++){const char *name=file_basename(cli_argv[a]);int size=length(name),replace=-1;
            if(size<1 || size>15 || equal(name,".") || equal(name,".."))goto done;for(int i=0;i<size;i++)if((u8)name[i]<=32 || name[i]=='/' || name[i]=='\\')goto done;
            if(op=='r')for(int i=0;i<count;i++)if(equal(members[i].name,name)){replace=i;break;}
            /* 先算目标大小及同时保留的新成员正文，再读文件。原归档
             * 最多16MiB，新正文总量也最多16MiB；不能每个参数各读
             * 16MiB到最后才发现超过归档上限，造成数百MiB峰值。 */
            char path[64];u32 info[2];if(cli_path(cli_argv[a],path)<0 || sc_stat(path,info)<0 || info[0]!=1 || info[1]>AR_LIMIT-60)goto done;
            u32 previous=replace<0?0:60+members[replace].size+(members[replace].size&1),added=60+info[1]+(info[1]&1);
            if(added>AR_LIMIT-(projected-previous) || info[1]>AR_LIMIT-owned_bytes || (replace<0 && count==1024))goto done;
            int fd=cli_input(a);if(fd<0 || !fd)goto done;u32 n;u8 *data=(u8 *)cli_slurp(fd,info[1],&n);sc_stream_close(fd,0);if(!data)goto done;
            if(n!=info[1]){sc_free(data);goto done;}projected=projected-previous+added;
            if(replace<0)replace=count++;ar_member *m=members+replace;if(m->allocated){owned_bytes-=m->size;sc_free((void *)m->bytes);}owned_bytes+=n;
            copy(m->name,name,16);m->bytes=data;m->size=n;m->allocated=1;if(verbose){cli_text(1,op=='q'?"a - ":"r - ");cli_text(1,name);cli_text(1,"\n");}}
        result=publish(cli_argv[1],input);goto done;}
    if(op=='d'){if(cli_argc==2)goto done;for(int i=0;i<count;i++)if(selected(members[i].name,2))members[i].removed=1;result=publish(cli_argv[1],input);goto done;}
    result=0;for(int i=0;i<count;i++){ar_member *m=members+i;if(!selected(m->name,2))continue;
        if(op=='t'){if(cli_text(1,m->name)<0 || cli_text(1,"\n")<0){result=1;break;}}
        else if(op=='p'){if(cli_write(1,m->bytes,m->size)<0){result=1;break;}}
        else {int fd=sc_stream_open(m->name,2,m->size);if(fd<0){result=1;break;}int r=cli_write(fd,m->bytes,m->size);if(r>=0)r=sc_stream_close(fd,1);if(r<0){sc_stream_close(fd,0);result=1;break;}if(verbose){cli_text(1,"x - ");cli_text(1,m->name);cli_text(1,"\n");}}sc_yield();}
done:
    for(int i=0;i<count;i++)if(members[i].allocated)sc_free((void *)members[i].bytes);if(archive)sc_free(archive);if(input>=0)sc_stream_close(input,0);if(result)cli_text(2,"ar: format/option/input/output failure\n");return result;
}
