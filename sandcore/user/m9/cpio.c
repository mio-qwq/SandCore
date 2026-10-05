#include "../SCUU.H"
#include "../SCGLOB.H"
#define CPIO_LIMIT 16777216u
typedef struct {char name[64];const u8 *data;u32 size,mode;} cpio_member;
static cpio_member members[1024];static u32 member_count,output_size,inode=1;static int zero_names,make_dirs,list_only,verbose;
static int output=1;static char output_path[64];
static int emit(const void *bytes,u32 n)
{if(n>CPIO_LIMIT-output_size || cli_write(output,bytes,n)<0)return -1;output_size+=n;return 0;}
static int pad(u32 count){static const u8 zero[4]={0,0,0,0};return emit(zero,(0u-count)&3);}
static void hex(u8 *out,u32 n)
{static const char *digits="0123456789abcdef";for(int i=7;i>=0;i--){out[i]=(u8)digits[n&15];n>>=4;}}
static int record(const char *name,u32 mode,u32 bytes,int fd)
{
    u8 header[110];for(int i=0;i<110;i++)header[i]='0';for(int i=0;i<6;i++)header[i]=(u8)"070701"[i];u32 n=(u32)length(name)+1;
    hex(header+6,inode++);hex(header+14,mode);hex(header+38,(mode&0170000)==0040000?2:1);hex(header+54,bytes);hex(header+94,n);
    if(emit(header,110)<0 || emit(name,n)<0 || pad(110+n)<0)return -1;u8 chunk[4096];u32 at=0;
    while(at<bytes){u32 take=bytes-at;if(take>4096)take=4096;int got=cli_read(fd,chunk,take);if(got<=0 || emit(chunk,(u32)got)<0)return -1;at+=(u32)got;sc_yield();}
    if(fd>=0 && sc_stream_seek(fd,0)<0)return -1;return pad(bytes);
}
static u32 unix_mode(u32 kind,u32 rw)
{u32 mode=kind==2?0040000:0100000;for(int i=0;i<3;i++){u32 pair=(rw>>(i*2))&3,permissions=(pair&1?4u:0u)|(pair&2?2u:0u);if(kind==2 && (pair&1))permissions|=1;mode|=permissions<<(6-i*3);}return mode;}
static int create(void)
{
    text_reader reader={0,{0},0,0};char name[64];u32 used=0;int c,result=0;
    for(;;){c=text_next(&reader);if(c>=0 && c!=(zero_names?0:'\n')){if(used==63 || (!zero_names && !c)){result=1;break;}name[used++]=(char)c;continue;}
        if(c<0 && c!=-256){result=1;break;}if(!used){if(c<0)break;continue;}name[used]=0;
        const char *relative=name;while(relative[0]=='.' && relative[1]=='/')relative+=2;
        if(!equal(relative,".") && !uu_safe_name(relative)){result=1;break;}char path[64];u32 meta[8];
        if(cli_path(name,path)<0 || (output_path[0] && equal(path,output_path)) || sc_fsmeta(path,meta)<0 || (meta[1]!=1 && meta[1]!=2)){result=1;break;}
        int fd=meta[1]==1?sc_stream_open(name,1,0):-1;if(meta[1]==1 && fd<0){result=1;break;}
        if(fd>=0){u32 current[8];if(sc_fsmeta(path,current)<0 || current[7]!=meta[7] || current[2]!=meta[2]){sc_stream_close(fd,0);result=1;break;}}
        int r=record(relative,unix_mode(meta[1],meta[5]),meta[1]==1?meta[2]:0,fd);if(fd>=0)sc_stream_close(fd,0);if(r<0){result=1;break;}
        if(verbose){cli_text(2,relative);cli_text(2,"\n");}used=0;if(c<0)break;}
    if(!result && record("TRAILER!!!",0,0,-1)<0)result=1;return result;
}
static int unhex(const u8 *p,u32 *value)
{u32 n=0;for(int i=0;i<8;i++){char c=(char)p[i];int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(d<0)return -1;n=(n<<4)|(u32)d;}*value=n;return 0;}
static int validate(const u8 *archive,u32 bytes)
{
    u32 at=0;while(at<bytes){if(bytes-at<110)return -1;const u8 *head=archive+at;int crc=head[5]=='2';if(text_compare((const char *)head,5,"07070",5,0) || (!crc && head[5]!='1'))return -1;
        u32 fields[13];for(int i=0;i<13;i++)if(unhex(head+6+i*8,fields+i)<0)return -1;
        u32 mode=fields[1],n=fields[11],size=fields[6],check=fields[12];if(!n || n>64 || n>bytes-at-110 || head[110+n-1])return -1;
        char name[64];for(u32 i=0;i<n-1;i++){if(!head[110+i])return -1;name[i]=(char)head[110+i];}name[n-1]=0;
        u32 aligned=(110+n+3)&~3u;if(aligned>bytes-at)return -1;at+=aligned;if(size>bytes-at)return -1;
        if(equal(name,"TRAILER!!!")){if(size || check)return -1;for(u32 i=at;i<bytes;i++)if(archive[i])return -1;return 0;}
        const char *relative=name;while(relative[0]=='.' && relative[1]=='/')relative+=2;
        if(!equal(relative,".") && !uu_safe_name(relative))return -1;u32 kind=mode&0170000;if(kind!=0100000 && kind!=0040000)return -1;
        if((kind==0100000 && fields[4]>1) || (kind==0040000 && size) || (!crc && check) || member_count==1024)return -1;
        if(crc){u32 actual=0;for(u32 i=0;i<size;i++)actual+=archive[at+i];if(actual!=check)return -1;}
        for(u32 i=0;i<member_count;i++)if(equal(members[i].name,relative))return -1;
        cpio_member *m=members+member_count++;copy(m->name,relative,64);m->data=archive+at;m->size=size;m->mode=mode;
        u32 padded=(size+3)&~3u;if(padded<size || padded>bytes-at)return -1;at+=padded;}
    return -1;
}
static int parents(const char *path)
{
    char name[64];copy(name,path,64);for(int i=0;name[i];i++)if(name[i]=='/'){name[i]=0;char absolute[64];u32 info[2];if(cli_path(name,absolute)<0)return -1;
        int r=sc_stat(absolute,info);if(r==-1 && make_dirs)r=sc_mkdir(absolute);else if(r>=0 && info[0]!=2)r=-1;name[i]='/';if(r<0)return -1;}return 0;
}
static int extract(int input,int patterns)
{
    u32 bytes;u8 *archive=(u8 *)cli_slurp(input,CPIO_LIMIT,&bytes);if(!archive)return 1;int result=1;
    /* 先检查整个容器/结束标记/成员路径，再进行任何目录或正文修改。
     * 提交仍逐成员；后续权限失败不能声称先前成员也整体回滚。 */
    if(validate(archive,bytes)<0)goto done;result=0;
    for(u32 i=0;i<member_count;i++){cpio_member *m=members+i;int selected=patterns==cli_argc;for(int p=patterns;p<cli_argc;p++)if(glob_match(m->name,cli_argv[p]))selected=1;if(!selected)continue;
        if(list_only){if(cli_text(1,m->name)<0 || cli_text(1,"\n")<0){result=1;break;}continue;}if(equal(m->name,"."))continue;
        if(parents(m->name)<0){result=1;break;}char absolute[64];if(cli_path(m->name,absolute)<0){result=1;break;}
        if((m->mode&0170000)==0040000){u32 info[2];int r=sc_stat(absolute,info);if(r==-1)r=sc_mkdir(absolute);else if(r>=0 && info[0]!=2)r=-1;if(r<0){result=1;break;}}
        else {int fd=sc_stream_open(m->name,2,m->size);if(fd<0){result=1;break;}int r=cli_write(fd,m->data,m->size);if(r>=0)r=sc_stream_close(fd,1);if(r<0){sc_stream_close(fd,0);result=1;break;}}
        if(verbose){cli_text(2,m->name);cli_text(2,"\n");}sc_yield();}
done:
    sc_free(archive);return result;
}
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,operation=0;const char *archive_name=0;
    for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(equal(p,"-F")){if(++at==cli_argc)return 2;archive_name=cli_argv[at];continue;}
        if(equal(p,"-H")){if(++at==cli_argc || !equal(cli_argv[at],"newc"))return 2;continue;}if(*p!='-' || !p[1])break;
        for(int i=1;p[i];i++){char c=p[i];if(c=='i'||c=='o'){if(operation && operation!=c)return 2;operation=c;}else if(c=='t')list_only=1;else if(c=='d')make_dirs=1;
            else if(c=='v')verbose=1;else if(c=='0')zero_names=1;else return 2;}}
    if(!operation || (operation=='o' && (at!=cli_argc || list_only || make_dirs)) || (operation=='i' && zero_names))return 2;
    int fd=operation=='o'?1:0,result;
    if(archive_name){if(operation=='o' && cli_path(archive_name,output_path)<0)return 2;fd=sc_stream_open(archive_name,operation=='o'?2:1,operation=='o'?CPIO_LIMIT:0);if(fd<0)return 1;}
    if(operation=='o'){output=fd;result=create();}else result=extract(fd,at);
    if(archive_name){int r=sc_stream_close(fd,operation=='o' && !result);if(r<0){sc_stream_close(fd,0);result=1;}}
    if(result)cli_text(2,"cpio: format/path/unsupported/input/output failure\n");return result;
}
