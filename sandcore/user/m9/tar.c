#include "../SCFILE.H"
/* 自写USTAR流式归档；仅普通文件/目录，未知扩展或链接明确拒绝。
 * 解包不接受绝对路径/..，不会按归档UID变换执行身份或伪造SYSTEM。
 * 每文件以COW事务发布，归档整体不宣称跨文件原子提交。 */
static int output=1,list_only,extracting,verbose;static u32 archive_bytes;
static char archive_path[64];
static int octal(u8 *out,int bytes,u32 value)
{for(int i=bytes-2;i>=0;i--){out[i]=(u8)('0'+(value&7));value>>=3;}out[bytes-1]=0;return value?-1:0;}
static int parse_octal(const u8 *p,int bytes,u32 *out)
{u32 n=0;int seen=0;for(int i=0;i<bytes;i++){u8 c=p[i];if(c==0 || c==' '){if(seen)break;continue;}if(c<'0'||c>'7' || n>(0xFFFFFFFFu-(c-'0'))/8)return -1;n=n*8+(c-'0');seen=1;}*out=n;return 0;}
static int pack(const char *path,int kind,u32 bytes,int after,void *context)
{
    int writing=*(int *)context;if(after || !*path || (*archive_path && equal(path,archive_path)))return 0;
    if(bytes>0xFFFFFDFFu)return -1;u32 needed=512+(kind==1?(bytes+511)&~511u:0);if(archive_bytes>0xFFFFFFFFu-needed)return -1;archive_bytes+=needed;
    if(!writing)return 0;u32 metadata[8];if(sc_fsmeta(path,metadata)<0)return -1;
    u8 header[512];for(int i=0;i<512;i++)header[i]=0;u32 name=(u32)length(path);for(u32 i=0;i<name;i++)header[i]=(u8)path[i];if(kind==2)header[name]='/';
    u32 mode=0;for(int i=0;i<3;i++){u32 rw=(metadata[5]>>(i*2))&3u;mode|=((rw&1?4u:0)|(rw&2?2u:0))<<((2-i)*3);}
    if(octal(header+100,8,mode)<0 || octal(header+108,8,(int)metadata[3]<0?0:metadata[3])<0 || octal(header+116,8,(int)metadata[4]<0?0:metadata[4])<0
        || octal(header+124,12,kind==1?bytes:0)<0 || octal(header+136,12,0)<0)return -1;
    for(int i=148;i<156;i++)header[i]=' ';header[156]=kind==2?'5':'0';copy((char *)header+257,"ustar",6);header[263]='0';header[264]='0';
    u32 checksum=0;for(int i=0;i<512;i++)checksum+=header[i];if(octal(header+148,7,checksum)<0)return -1;header[155]=' ';
    if(cli_write(output,header,512)<0)return -1;if(verbose){cli_text(2,path);cli_text(2,"\n");}
    if(kind==1){char rooted[65];rooted[0]='/';copy(rooted+1,path,64);int fd=sc_stream_open(rooted,1,0);if(fd<0)return -1;u8 buffer[4096];u32 copied=0;
        while(copied<bytes){int n=cli_read(fd,buffer,bytes-copied>4096?4096:bytes-copied);if(n<=0 || cli_write(output,buffer,(u32)n)<0){sc_stream_close(fd,0);return -1;}copied+=(u32)n;}
        u8 extra;int n=cli_read(fd,&extra,1);sc_stream_close(fd,0);if(n)return -1;
        for(int i=0;i<512;i++)header[i]=0;u32 pad=(512-(bytes&511))&511;if(pad && cli_write(output,header,pad)<0)return -1;
    }return 0;
}
static int read_exact(int fd,void *out,u32 count)
{u32 at=0;while(at<count){int n=cli_read(fd,(u8 *)out+at,count-at);if(n<=0)return -1;at+=(u32)n;}return 0;}
static int safe_path(const char *path)
{
    if(!*path || *path=='/' || *path=='\\')return 0;const char *p=path;
    while(*p){const char *start=p;while(*p && *p!='/')p++;if(p-start==2 && start[0]=='.' && start[1]=='.')return 0;
        for(const char *c=start;c<p;c++)if(*c=='\\' || *c==':')return 0;if(*p)p++;}return 1;
}
static int make_parents(const char *name,int include)
{
    char resolved[64];if(cli_path(name,resolved)<0)return -1;
    for(int at=0;;at++){char c=resolved[at];if(c=='/' || (!c && include)){resolved[at]=0;u32 info[2];int r=sc_stat(resolved,info);
            if(r<0)r=sc_mkdir(resolved);else if(info[0]!=2)r=-1;resolved[at]=c;if(r<0)return r;}if(!c)break;}return 0;
}
static int unpack(int fd)
{
    u8 header[512],buffer[4096];int zeros=0;
    for(;;){if(read_exact(fd,header,512)<0)return -1;int empty=1;for(int i=0;i<512;i++)if(header[i])empty=0;
        if(empty){if(++zeros==2)return 0;continue;}if(zeros)return -1;
        u32 checksum=0,expected,size,mode;for(int i=0;i<512;i++)checksum+=(i>=148 && i<156)?32:header[i];
        if(parse_octal(header+148,8,&expected)<0 || expected!=checksum || parse_octal(header+124,12,&size)<0 || parse_octal(header+100,8,&mode)<0)return -1;
        if(header[156] && header[156]!='0' && header[156]!='5')return -2;
        char name[257];int n=0;if(header[257]){
            if(header[257]!='u'||header[258]!='s'||header[259]!='t'||header[260]!='a'||header[261]!='r')return -1;
            for(int i=345;i<500 && header[i];i++)name[n++]=(char)header[i];if(n)name[n++]='/';}
        for(int i=0;i<100 && header[i];i++)name[n++]=(char)header[i];name[n]=0;if(!n || n>63 || !safe_path(name))return -2;
        while(n>1 && name[n-1]=='/')name[--n]=0;int directory=header[156]=='5';if(directory && size)return -1;
        if(list_only || verbose){cli_text(1,name);cli_text(1,directory?"/\n":"\n");}
        int target=-1;if(extracting){if(make_parents(name,directory)<0)return -1;if(!directory){target=sc_stream_open(name,2,size);if(target<0)return target;}}
        u32 remaining=size;while(remaining){u32 take=remaining>4096?4096:remaining;if(read_exact(fd,buffer,take)<0 || (target>=0 && cli_write(target,buffer,take)<0)){
                if(target>=0)sc_stream_close(target,0);return -1;}remaining-=take;}
        u32 pad=(512-(size&511))&511;if(pad && read_exact(fd,buffer,pad)<0){if(target>=0)sc_stream_close(target,0);return -1;}
        if(target>=0){int r=sc_stream_close(target,1);if(r<0){sc_stream_close(target,0);return r;}
            char path[64];u32 rw=0;for(int i=0;i<3;i++){u32 bits=(mode>>((2-i)*3))&7u;rw|=((bits&4?1u:0)|(bits&2?2u:0))<<(i*2);}
            if(cli_path(name,path)<0 || sc_permissions(path,-2,-2,rw)<0)return -1;}
    }
}
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,create=0;const char *archive=0;
    while(at<cli_argc){char *options=cli_argv[at++];if(options[0]=='-')options++;else if(at>1){at--;break;}
        for(int i=0;options[i];i++){char c=options[i];if(c=='c')create++;else if(c=='x')extracting++;else if(c=='t')list_only++;else if(c=='v')verbose=1;
            else if(c=='f'){if(at==cli_argc)return 2;archive=cli_argv[at++];}else return 2;}
        if(at==cli_argc || cli_argv[at][0]!='-')break;
    }
    if(create+extracting+list_only!=1)return 2;
    if(!create){if(at!=cli_argc)return 2;int fd=archive && !equal(archive,"-")?sc_stream_open(archive,1,0):0;if(fd<0)return 1;int r=unpack(fd);if(fd)sc_stream_close(fd,0);return r<0?cli_error("tar",r):0;}
    if(at==cli_argc)return 2;if(archive && !equal(archive,"-") && cli_path(archive,archive_path)<0)return 2;
    int writing=0;archive_bytes=1024;for(int i=at;i<cli_argc;i++)if(file_walk(cli_argv[i],0,pack,&writing)<0)return 1;
    u32 capacity=archive_bytes;if(*archive_path){output=sc_stream_open(archive,2,capacity);if(output<0)return cli_error("tar",output);}
    writing=1;archive_bytes=1024;int result=0;for(int i=at;i<cli_argc;i++)if(file_walk(cli_argv[i],0,pack,&writing)<0){result=1;break;}
    u8 end[1024];for(int i=0;i<1024;i++)end[i]=0;if(!result && (archive_bytes!=capacity || cli_write(output,end,1024)<0))result=1;
    if(output>2){int r=sc_stream_close(output,!result);if(r<0){sc_stream_close(output,0);result=1;}}return result;
}
